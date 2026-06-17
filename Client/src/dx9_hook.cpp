#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <string>
#include "dx9_hook.hpp"
#include "cef_manager.hpp"

extern HMODULE g_hModule;
extern void    NUILog(const char*);

static std::string GetModuleDir(HMODULE hMod)
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(hMod, path, MAX_PATH);
    std::string s(path);
    auto pos = s.rfind('\\');
    return (pos != std::string::npos) ? s.substr(0, pos) : s;
}

static bool s_cefStarted = false;

// IDirect3DDevice9 vtable indices
static constexpr int VTX_RESET   = 16;
static constexpr int VTX_PRESENT = 17;

using Reset_t   = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using Present_t = HRESULT(WINAPI*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);

static Reset_t   oReset   = nullptr;
static Present_t oPresent = nullptr;

// ── vtable patch helper ───────────────────────────────────────────────────────

static void PatchVtable(void** vtable, int index, void* hook, void** original)
{
    DWORD old;
    VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &old);
    *original = vtable[index];
    vtable[index] = hook;
    VirtualProtect(&vtable[index], sizeof(void*), old, &old);
}

// ── Hooked Present ────────────────────────────────────────────────────────────

static HRESULT WINAPI hkPresent(IDirect3DDevice9* pDevice,
    const RECT* pSrc, const RECT* pDst, HWND hWnd, const RGNDATA* pDirty)
{
    DX9Hook::OnPresent(pDevice);
    return oPresent(pDevice, pSrc, pDst, hWnd, pDirty);
}

// ── Hooked Reset ─────────────────────────────────────────────────────────────

static HRESULT WINAPI hkReset(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pp)
{
    DX9Hook::OnReset(pDevice);
    return oReset(pDevice, pp);
}

// ── Install ───────────────────────────────────────────────────────────────────

void DX9Hook::Install()
{
    NUILog("DX9Hook::Install start");

    WNDCLASSA wc = {};
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpszClassName = "omp_nui_dummy";
    BOOL regOk = RegisterClassA(&wc);
    NUILog(regOk ? "RegisterClass OK" : "RegisterClass failed (may already exist)");

    HWND hwnd = CreateWindowExA(0, "omp_nui_dummy", nullptr, WS_POPUP,
                                0, 0, 2, 2, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd)
    {
        char buf[64];
        sprintf_s(buf, sizeof(buf), "CreateWindowEx failed: %u", GetLastError());
        NUILog(buf);
        return;
    }
    NUILog("CreateWindowEx OK");

    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d)
    {
        NUILog("Direct3DCreate9 returned null");
        DestroyWindow(hwnd);
        return;
    }
    NUILog("Direct3DCreate9 OK");

    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed         = TRUE;
    pp.SwapEffect       = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.hDeviceWindow    = hwnd;

    IDirect3DDevice9* dummy = nullptr;

    // Try hardware first (same flags as GTA SA), fall back to software
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                    D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dummy);
    if (FAILED(hr))
    {
        NUILog("CreateDevice HW failed, trying SW...");
        hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dummy);
    }

    if (SUCCEEDED(hr) && dummy)
    {
        void** vtable = *reinterpret_cast<void***>(dummy);
        char buf[128];
        sprintf_s(buf, sizeof(buf), "vtable=%p  Present slot=%p  Reset slot=%p",
                  vtable, vtable[VTX_PRESENT], vtable[VTX_RESET]);
        NUILog(buf);

        PatchVtable(vtable, VTX_PRESENT, (void*)hkPresent, (void**)&oPresent);
        PatchVtable(vtable, VTX_RESET,   (void*)hkReset,   (void**)&oReset);
        dummy->Release();
        NUILog("vtable patched OK");
    }
    else
    {
        char buf[64];
        sprintf_s(buf, sizeof(buf), "CreateDevice FAILED: 0x%08X", (unsigned)hr);
        NUILog(buf);
    }

    d3d->Release();
    DestroyWindow(hwnd);
    UnregisterClassA("omp_nui_dummy", wc.hInstance);
    NUILog("DX9Hook::Install done");
}

// ── OnPresent — render CEF overlays ──────────────────────────────────────────

void DX9Hook::OnPresent(IDirect3DDevice9* pDevice)
{
    if (!s_cefStarted)
    {
        s_cefStarted = true;
        // Initialize CEF on GTA SA's render/main thread (has proper message loop context)
        NUILog("First Present: initializing CEF on render thread...");
        CefManager::Init(g_hModule);
        NUILog("CEF init done; showing connecting screen");
        CefManager::ShowConnecting(GetModuleDir(g_hModule) + "\\nui-local");
        NUILog("ShowConnecting done");
    }
    CefManager::RenderAll(pDevice);
}

// ── OnReset — release GPU resources before device reset ─────────────────────

void DX9Hook::OnReset(IDirect3DDevice9* pDevice)
{
    CefManager::OnDeviceLost();
}
