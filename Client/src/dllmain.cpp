#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include "dx9_hook.hpp"
#include "cef_manager.hpp"
#include "rpc_receiver.hpp"

static HMODULE g_hModule = nullptr;

static std::string GetDllDir()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(g_hModule, path, MAX_PATH);
    std::string s(path);
    auto pos = s.rfind('\\');
    return (pos != std::string::npos) ? s.substr(0, pos) : s;
}

DWORD WINAPI MainThread(LPVOID)
{
    // 1. Init CEF immediately — connecting screen needs it
    CefManager::Init(g_hModule);

    // 2. Show local connecting screen right away (file://, no server needed)
    CefManager::ShowConnecting(GetDllDir() + "\\nui-local");

    // 3. Wait for GTA SA + SA-MP/OMP to fully initialize their D3D9 device
    Sleep(3000);

    // 4. Hook D3D9 Present so CEF overlays get rendered
    DX9Hook::Install();

    // 5. Hook recvfrom to receive NUI RPCs from server
    RpcReceiver::Install();

    // Connecting screen stays visible until server sends RPC 221 (NUIShow)
    // or until connection fails — handled via postMessage from rpc_receiver
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
