#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <shlwapi.h>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_render_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/wrapper/cef_helpers.h"

#include "cef_manager.hpp"

// ── Screen dimensions (updated from D3D9 Present) ────────────────────────────

static int g_screenW = 1920;
static int g_screenH = 1080;

// ── Fullscreen textured quad vertex ──────────────────────────────────────────

struct Vertex { float x, y, z, w; DWORD color; float u, v; };
static const DWORD QUAD_FVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

// ── NUIRenderHandler — receives CEF paint callbacks ──────────────────────────

class NUIRenderHandler : public CefRenderHandler
{
public:
    NUIBrowser* owner;
    explicit NUIRenderHandler(NUIBrowser* b) : owner(b) {}

    void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override
    {
        rect = CefRect(0, 0, g_screenW, g_screenH);
    }

    void OnPaint(CefRefPtr<CefBrowser>, PaintElementType,
                 const RectList&, const void* buffer, int w, int h) override
    {
        std::lock_guard<std::mutex> lock(owner->pixelMutex);
        size_t bytes = (size_t)w * h * 4;
        owner->pixels.resize(bytes);
        memcpy(owner->pixels.data(), buffer, bytes);
        owner->pixelW = w;
        owner->pixelH = h;
        owner->dirty  = true;
    }

    IMPLEMENT_REFCOUNTING(NUIRenderHandler);
};

// ── NUIClient — aggregates all CEF handler interfaces ────────────────────────

class NUIClient : public CefClient, public CefLifeSpanHandler
{
public:
    CefRefPtr<NUIRenderHandler> renderHandler;
    explicit NUIClient(NUIBrowser* b) : renderHandler(new NUIRenderHandler(b)) {}

    CefRefPtr<CefRenderHandler>   GetRenderHandler()   override { return renderHandler; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }

    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override
    {
        // Browser is ready — nothing special needed
    }

    IMPLEMENT_REFCOUNTING(NUIClient);
};

// ── NUIBrowser GPU helpers ────────────────────────────────────────────────────

void NUIBrowser::ReleaseGPU()
{
    if (texture) { texture->Release(); texture = nullptr; }
    if (quad)    { quad->Release();    quad    = nullptr; }
    texW = texH = 0;
}

void NUIBrowser::UploadToTexture(IDirect3DDevice9* dev)
{
    std::lock_guard<std::mutex> lock(pixelMutex);
    if (!dirty || pixels.empty()) return;
    dirty = false;

    int w = pixelW, h = pixelH;

    // Recreate texture if size changed
    if (!texture || texW != w || texH != h)
    {
        if (texture) texture->Release();
        if (FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_DYNAMIC,
                                       D3DFMT_A8R8B8G8, D3DPOOL_DEFAULT, &texture, nullptr)))
        {
            texture = nullptr;
            return;
        }
        texW = w;
        texH = h;
        // Recreate fullscreen quad for new dimensions
        if (quad) { quad->Release(); quad = nullptr; }
    }

    // Upload pixel data (CEF gives BGRA = D3DFMT_A8R8B8G8 on little-endian)
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(texture->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD)))
    {
        for (int row = 0; row < h; ++row)
            memcpy((uint8_t*)lr.pBits + row * lr.Pitch,
                   pixels.data() + row * w * 4, w * 4);
        texture->UnlockRect(0);
    }

    // Create quad if needed
    if (!quad)
    {
        float W = (float)g_screenW, H = (float)g_screenH;
        Vertex verts[4] = {
            { 0.f,  0.f,  0.f, 1.f, 0xFFFFFFFF, 0.f, 0.f },
            { W,    0.f,  0.f, 1.f, 0xFFFFFFFF, 1.f, 0.f },
            { 0.f,  H,    0.f, 1.f, 0xFFFFFFFF, 0.f, 1.f },
            { W,    H,    0.f, 1.f, 0xFFFFFFFF, 1.f, 1.f },
        };
        dev->CreateVertexBuffer(sizeof(verts), D3DUSAGE_WRITEONLY, QUAD_FVF,
                                 D3DPOOL_DEFAULT, &quad, nullptr);
        void* data;
        if (quad && SUCCEEDED(quad->Lock(0, sizeof(verts), &data, 0)))
        {
            memcpy(data, verts, sizeof(verts));
            quad->Unlock();
        }
    }
}

void NUIBrowser::DrawQuad(IDirect3DDevice9* dev)
{
    if (!texture || !quad) return;

    // Save render state
    IDirect3DStateBlock9* sb = nullptr;
    dev->CreateStateBlock(D3DSBT_ALL, &sb);

    // Alpha blend for transparent NUI
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_ZENABLE,   FALSE);
    dev->SetRenderState(D3DRS_LIGHTING,  FALSE);

    dev->SetTexture(0, texture);
    dev->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

    dev->SetStreamSource(0, quad, 0, sizeof(Vertex));
    dev->SetFVF(QUAD_FVF);
    dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);

    if (sb) { sb->Apply(); sb->Release(); }
}

