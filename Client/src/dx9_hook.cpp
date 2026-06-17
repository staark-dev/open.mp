#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <string>
#include <atomic>
#include "dx9_hook.hpp"
#include "cef_manager.hpp"

extern HMODULE g_hModule;
extern void    NUILog(const char*);

// IDirect3DDevice9 vtable indices
static constexpr int VTX_RESET          = 16;
static constexpr int VTX_PRESENT        = 17;
// IDirect3D9 vtable index
static constexpr int VTX_CREATE_DEVICE  = 16;

using Reset_t         = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using Present_t       = HRESULT(WINAPI*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using CreateDevice_t  = HRESULT(WINAPI*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                          D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);

static Reset_t        oReset        = nullptr;
static Present_t      oPresent      = nullptr;
static CreateDevice_t oCreateDevice = nullptr;

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

static std::atomic<bool> s_cefStarted { false };

static std::string GetModuleDir(HMODULE hMod)
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(hMod, path, MAX_PATH);
    std::string s(path);
    auto pos = s.rfind('\\');
    return (pos != std::string::npos) ? s.substr(0, pos) : s;
}

// Isolated so __try doesn't mix with C++ objects in hkPresent
static DWORD TryCefInit(HMODULE hMod)
{
    __try { CefManager::Init(hMod); return 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode(); }
}

static HRESULT WINAPI hkPresent(IDirect3DDevice9* pDevice,
    const RECT* pSrc, const RECT* pDst, HWND hWnd, const RGNDATA* pDirty)
{
    bool expected = false;
    if (s_cefStarted.compare_exchange_strong(expected, true))
    {
        NUILog("First Present: initializing CEF on render thread...");
        DWORD ex = TryCefInit(g_hModule);
        if (ex != 0)
        {
            char buf[64];
            sprintf_s(buf, sizeof(buf), "CefInitialize CRASHED: 0x%08X", ex);
            NUILog(buf);
        }
        else
        {
            NUILog("CEF init OK — showing connecting screen");
            CefManager::ShowConnecting(GetModuleDir(g_hModule) + "\\nui-local");
            NUILog("ShowConnecting done");
        }
    }
    DX9Hook::OnPresent(pDevice);
    return oPresent(pDevice, pSrc, pDst, hWnd, pDirty);
}

// ── Hooked Reset ─────────────────────────────────────────────────────────────

static HRESULT WINAPI hkReset(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pp)
{
    DX9Hook::OnReset(pDevice);
    return oReset(pDevice, pp);
}

// ── Hook device vtable once we have a real device ────────────────────────────

static std::atomic<bool> s_deviceHooked { false };

static void HookDevice(IDirect3DDevice9* pDevice)
{
    bool expected = false;
    if (!s_deviceHooked.compare_exchange_strong(expected, true)) return;

    void** vtable = *reinterpret_cast<void***>(pDevice);
    char buf[128];
    sprintf_s(buf, sizeof(buf), "Hooking device vtable=%p Present[17]=%p Reset[16]=%p",
              vtable, vtable[VTX_PRESENT], vtable[VTX_RESET]);
    NUILog(buf);
    PatchVtable(vtable, VTX_PRESENT, (void*)hkPresent, (void**)&oPresent);
    PatchVtable(vtable, VTX_RESET,   (void*)hkReset,   (void**)&oReset);
    NUILog("Device vtable patched");
}

// ── Hooked IDirect3D9::CreateDevice ──────────────────────────────────────────

static HRESULT WINAPI hkCreateDevice(
    IDirect3D9* pD3D, UINT Adapter, D3DDEVTYPE DeviceType,
    HWND hFocusWindow, DWORD BehaviorFlags,
    D3DPRESENT_PARAMETERS* pPP, IDirect3DDevice9** ppDevice)
{
    NUILog("hkCreateDevice called");
    HRESULT hr = oCreateDevice(pD3D, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPP, ppDevice);
    NUILog(SUCCEEDED(hr) ? "CreateDevice OK" : "CreateDevice failed (pass-through)");
    if (SUCCEEDED(hr) && ppDevice && *ppDevice)
        HookDevice(*ppDevice);
    return hr;
}

// ── Install ───────────────────────────────────────────────────────────────────

void DX9Hook::Install()
{
    NUILog("DX9Hook::Install - hooking IDirect3D9::CreateDevice");

    // Create a temporary IDirect3D9 only to access its vtable.
    // We do NOT create a device here (would fail in exclusive fullscreen).
    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D)
    {
        NUILog("Direct3DCreate9 returned null");
        return;
    }

    void** vtable = *reinterpret_cast<void***>(pD3D);
    char buf[128];
    sprintf_s(buf, sizeof(buf), "IDirect3D9 vtable=%p  CreateDevice[16]=%p",
              vtable, vtable[VTX_CREATE_DEVICE]);
    NUILog(buf);

    PatchVtable(vtable, VTX_CREATE_DEVICE, (void*)hkCreateDevice, (void**)&oCreateDevice);
    pD3D->Release();
    NUILog("IDirect3D9::CreateDevice hooked — waiting for game device");
}

// ── OnPresent / OnReset ───────────────────────────────────────────────────────

void DX9Hook::OnPresent(IDirect3DDevice9* pDevice)
{
    CefManager::RenderAll(pDevice);
}

void DX9Hook::OnReset(IDirect3DDevice9* pDevice)
{
    CefManager::OnDeviceLost();
}
