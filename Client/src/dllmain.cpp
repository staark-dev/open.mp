#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <cstdio>
#include "dx9_hook.hpp"
#include "cef_manager.hpp"
#include "rpc_receiver.hpp"

HMODULE g_hModule = nullptr;  // extern-visible for dx9_hook

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

    // Hook IDirect3D9::CreateDevice immediately so we catch GTA SA's device
    // creation (which happens after WinMain starts, well after our DLL loads).
    NUILog("Installing DX9Hook (CreateDevice intercept)...");
    DX9Hook::Install();
    NUILog("DX9Hook installed");

    // Wait for SA-MP to load before hooking recvfrom in its IAT
    NUILog("Waiting 5s for samp.dll to load...");
    Sleep(5000);

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
