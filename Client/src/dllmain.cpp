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

void NUILog(const char* msg)
{
    if (!g_log) return;
    fprintf(g_log, "%s\n", msg);
    fflush(g_log);
}

// Isolated function so __try can coexist with C++ stack objects in caller
static DWORD TryCefInit(HMODULE hMod)
{
    __try {
        CefManager::Init(hMod);
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

DWORD WINAPI MainThread(LPVOID)
{
    std::string logPath = GetDllDir() + "\\omp-nui-debug.log";
    g_log = fopen(logPath.c_str(), "w");
    NUILog("MainThread started");

    NUILog("Calling CefManager::Init...");
    DWORD exCode = TryCefInit(g_hModule);
    if (exCode != 0)
    {
        char buf[64];
        sprintf_s(buf, sizeof(buf), "CefInitialize CRASHED: 0x%08X", exCode);
        NUILog(buf);
        return 1;
    }
    NUILog("CefManager::Init returned OK");

    std::string nuiLocal = GetDllDir() + "\\nui-local";
    NUILog(("ShowConnecting: " + nuiLocal).c_str());
    CefManager::ShowConnecting(nuiLocal);
    NUILog("ShowConnecting returned");

    NUILog("Sleeping 3s for D3D9 init...");
    Sleep(3000);

    NUILog("Installing DX9Hook...");
    DX9Hook::Install();
    NUILog("DX9Hook installed");

    NUILog("Installing RpcReceiver...");
    RpcReceiver::Install();
    NUILog("RpcReceiver installed — NUI ready");

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
