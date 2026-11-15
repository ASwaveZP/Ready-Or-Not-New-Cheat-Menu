#include "InputOwnership.h"

#include "Utils.h"

#include <SDK/Engine_parameters.hpp>
#include <SDK/ReadyOrNot_parameters.hpp>

#include <Windows.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <string>

// ---------------------------------------------------------------------------
// Why this module exists
//
// Ready or Not drives three separate things from its own input-mode state:
//   1. bShouldShowMouseCursor  -> RON's UMG cursor widget (UCursorWidget)
//   2. bShowMouseCursor        -> the engine-level cursor flag
//   3. the viewport mouse capture -> camera look + per-frame cursor recentering
//
// The WPF menu draws its own cursor and never wants a system or in-game cursor
// on top of it. So while the menu is visible we pin both cursor flags to false
// and only release the *capture*, so that:
//   * the game stops recentering the OS cursor every frame (that recentering is
//     what made the camera feel like it was dragging the pointer around and
//     stop dead at the screen edge), and
//   * no second pointer is drawn by the game.
//
// When the menu closes we restore game input mode, which re-acquires the
// capture and gives camera look back to the player.
//
// The menu is otherwise completely passive towards the rest of the system:
//   * the game window is never moved, resized, minimized or restored - not even
//     to "fix" it - and no resolution is ever handed to the engine or display,
//     because a display-mode switch rescales every window on the desktop;
//   * the pointer is never warped, and no mouse message is ever synthesised for
//     the game: while the menu is open the game receives nothing at all.
// ---------------------------------------------------------------------------

namespace {
    std::atomic<bool> g_requested{ false };
    std::atomic<bool> g_requestedState{ false };
    std::atomic<bool> g_applied{ false };
    std::atomic<bool> g_appliedState{ false };
    // Number of frames left to keep re-asserting cursor suppression after a
    // transition. A display-mode change makes the game recompute its input state,
    // and it can flip the cursor flags back on right after we set them.
    std::atomic<int> g_holdFrames{ 0 };
    constexpr int kHoldFrames = 20;

    // ---------------------------------------------------------------------
    // Diagnostics. Everything below runs on the game thread and only logs on
    // transitions, so the cost is negligible, but when the window or cursor
    // misbehaves again the log says exactly which step did it.
    // ---------------------------------------------------------------------
    void Log(const char* format, ...) {
        char message[512]{};
        va_list arguments;
        va_start(arguments, format);
        const int length = _vsnprintf_s(message, sizeof(message), _TRUNCATE, format, arguments);
        va_end(arguments);
        if (length <= 0) return;

        wchar_t directory[MAX_PATH]{};
        if (!GetTempPathW(MAX_PATH, directory)) return;
        std::wstring path = directory;
        path += L"RONMenu.log";

        const HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;

        SYSTEMTIME time{};
        GetLocalTime(&time);
        char line[640]{};
        const int lineLength = _snprintf_s(line, sizeof(line), _TRUNCATE, "[%02u:%02u:%02u.%03u] %s\r\n",
            time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, message);
        if (lineLength > 0) {
            DWORD written = 0;
            WriteFile(file, line, static_cast<DWORD>(lineLength), &written, nullptr);
        }
        CloseHandle(file);
    }

    UClass* ControllerClass() {
        return AReadyOrNotPlayerController::StaticClass();
    }

    // UFunction lookups are cached, but a null result is not treated as final:
    // the lookup needs GObjects to be ready, so we keep retrying until it works.
    UFunction* ResolveFunction(const char* functionName) {
        UClass* controllerClass = ControllerClass();
        if (!controllerClass) return nullptr;
        return controllerClass->GetFunction("ReadyOrNotPlayerController", functionName);
    }

    UFunction* ChangeInputModeFn() {
        static UFunction* cached = nullptr;
        if (!cached) cached = ResolveFunction("ChangeInputMode");
        return cached;
    }

    UFunction* UpdateMouseCursorVisibilityFn() {
        static UFunction* cached = nullptr;
        if (!cached) cached = ResolveFunction("UpdateMouseCursorVisibility");
        return cached;
    }

