#include "WindowsHook.h"

#include "../ImGui/imgui.h"
#include "../ImGui/impls/windows/imgui_impl_win32.h"
#include "../Macros.h"
#include "../../../OHook/Core/MenuHost.h"
#include "../../../OHook/Core/InputOwnership.h"

namespace {
    // Everything through which the game could learn where the pointer is or act
    // on a mouse button. WM_INPUT matters most: RON reads raw mouse deltas for
    // camera look, and raw input is delivered regardless of which window sits
    // under the cursor, so blocking only WM_MOUSEMOVE would leave the camera
    // rotating while the menu is open.
    bool IsPointerMessage(UINT message) {
        switch (message) {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_INPUT:
        case WM_SETCURSOR:
            return true;
        default:
            return false;
        }
    }
}

WindowsHook* WindowsHook::_inst = nullptr;

bool WindowsHook::StartHook()
{
    return true;
}

void WindowsHook::ResetRenderState()
{
    if (initialized)
    {
        // Restore the original window procedure
        SetWindowLongPtr(_game_hwnd, GWLP_WNDPROC, (LONG_PTR)_game_wndproc);
        _game_hwnd = nullptr;
        _game_wndproc = nullptr;
        ImGui_ImplWin32_Shutdown();
        initialized = false;
    }
}

void WindowsHook::PrepareForOverlay(HWND hWnd)
{
    if (!initialized)
    {
        ImGui_ImplWin32_Init(hWnd);

        _game_hwnd = hWnd;
        _game_wndproc = (WNDPROC)SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)&WindowsHook::HookWndProc);

        initialized = true;
    }

    // The ImGui layer is a render-only HUD (ESP, aimbot overlay, etc.): it
    // never owns the mouse. The WPF menu draws its own independent cursor and
    // manages mouse ownership through InputOwnership.
    //
    // Without NoMouseCursorChange the Win32 backend answers WM_SETCURSOR with
    // SetCursor(IDC_ARROW) and ImGui_ImplWin32_NewFrame() can push a cursor of
    // its own, which overrides the game's hidden cursor. That is what makes the
    // Windows pointer show up on top of the game cursor and what breaks the
    // viewport mouse lock (camera look then stops at the screen edge).
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.MouseDrawCursor = false;

    ImGui_ImplWin32_NewFrame();
}

HWND WindowsHook::GetGameHwnd() const
{
    return _game_hwnd;
}

WNDPROC WindowsHook::GetGameWndProc() const
{
    return _game_wndproc;
}

/////////////////////////////////////////////////////////////////////////////////////
LRESULT CALLBACK WindowsHook::HookWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_KEYDOWN && wParam == VK_INSERT && !(lParam & (1 << 30))) {
        MenuHost::Toggle(hWnd);
        return 0;
    }

    // Hard pointer isolation. While the menu is open it owns the mouse
    // completely: the game never sees a move, a click, a wheel event or a raw
    // input packet, so it cannot read the pointer position at all and its own
    // cursor can never be dragged along by the menu cursor. Ownership is handed
    // back the moment the menu hides (IsMenuOpen() goes false), and the game
    // re-acquires its capture through InputOwnership's input-mode switch.
    if (InputOwnership::IsMenuOpen() && IsPointerMessage(uMsg)) {
        // WM_SETCURSOR has to report "handled" or DefWindowProc puts the class
        // cursor back on screen. Clearing the cursor here also keeps the system
        // arrow from being drawn on top of the menu's own glyph when the pointer
        // is over the game area.
        if (uMsg == WM_SETCURSOR) {
            SetCursor(nullptr);
            return TRUE;
        }
        return 0;
    }

    // Input is intentionally not forwarded to the ImGui backend. The WPF menu
    // manages its own cursor and input, and ImGui_ImplWin32_WndProcHandler
    // reacted to WM_SETCURSOR by calling SetCursor() with the Windows arrow -
    // stealing the cursor from the game and leaving a second pointer on screen.
    // ImGui_ImplWin32_NewFrame() still reads the mouse position via
    // GetCursorPos() so ESP/HUD overlays continue to track the mouse correctly.
    return CallWindowProc(WindowsHook::Instance()->_game_wndproc, hWnd, uMsg, wParam, lParam);
}
/////////////////////////////////////////////////////////////////////////////////////

WindowsHook::WindowsHook() :
    initialized(false),
    _game_hwnd(nullptr),
    _game_wndproc(nullptr)
{
    //_library = LoadLibrary(DLL_NAME);
}

WindowsHook::~WindowsHook()
{
    PRINT_DEBUG("Windows Hook removed\n");

    ResetRenderState();

    //FreeLibrary(reinterpret_cast<HMODULE>(_library));

    _inst = nullptr;
}

WindowsHook* WindowsHook::Instance()
{
    if (_inst == nullptr)
        _inst = new WindowsHook;

    return _inst;
}

const char* WindowsHook::GetLibName() const
{
    return WINDOWS_DLL;
}
