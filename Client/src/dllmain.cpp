#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <cstdio>
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

static FILE* g_log = nullptr;

static void Log(const char* msg)
{
    if (!g_log) return;
    fprintf(g_log, "%s\n", msg);
    fflush(g_log);
}

DWORD WINAPI MainThread(LPVOID)
{
    std::string logPath = GetDllDir() + "\\omp-nui-debug.log";
    g_log = fopen(logPath.c_str(), "w");
    Log("MainThread started");

    Log("Calling CefManager::Init...");
    CefManager::Init(g_hModule);
    Log("CefManager::Init returned");

    std::string nuiLocal = GetDllDir() + "\\nui-local";
    Log(("ShowConnecting: " + nuiLocal).c_str());
    CefManager::ShowConnecting(nuiLocal);
    Log("ShowConnecting returned");

    Log("Sleeping 3s for D3D9 init...");
    Sleep(3000);

    Log("Installing DX9Hook...");
    DX9Hook::Install();
    Log("DX9Hook installed");

    Log("Installing RpcReceiver...");
    RpcReceiver::Install();
    Log("RpcReceiver installed — NUI ready");

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
