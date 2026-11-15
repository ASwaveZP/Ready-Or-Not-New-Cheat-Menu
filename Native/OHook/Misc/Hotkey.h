#pragma once

#include <Windows.h>

namespace Hotkey {
    constexpr int KeyMask = 0xFF;
    constexpr int Ctrl = 0x100;
    constexpr int Shift = 0x200;
    constexpr int Alt = 0x400;
    constexpr int ModifierMask = Ctrl | Shift | Alt;
    inline int MainKey(int binding) { return binding & KeyMask; }
    inline bool ModifierHeld(int modifier) {
        if (modifier == Ctrl) return (GetAsyncKeyState(VK_LCONTROL) & 0x8000) || (GetAsyncKeyState(VK_RCONTROL) & 0x8000);
        if (modifier == Shift) return (GetAsyncKeyState(VK_LSHIFT) & 0x8000) || (GetAsyncKeyState(VK_RSHIFT) & 0x8000);
        if (modifier == Alt) return (GetAsyncKeyState(VK_LMENU) & 0x8000) || (GetAsyncKeyState(VK_RMENU) & 0x8000);
        return false;
    }
    inline bool ModifiersHeld(int binding) {
        const int modifiers = binding & ModifierMask;
        return (!(modifiers & Ctrl) || ModifierHeld(Ctrl)) && (!(modifiers & Shift) || ModifierHeld(Shift)) && (!(modifiers & Alt) || ModifierHeld(Alt));
    }
    inline bool IsPressed(int binding) {
        const int key = MainKey(binding);
        return key != 0 && ModifiersHeld(binding) && (GetAsyncKeyState(key) & 0x8000) != 0;
    }
}