// ── Browser registry ─────────────────────────────────────────────────────────

static std::unordered_map<std::string, std::shared_ptr<NUIBrowser>> g_browsers;
static std::mutex g_browserMutex;

// ── CefManager ───────────────────────────────────────────────────────────────

void CefManager::Init(HMODULE hModule)
{
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(hModule, exePath, MAX_PATH);
    PathRemoveFileSpecA(exePath);
    std::string helperPath = std::string(exePath) + "\\omp-nui-helper.exe";

    CefMainArgs args(GetModuleHandleA(nullptr));
    CefSettings settings;
    settings.multi_threaded_message_loop  = true;   // CEF runs its own UI thread
    settings.windowless_rendering_enabled = true;   // OSR mode — no native window
    settings.no_sandbox                   = true;
    CefString(&settings.browser_subprocess_path) = helperPath;

    std::string resourcesPath = std::string(exePath) + "\\Resources";
    CefString(&settings.resources_dir_path)  = resourcesPath;
    CefString(&settings.locales_dir_path)    = resourcesPath + "\\locales";

    // Log to file next to the DLL
    CefString(&settings.log_file) = std::string(exePath) + "\\omp-nui-cef.log";
    settings.log_severity = LOGSEVERITY_WARNING;

    CefInitialize(args, settings, nullptr, nullptr);
}

void CefManager::Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(g_browserMutex);
        for (auto& [name, b] : g_browsers)
        {
            if (b->browser)
                b->browser->GetHost()->CloseBrowser(true);
            b->ReleaseGPU();
        }
        g_browsers.clear();
    }
    CefShutdown();
}

void CefManager::ShowNUI(const std::string& resource,
                          const std::string& baseUrl,
                          const std::string& token)
{
    std::lock_guard<std::mutex> lock(g_browserMutex);

    // Close existing browser for this resource if any
    auto it = g_browsers.find(resource);
    if (it != g_browsers.end())
    {
        if (it->second->browser)
            it->second->browser->GetHost()->CloseBrowser(true);
        g_browsers.erase(it);
    }

    auto nb = std::make_shared<NUIBrowser>();
    nb->resource = resource;
    nb->client   = new NUIClient(nb.get());

    CefWindowInfo wi;
    wi.SetAsWindowless(nullptr);    // OSR — no native window

    CefBrowserSettings bs;
    bs.windowless_frame_rate = 60;

    // URL: {baseUrl}/nui/{resource}/index.html
    std::string url = baseUrl + "/nui/" + resource + "/index.html";

    // Attach token so the HTTP server can inject it into <head>
    // The server reads X-NUI-Token from the initial request header.
    // We set it via a custom request:
    CefRefPtr<CefRequest> req = CefRequest::Create();
    req->SetURL(url);
    CefRequest::HeaderMap headers;
    headers.insert({ "X-NUI-Token", token });
    req->SetHeaderMap(headers);

    nb->browser = CefBrowserHost::CreateBrowserSync(wi, nb->client, req, bs, nullptr, nullptr);
    g_browsers[resource] = nb;
}

void CefManager::HideNUI(const std::string& resource)
{
    std::lock_guard<std::mutex> lock(g_browserMutex);
    auto it = g_browsers.find(resource);
    if (it == g_browsers.end()) return;
    if (it->second->browser)
        it->second->browser->GetHost()->CloseBrowser(true);
    it->second->ReleaseGPU();
    g_browsers.erase(it);
}

void CefManager::SendMessage(const std::string& resource, const std::string& json)
{
    std::lock_guard<std::mutex> lock(g_browserMutex);
    auto it = g_browsers.find(resource);
    if (it == g_browsers.end() || !it->second->browser) return;

    // Dispatch via window.dispatchEvent(new MessageEvent('message', {data: <json>}))
    std::string js = "window.dispatchEvent(new MessageEvent('message',{data:" + json + "}));";
    it->second->browser->GetMainFrame()->ExecuteJavaScript(js, {}, 0);
}

void CefManager::RenderAll(IDirect3DDevice9* pDevice)
{
    // Update screen size from backbuffer
    D3DSURFACE_DESC desc;
    IDirect3DSurface9* bb = nullptr;
    if (SUCCEEDED(pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)))
    {
        bb->GetDesc(&desc);
        g_screenW = (int)desc.Width;
        g_screenH = (int)desc.Height;
        bb->Release();
    }

    std::lock_guard<std::mutex> lock(g_browserMutex);
    for (auto& [name, b] : g_browsers)
    {
        if (!b->browser) continue;
        b->UploadToTexture(pDevice);
        b->DrawQuad(pDevice);
    }
}

void CefManager::OnDeviceLost()
{
    std::lock_guard<std::mutex> lock(g_browserMutex);
    for (auto& [name, b] : g_browsers)
        b->ReleaseGPU();
}
