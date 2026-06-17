#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "dx9_hook.hpp"
#include "cef_manager.hpp"
#include "rpc_receiver.hpp"

static HMODULE g_hModule = nullptr;

DWORD WINAPI MainThread(LPVOID)
{
    // Wait for GTA SA + SA-MP/OMP to fully initialize their D3D9 device
    Sleep(3000);

    RpcReceiver::Install();
    DX9Hook::Install();
    CefManager::Init(g_hModule);

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        CloseHandle(CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr));
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        CefManager::Shutdown();
    }
    return TRUE;
}
