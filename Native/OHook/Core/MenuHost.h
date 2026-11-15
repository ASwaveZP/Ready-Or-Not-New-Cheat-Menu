#pragma once

#include <Windows.h>

namespace MenuHost {
    bool Start(HMODULE module);
    bool Toggle(HWND gameWindow);
}