    UFunction* DisableForceShowMouseCursorFn() {
        static UFunction* cached = nullptr;
        if (!cached) cached = ResolveFunction("DisableForceShowMouseCursor");
        return cached;
    }

    // ProcessEvent on a function whose parameter layout we only partially know
    // is the one place where a bad pointer turns into a hard crash. Guard it so
    // a failed call degrades the menu instead of killing the game.
    void CallGuarded(UObject* object, UFunction* function, void* params) {
        if (!object || !function) return;
        __try {
            object->ProcessEvent(function, params);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            // Ignored on purpose: the caller re-pins the cursor flags afterwards.
        }
    }

    // UI input mode needs a widget to focus. Passing nullptr relies on RON
    // checking for it, which we cannot verify, so hand it a widget the
    // controller already owns instead. Subtitles come first: it is a passive
    // overlay, so focusing it has no visible side effect.
    UWidget* FocusWidget(AReadyOrNotPlayerController* controller) {
        if (!controller) return nullptr;
        if (controller->SubtitlesWidget) return reinterpret_cast<UWidget*>(controller->SubtitlesWidget);
        if (controller->TabMenuWidget) return reinterpret_cast<UWidget*>(controller->TabMenuWidget);
        return nullptr;
    }

    // Pin both cursor flags to "no cursor". The menu draws its own.
    void SuppressGameCursor(AReadyOrNotPlayerController* controller) {
        controller->bShouldShowMouseCursor = false;
        controller->bShowMouseCursor = false;
    }

    BOOL CALLBACK FindGameWindowProc(HWND window, LPARAM context) {
        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);
        if (processId != GetCurrentProcessId()) return TRUE;
        if (!IsWindowVisible(window)) return TRUE;
        // The menu window is owned by the game window, so skipping owned windows
        // leaves exactly the game's own top-level window. Tool windows are
        // skipped too: the menu panel and the cursor overlay are both
        // WS_EX_TOOLWINDOW, and the overlay has no owner at all - without this
        // check it could be mistaken for the game window.
        if (GetWindow(window, GW_OWNER) != nullptr) return TRUE;
        if ((GetWindowLongW(window, GWL_STYLE) & WS_CHILD) != 0) return TRUE;
        if ((GetWindowLongW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) return TRUE;
        *reinterpret_cast<HWND*>(context) = window;
        return FALSE;
    }

    HWND FindGameWindow() {
        HWND found = nullptr;
        EnumWindows(&FindGameWindowProc, reinterpret_cast<LPARAM>(&found));
        return found;
    }

    // Several of the per-frame paths below need the game window. EnumWindows on
    // every frame would be wasteful, so the handle is cached and only looked up
    // again once it stops being a valid window.
    HWND CachedGameWindow() {
        static HWND cached = nullptr;
        if (cached && IsWindow(cached)) return cached;
        cached = FindGameWindow();
        return cached;
    }

    // ---------------------------------------------------------------------
    // Display mode
    //
    // A separate topmost window cannot coexist with exclusive fullscreen:
    // Windows forces the game out of it, which shows the desktop, shrinks the
    // game to a small window and leaves RON's UMG cursor mapped to the old
    // viewport geometry.
    //
    // The fix is to move the game to borderless fullscreen - but it has to
    // happen BEFORE the first menu open. Doing it from the open path is what
    // made the first Insert resize the game window.
    // ---------------------------------------------------------------------
    constexpr int kExclusiveFullscreen = 0;

    UFunction* ResolveSettingsFunction(const char* functionName) {
        UClass* settingsClass = UGameUserSettings::StaticClass();
        if (!settingsClass) return nullptr;
        return settingsClass->GetFunction("GameUserSettings", functionName);
    }

