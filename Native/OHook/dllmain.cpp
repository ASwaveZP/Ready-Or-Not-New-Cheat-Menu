#include <Windows.h>
#include <thread>
#include <iostream>
#include <atomic>

#include <SDK.hpp>
#include "ReadyornotOverlay.h"
#include "console.hpp"
#include "MenuPipe.h"
#include "MenuHost.h"
#include "Hotkey.h"

DWORD WINAPI MainThread(LPVOID lpReserved);
DWORD WINAPI KeyboardMessageThread(LPVOID lpReserved);

namespace {
    constexpr UINT ToggleInsertMessage = WM_APP + 0x34;
    HMODULE g_injectedModule = nullptr;
    std::atomic<DWORD> g_keyboardThreadId{ 0 };
    std::atomic<DWORD> g_menuKeyDown{ 0 };
    std::atomic<bool> g_keyboardHookReady{ false };
    HANDLE g_keyboardHookReadyEvent = nullptr;

    LRESULT CALLBACK InsertKeyboardHook(int code, WPARAM wParam, LPARAM lParam) {
        if (code == HC_ACTION) {
            const auto* key = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            const int configuredHotkey = MenuPipe::GetMenuHotkey();
            if (key && key->vkCode == static_cast<DWORD>(Hotkey::MainKey(configuredHotkey)) && key->vkCode != 0) {
                const DWORD virtualKey = key->vkCode;
                const bool keyUp = (key->flags & LLKHF_UP) != 0 || wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
                if (keyUp) {
                    if (g_menuKeyDown.exchange(0) == virtualKey) return 1;
                } else if ((wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) && Hotkey::ModifiersHeld(configuredHotkey)) {
                    DWORD processId = 0;
                    const HWND foreground = GetForegroundWindow();
                    GetWindowThreadProcessId(foreground, &processId);
                    if (processId == GetCurrentProcessId() && g_menuKeyDown.exchange(virtualKey) != virtualKey) {
                        PostThreadMessageW(g_keyboardThreadId.load(), ToggleInsertMessage,
                            reinterpret_cast<WPARAM>(foreground), 0);
                        return 1;
                    }
                }
            }
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }
}

DWORD WINAPI KeyboardMessageThread(LPVOID) {
    g_keyboardThreadId.store(GetCurrentThreadId());
    MSG message{};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    const HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, InsertKeyboardHook, g_injectedModule, 0);
    g_keyboardHookReady.store(hook != nullptr);
    if (g_keyboardHookReadyEvent) SetEvent(g_keyboardHookReadyEvent);
    if (!hook) return 0;

    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == ToggleInsertMessage)
            MenuHost::Toggle(reinterpret_cast<HWND>(message.wParam));
    }
    UnhookWindowsHookEx(hook);
    return 0;
}

DWORD WINAPI MenuHostThread(LPVOID lpReserved) {
    MenuHost::Start(static_cast<HMODULE>(lpReserved));
    return 0;
}

BOOL APIENTRY DllMain(const HMODULE hModule, const DWORD ul_reason_for_call) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        g_injectedModule = hModule;
        CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
    }

    return TRUE;
}

DWORD WINAPI MainThread(const LPVOID lpReserved) {
    g_keyboardHookReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (HANDLE keyboardThread = CreateThread(nullptr, 0, KeyboardMessageThread, nullptr, 0, nullptr))
        CloseHandle(keyboardThread);
    if (g_keyboardHookReadyEvent) {
        WaitForSingleObject(g_keyboardHookReadyEvent, 1000);
        CloseHandle(g_keyboardHookReadyEvent);
        g_keyboardHookReadyEvent = nullptr;
    }

    Console::Alloc();
    MenuPipe::Start();

    const auto Overlay = new ReadyornotOverlay();
    OverlayBase::Instance = Overlay;
    Overlay->SetupOverlay();

    // Do not hold renderer/window-hook setup behind CLR + WPF initialization.
    // The native MenuHost queues early Insert requests until the CLR host is ready.
    if (HANDLE menuHostThread = CreateThread(nullptr, 0, MenuHostThread, lpReserved, 0, nullptr))
        CloseHandle(menuHostThread);

    WaitForSingleObject(lpReserved, INFINITE);

    return TRUE;
}
