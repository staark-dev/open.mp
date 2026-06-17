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
#include "include/cef_command_line.h"
#include "include/wrapper/cef_helpers.h"

#include "cef_manager.hpp"

// ── NUICefApp — browser process handler that disables GPU ────────────────────

class NUICefApp : public CefApp, public CefBrowserProcessHandler
{
public:
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override
    {
        return this;
    }

    void OnBeforeCommandLineProcessing(const CefString& process_type,
                                        CefRefPtr<CefCommandLine> cmd) override
    {
        if (process_type.empty()) // browser process only
        {
            // Disable GPU process — OSR uses software renderer (OnPaint) anyway
            cmd->AppendSwitch("disable-gpu");
            cmd->AppendSwitch("disable-gpu-compositing");
            cmd->AppendSwitch("disable-software-rasterizer");
            // Don't let CEF fight with SA-MP's crash handler
            cmd->AppendSwitch("disable-crash-reporter");
            // Single-process avoids subprocess launch issues in injection context
            // (comment this out if you see renderer-related bugs later)
            cmd->AppendSwitch("no-sandbox");
        }
    }

    IMPLEMENT_REFCOUNTING(NUICefApp);
};

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
                                       D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture, nullptr)))
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

// Declared in dllmain.cpp — available throughout the DLL
extern void NUILog(const char*);

void CefManager::Init(HMODULE hModule)
{
    // chrome_elf must be initialised before libcef; load it explicitly
    // in case the DLL loader didn't guarantee ordering
    NUILog("Loading chrome_elf.dll...");
    LoadLibraryA("chrome_elf.dll");
    NUILog("chrome_elf.dll loaded");

    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(hModule, exePath, MAX_PATH);
    PathRemoveFileSpecA(exePath);
    std::string dir(exePath);
    NUILog(("CEF base dir: " + dir).c_str());

    // Use bootstrap.exe if present (custom CEF distributions ship it as the subprocess)
    // otherwise fall back to our own helper
    std::string helperPath    = dir + "\\bootstrap.exe";
    if (GetFileAttributesA(helperPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        helperPath = dir + "\\omp-nui-helper.exe";
    std::string resourcesPath = dir + "\\Resources";
    std::string logPath       = dir + "\\omp-nui-cef.log";
    NUILog(("helper:    " + helperPath).c_str());
    NUILog(("resources: " + resourcesPath).c_str());

    NUILog("Building CefMainArgs...");
    CefMainArgs args(GetModuleHandleA(nullptr));

    NUILog("Building CefSettings...");
    CefSettings settings;
    // CEF 149 Chrome runtime does NOT support multi_threaded_message_loop —
    // it triggers a fatal CHECK (STATUS_BREAKPOINT) in CefInitialize.
    // Instead we pump the loop manually via CefDoMessageLoopWork() each frame.
    settings.multi_threaded_message_loop  = false;
    settings.external_message_pump        = false;
    settings.windowless_rendering_enabled = true;
    settings.no_sandbox                   = true;
    CefString(&settings.browser_subprocess_path) = helperPath;
    CefString(&settings.resources_dir_path)      = resourcesPath;
    CefString(&settings.locales_dir_path)        = resourcesPath + "\\locales";
    CefString(&settings.log_file)                = logPath;
    settings.log_severity = LOGSEVERITY_VERBOSE;

    // Apply critical switches to the global command line BEFORE CefInitialize reads it.
    // OnBeforeCommandLineProcessing fires too late for crash reporter / sandbox init.
    NUILog("Patching global command line before CefInitialize...");
    CefRefPtr<CefCommandLine> cmdLine = CefCommandLine::GetGlobalCommandLine();
    if (cmdLine)
    {
        cmdLine->AppendSwitch("disable-crash-reporter");
        cmdLine->AppendSwitch("disable-gpu");
        cmdLine->AppendSwitch("disable-gpu-compositing");
        cmdLine->AppendSwitch("no-sandbox");
        cmdLine->AppendSwitch("in-process-gpu");   // avoid separate GPU process
        NUILog("Command line patched");
    }
    else
    {
        NUILog("WARNING: GetGlobalCommandLine returned null");
    }

    NUILog("Calling CefInitialize...");
    CefRefPtr<NUICefApp> app = new NUICefApp();
    bool ok = CefInitialize(args, settings, app, nullptr);
    NUILog(ok ? "CefInitialize returned TRUE" : "CefInitialize returned FALSE");
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
    wi.runtime_style = CEF_RUNTIME_STYLE_ALLOY;  // windowless requires Alloy in CEF 149

    CefBrowserSettings bs;
    bs.windowless_frame_rate = 60;

    // URL: {baseUrl}/nui/{resource}/index.html
    std::string url = baseUrl + "/nui/" + resource + "/index.html";

    // Attach token in URL query so the server can inject window.__NUI_TOKEN__
    // (CEF 149 removed CefRequest overload of CreateBrowserSync)
    std::string urlWithToken = url + "?nui_token=" + token;

    nb->browser = CefBrowserHost::CreateBrowserSync(wi, nb->client, urlWithToken, bs, nullptr, nullptr);
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
    // Pump CEF's message loop on the game's render thread (single-threaded mode).
    // Required because multi_threaded_message_loop is disabled for Chrome runtime.
    CefDoMessageLoopWork();

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

// ── Local connecting screen (file:// — no server needed) ─────────────────────

static const std::string CONNECTING_RESOURCE = "__connecting__";

void CefManager::ShowConnecting(const std::string& localResourcesDir)
{
    // Build a file:// URL to the local connecting.html
    std::string url = "file:///" + localResourcesDir + "/connecting.html";
    // Normalize backslashes to forward slashes for CEF
    for (char& c : url) if (c == '\\') c = '/';

    std::lock_guard<std::mutex> lock(g_browserMutex);

    auto nb = std::make_shared<NUIBrowser>();
    nb->resource = CONNECTING_RESOURCE;
    nb->client   = new NUIClient(nb.get());

    CefWindowInfo wi;
    wi.SetAsWindowless(nullptr);
    wi.runtime_style = CEF_RUNTIME_STYLE_ALLOY;  // windowless requires Alloy in CEF 149

    CefBrowserSettings bs;
    bs.windowless_frame_rate = 30;  // 30fps e suficient pentru un loading screen

    nb->browser = CefBrowserHost::CreateBrowserSync(wi, nb->client, url, bs, nullptr, nullptr);
    g_browsers[CONNECTING_RESOURCE] = nb;
}

void CefManager::HideConnecting()
{
    HideNUI(CONNECTING_RESOURCE);
}