    UGameUserSettings* GetGameUserSettings() {
        static UFunction* function = nullptr;
        if (!function) function = ResolveSettingsFunction("GetGameUserSettings");
        if (!function) return nullptr;
        UObject* defaultObject = UGameUserSettings::GetDefaultObj();
        if (!defaultObject) return nullptr;
        Params::GameUserSettings_GetGameUserSettings params{};
        __try {
            defaultObject->ProcessEvent(function, &params);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
        return params.ReturnValue;
    }

    // RON exposes its video mode through UBpVideoSettingsLib, the same library
    // the in-game options screen uses. Reading through it gives the mode the
    // game is really running in, not just the pending config value.
    UFunction* ResolveVideoSettingsFunction(const char* functionName) {
        UClass* libraryClass = UBpVideoSettingsLib::StaticClass();
        if (!libraryClass) return nullptr;
        return libraryClass->GetFunction("BpVideoSettingsLib", functionName);
    }

    int CurrentScreenMode() {
        static UFunction* function = nullptr;
        static bool resolved = false;
        if (!resolved) {
            resolved = true;
            function = ResolveVideoSettingsFunction("GetCurrentScreenMode");
        }
        if (!function) return -1;
        UObject* defaultObject = UBpVideoSettingsLib::GetDefaultObj();
        if (!defaultObject) return -1;
        Params::BpVideoSettingsLib_GetCurrentScreenMode params{};
        __try {
            defaultObject->ProcessEvent(function, &params);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return -1;
        }
        return static_cast<int>(params.ReturnValue.EnumValue);
    }

    // Never call UBpVideoSettingsLib::ChangeScreenResolution() here. It hands a
    // resolution to the display, and a display-mode switch rescales the entire
    // desktop - every other application's window changes size. That is exactly
    // the symptom that appeared when the menu was opened, so this module only
    // ever changes the *window mode*, through the settings object below.

    // Exclusive fullscreen cannot coexist with a topmost overlay window, so the
    // game is moved to borderless - once, at injection, never when the menu
    // opens.
    //
    // Only the *window mode* is touched. No resolution is ever passed to the
    // engine or to the display: RON keeps rendering at whatever resolution the
    // player already configured.
    bool EnsureBorderlessFullscreen() {
        int mode = CurrentScreenMode();
        if (mode < 0) {
            if (UGameUserSettings* settings = GetGameUserSettings()) mode = settings->FullscreenMode;
        }
        if (mode < 0) return false;                    // Engine not ready yet.
        if (mode != kExclusiveFullscreen) return true; // Already borderless or windowed.

        UGameUserSettings* settings = GetGameUserSettings();
        if (!settings) return false;

        static UFunction* setFullscreenMode = nullptr;
        if (!setFullscreenMode) setFullscreenMode = ResolveSettingsFunction("SetFullscreenMode");
        static UFunction* applyResolution = nullptr;
        if (!applyResolution) applyResolution = ResolveSettingsFunction("ApplyResolutionSettings");
        if (!setFullscreenMode || !applyResolution) return false;

        Log("display: exclusive fullscreen detected, switching to borderless only");
        Params::GameUserSettings_SetFullscreenMode modeParams{};
        modeParams.InFullscreenMode = EWindowMode::WindowedFullscreen;
        __try {
            settings->ProcessEvent(setFullscreenMode, &modeParams);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("display: SetFullscreenMode failed");
            return false;
        }

        Params::GameUserSettings_ApplyResolutionSettings applyParams{};
        applyParams.bCheckForCommandLineOverrides = false;
        __try {
            settings->ProcessEvent(applyResolution, &applyParams);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("display: ApplyResolutionSettings failed");
            return false;
        }
        Log("display: borderless applied, resolution untouched");
        return true;
    }

    // The menu is a WS_EX_NOACTIVATE window, so it never needs to own the
    // foreground. Hand the foreground back to the game on every transition:
    //   * a fullscreen game that loses activation drops out of fullscreen, which
    //     is why the desktop appeared and the game shrank to a small window;
    //   * after a topmost overlay goes away Windows promotes whatever is next in
    //     the z-order, which is how unrelated background apps ended up on top.
    // The window geometry is deliberately left alone: SW_MAXIMIZE used to be
    // called here and that is itself a resize of the game window.
    void EnsureGameForeground() {
        const HWND game = CachedGameWindow();
        if (!game) return;
        // Activation only. The window's size, position and state are never
        // touched: the menu is an independent window and must not be able to
        // resize anything else on the desktop.
        if (IsIconic(game)) {
            Log("window: game is minimized, leaving it alone");
            return;
        }
        if (GetForegroundWindow() != game) SetForegroundWindow(game);
    }

    // ---------------------------------------------------------------------
    // Pointer isolation
    //
    // The menu owns the mouse outright while it is open: the game must not be
    // able to read the pointer position at all, so nothing is forwarded to it.
    // The actual blocking happens in WindowsHook::HookWndProc, which drops every
    // mouse message (and WM_INPUT) for the game window while IsMenuOpen() is
    // true. Here we only clear any cursor clip the game left behind, so the menu
    // cursor can travel the whole screen, and re-hide the OS pointer.
    // ---------------------------------------------------------------------
    void IsolatePointer() {
        // Drop any clip rect the game installed for camera look, otherwise the
        // pointer cannot reach parts of the screen while the menu is open. The
        // OS pointer itself is hidden by the menu windows (Cursor="None"), not by
        // ShowCursor, so its display count is left alone.
        //
        // GetClipCursor() fails when no clip is installed, which is the common
        // case once the first clear has happened. Reading the state first keeps
        // this a no-op instead of a ClipCursor() syscall on every single frame.
        RECT clip{};
        if (!GetClipCursor(&clip)) return;
        RECT desktop{};
        if (!GetWindowRect(GetDesktopWindow(), &desktop)) {
            ClipCursor(nullptr);
            return;
        }
        // A clip that already covers the whole desktop is as good as none.
        if (clip.left <= desktop.left && clip.top <= desktop.top &&
            clip.right >= desktop.right && clip.bottom >= desktop.bottom) return;
        ClipCursor(nullptr);
    }

    void NudgeGameCursor() {
        // Ask the game to re-evaluate its cursor. The game's own window
        // procedure answers WM_SETCURSOR by hiding the cursor again, which is
        // what has to happen after the menu handed the mouse back. We never set
        // a cursor shape ourselves - that is what broke the game's mouse lock.
        const HWND foreground = GetForegroundWindow();
        if (!foreground) return;
        DWORD processId = 0;
        GetWindowThreadProcessId(foreground, &processId);
        if (processId != GetCurrentProcessId()) return;
        PostMessageW(foreground, WM_SETCURSOR, reinterpret_cast<WPARAM>(foreground),
            MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    }

    // Pin the flags, clear any "force show" override and let the game recompute
    // its own cursor visibility. Called once per transition and then repeated for
    // a few frames, because a display-mode change can make the game flip the
    // flags back on right after we clear them.
    void ReassertCursorSuppression(AReadyOrNotPlayerController* controller) {
        SuppressGameCursor(controller);
        CallGuarded(controller, DisableForceShowMouseCursorFn(), nullptr);
        CallGuarded(controller, UpdateMouseCursorVisibilityFn(), nullptr);
        SuppressGameCursor(controller);
    }

    // Per-frame variant. UpdateMouseCursorVisibility() rebuilds RON's Slate
    // cursor widget, so running the three UFunctions above on every frame for as
    // long as the menu stayed open was pure waste - and it is what made the game
    // stutter while the menu was up. The flags are plain fields, so checking them
    // is free: only re-run the sequence when the game has actually flipped one
    // back on (the engine does that whenever it thinks the viewport regained
    // focus).
    void ReassertCursorSuppressionIfNeeded(AReadyOrNotPlayerController* controller) {
        if (!controller->bShouldShowMouseCursor && !controller->bShowMouseCursor) return;
        ReassertCursorSuppression(controller);
    }

    bool Apply(bool menuOpen) {
        AReadyOrNotPlayerController* controller = GetARONPC();
        if (!controller || !IsActorValid(controller)) return false;

        // Flags first: RON's UpdateMouseCursorVisibility() reads them.
        SuppressGameCursor(controller);

        // Switch input mode. This is the game's own input-mode switch, so
        // viewport capture, cursor clipping and camera look all follow it.
        // bMouseCursorEnabled is always false: the menu draws its own cursor and
        // a game cursor here would show up as a second pointer.
        if (UFunction* changeInputMode = ChangeInputModeFn()) {
            Params::ReadyOrNotPlayerController_ChangeInputMode params{};
            params.bGameMode = !menuOpen;
            params.bMouseCursorEnabled = false;
            params.Widget = menuOpen ? FocusWidget(controller) : nullptr;
            CallGuarded(controller, changeInputMode, &params);
        }

        // Applying an input mode can flip the cursor flags back on (the engine's
        // GameAndUI mode forces bShowMouseCursor = true), so pin them again.
        ReassertCursorSuppression(controller);

        // Keep the game in the foreground so it neither drops out of fullscreen
        // nor loses its place in the z-order.
        EnsureGameForeground();

        if (menuOpen) {
            // The menu now owns the pointer: unclip it so the menu cursor can
            // reach the whole screen. Every mouse message addressed to the game
            // is dropped by WindowsHook while IsMenuOpen() is true. Nothing is
            // ever done to the game's window itself.
            IsolatePointer();
        }
        else {
            // Menu closed: hand everything back. The game re-acquires the
            // capture through ChangeInputMode above, so it can clip and recenter
            // the pointer itself again.
            NudgeGameCursor();
        }

        Log("menu: %s (input mode applied)", menuOpen ? "open" : "closed");
        return true;
    }
}

void InputOwnership::RequestMenuState(bool menuOpen) {
    if (g_applied.load(std::memory_order_acquire) &&
        g_appliedState.load(std::memory_order_relaxed) == menuOpen)
        return;
    g_requestedState.store(menuOpen, std::memory_order_relaxed);
    g_requested.store(true, std::memory_order_release);
}

bool InputOwnership::IsMenuOpen() {
    return g_applied.load(std::memory_order_acquire) &&
        g_appliedState.load(std::memory_order_relaxed);
}

void InputOwnership::EnsureDisplayMode() {
    // Runs as soon as the engine is ready, well before the first menu toggle,
    // so the menu itself never has to touch the display mode. Retries until the
    // mode is actually known, throttled to a few times per second.
    static bool done = false;
    static int attempts = 0;
    if (done) return;
    if ((attempts++ % 20) != 0) return;
    if (!EnsureBorderlessFullscreen()) return;
    done = true;
    Log("display: ready (mode settled)");
}

void InputOwnership::ApplyPending() {
    // While the menu is open the game must not draw or track any cursor at all:
    // the menu's overlay owns the pointer. RON recomputes its cursor widget
    // continuously, so a one-off clear is not enough - re-assert every frame.
    // The hold window covers the frames right after the menu closes, so a late
    // recompute cannot flash the game cursor back on.
    const bool menuOpen = g_applied.load(std::memory_order_acquire) &&
        g_appliedState.load(std::memory_order_relaxed);
    const bool holding = g_holdFrames.load(std::memory_order_relaxed) > 0;
    if (menuOpen || holding) {
        if (holding) g_holdFrames.fetch_sub(1, std::memory_order_relaxed);
        AReadyOrNotPlayerController* controller = GetARONPC();
        if (controller && IsActorValid(controller)) ReassertCursorSuppressionIfNeeded(controller);
        if (menuOpen) {
            // Nothing is sent to the game here on purpose: the whole point is
            // that the game cannot read the pointer while the menu is up. The
            // clip is re-cleared every frame because the game re-installs its
            // own clip rect whenever it thinks the viewport is focused.
            IsolatePointer();
        }
    }

    if (!g_requested.load(std::memory_order_acquire)) return;
    const bool requested = g_requestedState.load(std::memory_order_relaxed);
    if (!Apply(requested)) return; // Controller not ready yet; retry on the next tick.

    g_appliedState.store(requested, std::memory_order_relaxed);
    g_applied.store(true, std::memory_order_release);
    g_requested.store(false, std::memory_order_release);
    g_holdFrames.store(kHoldFrames, std::memory_order_relaxed);
}
