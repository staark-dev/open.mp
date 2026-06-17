#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>

#include "include/cef_client.h"
#include "include/cef_render_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_browser.h"

// ── Per-browser CEF + D3D9 state ─────────────────────────────────────────────

class NUIClient;

struct NUIBrowser
{
    std::string              resource;
    CefRefPtr<NUIClient>     client;
    CefRefPtr<CefBrowser>    browser;

    // CPU-side pixel buffer written by CEF OnPaint (BGRA, width*height*4 bytes)
    std::vector<uint8_t>     pixels;
    int                      pixelW = 0;
    int                      pixelH = 0;
    std::mutex               pixelMutex;
    std::atomic<bool>        dirty { false };

    // GPU-side texture (created/updated on game thread in RenderAll)
    IDirect3DTexture9*       texture = nullptr;
    IDirect3DVertexBuffer9*  quad    = nullptr;
    int                      texW = 0;
    int                      texH = 0;

    void ReleaseGPU();
    void UploadToTexture(IDirect3DDevice9* dev);
    void DrawQuad(IDirect3DDevice9* dev);
};

// ── CefManager namespace ─────────────────────────────────────────────────────

namespace CefManager
{
    // Call once from DLL main thread
    void Init(HMODULE hModule);
    void Shutdown();

    // Called from RPC handler (any thread) — thread-safe
    void ShowNUI(const std::string& resource,
                 const std::string& baseUrl,
                 const std::string& token);
    void HideNUI(const std::string& resource);
    void SendMessage(const std::string& resource, const std::string& json);

    // Called from D3D9 Present hook (game thread)
    void RenderAll(IDirect3DDevice9* pDevice);
    void OnDeviceLost();
}
