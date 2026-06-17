#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>

namespace DX9Hook
{
    // Hooks IDirect3DDevice9::Present and ::Reset via vtable patching.
    // Must be called after GTA SA has loaded D3D9 (use Sleep delay in DllMain thread).
    void Install();

    // Called from hooked Present — renders all visible CEF browsers
    void OnPresent(IDirect3DDevice9* pDevice);

    // Called from hooked Reset — releases GPU-side CEF textures before reset
    void OnReset(IDirect3DDevice9* pDevice);
}
