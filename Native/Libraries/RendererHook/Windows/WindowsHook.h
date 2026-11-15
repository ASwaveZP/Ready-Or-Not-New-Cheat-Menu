#pragma once
#include <Windows.h>
#include "../BaseHook.h"
class WindowsHook : public BaseHook
{
public:
#define WINDOWS_DLL "user32.dll"
public:
    virtual ~WindowsHook();

    void ResetRenderState();
    void PrepareForOverlay(HWND);

    HWND GetGameHwnd() const;
    WNDPROC GetGameWndProc() const;

    bool StartHook();
    static WindowsHook* Instance();
    virtual const char* GetLibName() const;
private:
    // Window message hook
    WindowsHook();

    static LRESULT CALLBACK HookWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
private:
    static WindowsHook* _inst;

    // Variables
    bool initialized;
    HWND _game_hwnd;
    WNDPROC _game_wndproc;
};

