#include "MenuHost.h"

#include <metahost.h>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "mscoree.lib")

namespace {
    std::mutex g_runtimeMutex;
    ICLRRuntimeHost* g_runtimeHost = nullptr;
    std::wstring g_menuAssemblyPath;
    std::vector<HWND> g_pendingToggles;

    bool ExtractMenuAssembly(HMODULE module, std::wstring& outputPath) {
        const auto resource = FindResourceW(module, MAKEINTRESOURCEW(101), MAKEINTRESOURCEW(10));
        if (!resource) return false;
        const auto loaded = LoadResource(module, resource);
        const auto* bytes = static_cast<const BYTE*>(LockResource(loaded));
        const DWORD size = SizeofResource(module, resource);
        if (!bytes || size == 0) return false;

        wchar_t tempPath[MAX_PATH]{};
        if (!GetTempPathW(MAX_PATH, tempPath)) return false;
        outputPath = tempPath;
        outputPath += L"RONMenu.UI_" + std::to_wstring(GetCurrentProcessId()) + L".dll";
        const HANDLE file = CreateFileW(outputPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        const bool ok = WriteFile(file, bytes, size, &written, nullptr) && written == size;
        CloseHandle(file);
        return ok;
    }
}

bool MenuHost::Start(HMODULE module) {
    std::wstring assemblyPath;
    if (!ExtractMenuAssembly(module, assemblyPath)) {
        OutputDebugStringW(L"RON Menu: failed to extract the embedded WPF assembly.\n");
        return false;
    }

    ICLRMetaHost* metaHost = nullptr;
    ICLRRuntimeInfo* runtimeInfo = nullptr;
    ICLRRuntimeHost* runtimeHost = nullptr;
    HRESULT result = CLRCreateInstance(CLSID_CLRMetaHost, IID_ICLRMetaHost,
        reinterpret_cast<void**>(&metaHost));
    if (SUCCEEDED(result)) result = metaHost->GetRuntime(L"v4.0.30319", IID_ICLRRuntimeInfo,
        reinterpret_cast<void**>(&runtimeInfo));
    if (SUCCEEDED(result)) result = runtimeInfo->GetInterface(CLSID_CLRRuntimeHost, IID_ICLRRuntimeHost,
        reinterpret_cast<void**>(&runtimeHost));
    if (SUCCEEDED(result)) result = runtimeHost->Start();

    DWORD managedResult = 0;
    if (SUCCEEDED(result)) result = runtimeHost->ExecuteInDefaultAppDomain(assemblyPath.c_str(),
        L"RonMenu.MenuLauncher", L"Start", L"", &managedResult);

    if (SUCCEEDED(result) && managedResult == 0) {
        std::vector<HWND> pendingToggles;
        {
            std::lock_guard lock(g_runtimeMutex);
            g_runtimeHost = runtimeHost;
            g_menuAssemblyPath = std::move(assemblyPath);
            runtimeHost = nullptr;
            pendingToggles.swap(g_pendingToggles);
        }
        for (const HWND window : pendingToggles)
            MenuHost::Toggle(window);
    }
    if (runtimeHost) runtimeHost->Release();
    if (runtimeInfo) runtimeInfo->Release();
    if (metaHost) metaHost->Release();
    if (FAILED(result)) {
        wchar_t message[128]{};
        swprintf_s(message, L"RON Menu: CLR hosting failed (0x%08X).\n", static_cast<unsigned>(result));
        OutputDebugStringW(message);
        return false;
    }
    return managedResult == 0;
}

bool MenuHost::Toggle(HWND gameWindow) {
    ICLRRuntimeHost* runtimeHost = nullptr;
    std::wstring assemblyPath;
    {
        std::lock_guard lock(g_runtimeMutex);
        if (!g_runtimeHost) {
            g_pendingToggles.push_back(gameWindow);
            return true;
        }
        runtimeHost = g_runtimeHost;
        runtimeHost->AddRef();
        assemblyPath = g_menuAssemblyPath;
    }
    DWORD managedResult = 0;
    const HRESULT result = runtimeHost->ExecuteInDefaultAppDomain(assemblyPath.c_str(),
        L"RonMenu.MenuLauncher", L"Toggle", std::to_wstring(reinterpret_cast<uintptr_t>(gameWindow)).c_str(), &managedResult);
    runtimeHost->Release();
    return SUCCEEDED(result) && managedResult == 0;
}
