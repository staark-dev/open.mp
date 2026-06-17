#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "dx9_hook.hpp"
#include "cef_manager.hpp"

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
    // Create a tiny dummy window + dummy D3D9 device just to read the vtable.
    // Vtables are per-class (shared), so patching the dummy patches the real device too.
    WNDCLASSA wc = {};
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpszClassName = "omp_nui_dummy";
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(0, "omp_nui_dummy", nullptr, WS_POPUP,
                                0, 0, 2, 2, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return;

    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { DestroyWindow(hwnd); return; }

    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed         = TRUE;
    pp.SwapEffect       = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.hDeviceWindow    = hwnd;

    IDirect3DDevice9* dummy = nullptr;
    if (SUCCEEDED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                     D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dummy)))
    {
        void** vtable = *reinterpret_cast<void***>(dummy);
        PatchVtable(vtable, VTX_PRESENT, (void*)hkPresent, (void**)&oPresent);
        PatchVtable(vtable, VTX_RESET,   (void*)hkReset,   (void**)&oReset);
        dummy->Release();
    }

    d3d->Release();
    DestroyWindow(hwnd);
    UnregisterClassA("omp_nui_dummy", wc.hInstance);
}

// ── OnPresent — render CEF overlays ──────────────────────────────────────────

void DX9Hook::OnPresent(IDirect3DDevice9* pDevice)
{
    CefManager::RenderAll(pDevice);
}

// ── OnReset — release GPU resources before device reset ─────────────────────

void DX9Hook::OnReset(IDirect3DDevice9* pDevice)
{
    CefManager::OnDeviceLost();
}
