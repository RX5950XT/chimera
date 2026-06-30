// Probe ANGLE EGL → D3D11 device; publish 1920×1080 render target as a
// named D3D11 shared texture consumable by Chimera's SharedD3D11TextureCapture.
// Called from chimera_on_post (android_onPost wrapper) in gfxstream_proxy.c.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstring>
#include <stdint.h>
#include <atomic>

extern "C" void chimera_gfxstream_proxy_log(const char* line);

// ---------- EGL types / constants (no EGL headers needed) ----------
typedef void*     EGLDisplay;
typedef void*     EGLDeviceEXT;
typedef int       EGLBoolean;
typedef int       EGLint;
typedef uintptr_t EGLAttrib;

#define EGL_DEVICE_EXT          0x322C
#define EGL_D3D11_DEVICE_ANGLE  0x33A1

typedef EGLDisplay (__cdecl *pfn_eglGetCurrentDisplay)(void);
typedef EGLDisplay (__cdecl *pfn_eglGetDisplay)(void* nativeDisplay);
typedef EGLBoolean (__cdecl *pfn_eglQueryDisplayAttribEXT)(EGLDisplay, EGLint, EGLAttrib*);
typedef EGLBoolean (__cdecl *pfn_eglQueryDeviceAttribEXT)(EGLDeviceEXT, EGLint, EGLAttrib*);

#define EGL_DEFAULT_DISPLAY ((void*)0)

// ---------- SharedD3D11TextureHeader mirror (must match SharedMemoryFrameAbi.h) ----------
// magic=0x43485458 ("CHTX"), version=1, size=560
#pragma pack(push, 1)
struct ProxyD3D11Header {
    uint32_t magic;
    uint32_t version;
    uint32_t headerSize;
    uint32_t width;
    uint32_t height;
    uint32_t dxgiFormat;
    uint32_t flags;
    uint32_t reserved;
    uint64_t sequence;      // seqlock: odd=writing, even=done, 0=no frame
    char16_t textureName[260];
};
#pragma pack(pop)
static_assert(sizeof(ProxyD3D11Header) == 560, "ProxyD3D11Header must be 560 bytes");

// ---------- Global state ----------
// 0=uninit, 1=init-in-progress, 2=ready, -1=permanently-failed
static std::atomic<int>     g_state{0};
static ID3D11Device*        g_device    = nullptr;
static ID3D11DeviceContext* g_ctx       = nullptr;
static ID3D11Texture2D*     g_shared_tex = nullptr;
static HANDLE               g_sh_handle = nullptr;  // NT shared handle (kept alive)
static HANDLE               g_mapping   = nullptr;
static ProxyD3D11Header*    g_header    = nullptr;
static HANDLE               g_event     = nullptr;
static std::atomic<int64_t> g_seq{0};

// ---------- EGL resolution (once per process) ----------
static bool s_egl_resolved = false;
static pfn_eglGetCurrentDisplay     s_eglGetCurrentDisplay     = nullptr;
static pfn_eglGetDisplay            s_eglGetDisplay            = nullptr;
static pfn_eglQueryDisplayAttribEXT s_eglQueryDisplayAttribEXT = nullptr;
static pfn_eglQueryDeviceAttribEXT  s_eglQueryDeviceAttribEXT  = nullptr;

static bool resolve_angle_egl()
{
    if (s_egl_resolved) return s_eglGetCurrentDisplay != nullptr;
    // NOTE: s_egl_resolved is only set on SUCCESS so the caller can retry when
    // libEGL.dll has not been loaded yet (stock loads it lazily after first GL use).

    HMODULE mod = GetModuleHandleA("libEGL.dll");
    if (!mod) {
        // Rate-limit: log first miss, then every 100 misses (~2s intervals at 20ms sleep)
        static std::atomic<int> miss_count{0};
        const int n = miss_count.fetch_add(1, std::memory_order_relaxed);
        if (n == 0 || (n % 100) == 0) {
            HMODULE vk = GetModuleHandleA("vulkan-1.dll");
            char line[128];
            snprintf(line, sizeof(line),
                     "angle_egl libEGL=not_loaded miss=%d vulkan-1=%s\n",
                     n, vk ? "loaded" : "not_loaded");
            chimera_gfxstream_proxy_log(line);
        }
        return false;  // do NOT set s_egl_resolved — allow retry next call
    }
    s_egl_resolved = true;
    s_eglGetCurrentDisplay     = (pfn_eglGetCurrentDisplay)GetProcAddress(mod, "eglGetCurrentDisplay");
    s_eglGetDisplay            = (pfn_eglGetDisplay)GetProcAddress(mod, "eglGetDisplay");
    s_eglQueryDisplayAttribEXT = (pfn_eglQueryDisplayAttribEXT)GetProcAddress(mod, "eglQueryDisplayAttribEXT");
    s_eglQueryDeviceAttribEXT  = (pfn_eglQueryDeviceAttribEXT)GetProcAddress(mod, "eglQueryDeviceAttribEXT");

    char line[160];
    snprintf(line, sizeof(line), "angle_egl current=%p default=%p queryDisp=%p queryDev=%p\n",
             (void*)s_eglGetCurrentDisplay,
             (void*)s_eglGetDisplay,
             (void*)s_eglQueryDisplayAttribEXT,
             (void*)s_eglQueryDeviceAttribEXT);
    chimera_gfxstream_proxy_log(line);
    return s_eglGetCurrentDisplay != nullptr;
}

static ID3D11Device* get_angle_device()
{
    if (!resolve_angle_egl()) return nullptr;

    // Try context-bound display first (render thread path).
    // Fall back to EGL_DEFAULT_DISPLAY so background/gRPC threads can also probe
    // the D3D11 device — eglQueryDisplayAttribEXT does not require a current context.
    EGLDisplay display = s_eglGetCurrentDisplay ? s_eglGetCurrentDisplay() : nullptr;
    const bool fromCurrent = (display != nullptr);
    if (!display && s_eglGetDisplay) {
        display = s_eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }
    if (!display) {
        // EGL not loaded yet; caller should retry later — do NOT permanently fail.
        static std::atomic<int> logged{0};
        if (logged.fetch_add(1, std::memory_order_relaxed) == 0)
            chimera_gfxstream_proxy_log("angle_device display=null (EGL not ready)\n");
        return nullptr;
    }

    char path[64];
    snprintf(path, sizeof(path), "angle_device path=%s display=%p\n",
             fromCurrent ? "current" : "default", display);
    static std::atomic<int> pathLogged{0};
    if (pathLogged.fetch_add(1, std::memory_order_relaxed) == 0)
        chimera_gfxstream_proxy_log(path);

    EGLAttrib deviceAttr = 0;
    if (!s_eglQueryDisplayAttribEXT ||
        !s_eglQueryDisplayAttribEXT(display, EGL_DEVICE_EXT, &deviceAttr) || !deviceAttr) {
        static std::atomic<int> dlogged{0};
        if (dlogged.fetch_add(1, std::memory_order_relaxed) == 0)
            chimera_gfxstream_proxy_log("angle_device queryDevice=failed\n");
        return nullptr;
    }

    EGLAttrib d3dAttr = 0;
    if (!s_eglQueryDeviceAttribEXT ||
        !s_eglQueryDeviceAttribEXT((EGLDeviceEXT)(uintptr_t)deviceAttr, EGL_D3D11_DEVICE_ANGLE, &d3dAttr) ||
        !d3dAttr) {
        static std::atomic<int> d3dlogged{0};
        if (d3dlogged.fetch_add(1, std::memory_order_relaxed) == 0)
            chimera_gfxstream_proxy_log("angle_device queryD3D11=failed\n");
        return nullptr;
    }

    return reinterpret_cast<ID3D11Device*>((uintptr_t)d3dAttr);
}

// ---------- One-time initialisation ----------
static bool do_init(ID3D11Device* device, DXGI_FORMAT rtFormat)
{
    char metaEnv[256] = {}, texEnv[256] = {}, evtEnv[256] = {};
    GetEnvironmentVariableA("CHIMERA_D3D11_TEXTURE_METADATA", metaEnv, sizeof(metaEnv));
    GetEnvironmentVariableA("CHIMERA_D3D11_TEXTURE_NAME",     texEnv,  sizeof(texEnv));
    GetEnvironmentVariableA("CHIMERA_D3D11_TEXTURE_EVENT",    evtEnv,  sizeof(evtEnv));

    if (!metaEnv[0] || !texEnv[0]) {
        chimera_gfxstream_proxy_log("angle_d3d11_init env_names=missing (set CHIMERA_D3D11_TEXTURE_METADATA + CHIMERA_D3D11_TEXTURE_NAME)\n");
        return false;
    }

    // Hold a reference to device + context
    device->AddRef();
    g_device = device;
    device->GetImmediateContext(&g_ctx);

    // Create named shared texture (same format as RT, NTHANDLE for OpenSharedResourceByName)
    // D3D11_RESOURCE_MISC_SHARED is required alongside NTHANDLE; NTHANDLE alone causes
    // CreateTexture2D to fail or hang on some drivers (E_INVALIDARG at minimum).
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width          = 1920;
    desc.Height         = 1080;
    desc.MipLevels      = 1;
    desc.ArraySize      = 1;
    desc.Format         = rtFormat;
    desc.SampleDesc.Count = 1;
    desc.Usage          = D3D11_USAGE_DEFAULT;
    desc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags      = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &g_shared_tex);
    if (FAILED(hr)) {
        char line[128];
        snprintf(line, sizeof(line), "angle_d3d11_init createTex=FAILED hr=0x%lx\n", (unsigned long)hr);
        chimera_gfxstream_proxy_log(line);
        return false;
    }

    // Create NT named shared handle so consumer can call OpenSharedResourceByName
    IDXGIResource1* dxgiRes = nullptr;
    hr = g_shared_tex->QueryInterface(__uuidof(IDXGIResource1), (void**)&dxgiRes);
    if (FAILED(hr) || !dxgiRes) {
        chimera_gfxstream_proxy_log("angle_d3d11_init IDXGIResource1=unavail (need D3D11.1)\n");
        g_shared_tex->Release(); g_shared_tex = nullptr;
        return false;
    }
    wchar_t wTexName[256] = {};
    MultiByteToWideChar(CP_ACP, 0, texEnv, -1, wTexName, 256);
    // GENERIC_ALL makes the consumer's OpenSharedResourceByName fail with
    // E_INVALIDARG; the DXGI keyed-mutex/shared-resource access flags are the
    // only valid ones for D3D11 NT shared handles (matches the production bridge).
    hr = dxgiRes->CreateSharedHandle(nullptr,
                                     DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                     wTexName, &g_sh_handle);
    dxgiRes->Release();
    if (FAILED(hr)) {
        char line[128];
        snprintf(line, sizeof(line), "angle_d3d11_init CreateSharedHandle=FAILED hr=0x%lx\n", (unsigned long)hr);
        chimera_gfxstream_proxy_log(line);
        g_shared_tex->Release(); g_shared_tex = nullptr;
        return false;
    }
    // Do NOT close g_sh_handle — keeping it open keeps the named object alive.

    // Create named file mapping for SharedD3D11TextureHeader
    wchar_t wMetaName[256] = {};
    MultiByteToWideChar(CP_ACP, 0, metaEnv, -1, wMetaName, 256);
    g_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                   0, (DWORD)sizeof(ProxyD3D11Header), wMetaName);
    if (!g_mapping) {
        chimera_gfxstream_proxy_log("angle_d3d11_init CreateFileMapping=FAILED\n");
        return false;
    }
    g_header = static_cast<ProxyD3D11Header*>(
        MapViewOfFile(g_mapping, FILE_MAP_WRITE, 0, 0, sizeof(ProxyD3D11Header)));
    if (!g_header) {
        chimera_gfxstream_proxy_log("angle_d3d11_init MapViewOfFile=FAILED\n");
        return false;
    }

    // Create or open frame-ready event
    if (evtEnv[0]) {
        wchar_t wEvtName[256] = {};
        MultiByteToWideChar(CP_ACP, 0, evtEnv, -1, wEvtName, 256);
        g_event = CreateEventW(nullptr, FALSE, FALSE, wEvtName);
    }

    // Write static header fields; sequence stays 0 until first GPU copy
    memset(g_header, 0, sizeof(ProxyD3D11Header));
    g_header->magic      = 0x43485458u;  // CHTX
    g_header->version    = 1;
    g_header->headerSize = (uint32_t)sizeof(ProxyD3D11Header);
    g_header->width      = 1920;
    g_header->height     = 1080;
    g_header->dxgiFormat = (uint32_t)rtFormat;
    g_header->flags      = 0;
    for (int i = 0; i < 259 && wTexName[i]; ++i)
        g_header->textureName[i] = (char16_t)wTexName[i];

    char line[288];
    snprintf(line, sizeof(line),
             "angle_d3d11_init ok meta=%s tex=%s evt=%s fmt=%u\n",
             metaEnv, texEnv, evtEnv[0] ? evtEnv : "(none)", (unsigned)rtFormat);
    chimera_gfxstream_proxy_log(line);
    return true;
}

// ---------- Probe current render target ----------
// Returns a live ID3D11Texture2D* (caller must Release) or nullptr.
static ID3D11Texture2D* get_current_rt_1080p(ID3D11DeviceContext* ctx)
{
    ID3D11RenderTargetView* rtv = nullptr;
    ctx->OMGetRenderTargets(1, &rtv, nullptr);
    if (!rtv) return nullptr;

    ID3D11Resource* res = nullptr;
    rtv->GetResource(&res);
    rtv->Release();
    if (!res) return nullptr;

    ID3D11Texture2D* tex = nullptr;
    res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
    res->Release();
    if (!tex) return nullptr;

    D3D11_TEXTURE2D_DESC d = {};
    tex->GetDesc(&d);
    if (d.Width != 1920 || d.Height != 1080) {
        tex->Release();
        return nullptr;
    }
    return tex;
}

// ---------- Public entry point (called from gfxstream_proxy.c per frame) ----------
extern "C" void chimera_proxy_try_publish_d3d11_frame(int width, int height)
{
    if (width != 1920 || height != 1080) return;

    const int cur = g_state.load(std::memory_order_acquire);
    if (cur == -1) return;

    // Log once when first called so we know which thread (render vs gRPC IO)
    static std::atomic<int> entry_logged{0};
    if (entry_logged.fetch_add(1, std::memory_order_relaxed) == 0) {
        char line[128];
        snprintf(line, sizeof(line), "angle_d3d11 first_call tid=%lu state=%d\n",
                 (unsigned long)GetCurrentThreadId(), cur);
        chimera_gfxstream_proxy_log(line);
    }

    ID3D11Device* device = get_angle_device();
    if (!device) {
        // EGL display or D3D11 device not yet reachable — skip this call.
        // Do NOT permanently fail: the render-thread path (via setPostCallback) will
        // retry on the next frame where eglGetCurrentDisplay() returns a valid display.
        return;
    }

    // ---- Phase 1: probe ----
    if (cur == 0) {
        int expected = 0;
        if (!g_state.compare_exchange_strong(expected, 1, std::memory_order_acq_rel))
            return; // another invocation racing; skip this frame

        ID3D11DeviceContext* ctx = nullptr;
        device->GetImmediateContext(&ctx);

        ID3D11Texture2D* rtTex = get_current_rt_1080p(ctx);
        ctx->Release();

        if (!rtTex) {
            chimera_gfxstream_proxy_log("angle_d3d11_probe rt=null_or_wrong_size (retry next frame)\n");
            g_state.store(0, std::memory_order_release);
            return;
        }

        D3D11_TEXTURE2D_DESC rtDesc = {};
        rtTex->GetDesc(&rtDesc);
        rtTex->Release();

        char line[192];
        snprintf(line, sizeof(line),
                 "angle_d3d11_probe rt=1920x1080 fmt=%u bind=0x%x misc=0x%x\n",
                 (unsigned)rtDesc.Format, rtDesc.BindFlags, rtDesc.MiscFlags);
        chimera_gfxstream_proxy_log(line);

        if (!do_init(device, rtDesc.Format)) {
            g_state.store(-1, std::memory_order_release);
            return;
        }
        g_state.store(2, std::memory_order_release);
        // fall through to publish immediately
    }

    // ---- Phase 2: publish ----
    if (g_state.load(std::memory_order_acquire) != 2) return;
    if (!g_ctx || !g_shared_tex || !g_header) return;

    ID3D11Texture2D* rtTex = get_current_rt_1080p(g_ctx);
    if (!rtTex) {
        // RT is unbound this frame — skip without logging to avoid flood
        return;
    }

    // GPU copy render target → named shared texture
    g_ctx->CopyResource(g_shared_tex, rtTex);
    rtTex->Release();
    // Flush ensures the copy is submitted to the GPU before the seqlock update,
    // so consumers don't read stale data from the shared texture.
    g_ctx->Flush();

    // Seqlock publish (odd = writing, even = complete)
    const int64_t seq = g_seq.fetch_add(1, std::memory_order_relaxed) + 1;
    MemoryBarrier();
    g_header->sequence = (uint64_t)(seq * 2 - 1);  // odd
    MemoryBarrier();
    g_header->sequence = (uint64_t)(seq * 2);       // even
    MemoryBarrier();

    if (g_event) SetEvent(g_event);

    if (seq <= 5 || (seq % 120) == 0) {
        char line[128];
        snprintf(line, sizeof(line),
                 "gpu_display_signal 1920x1080 via_angle_d3d11 seq=%lld\n",
                 (long long)seq);
        chimera_gfxstream_proxy_log(line);
    }
}

// ---- Background D3D11 polling thread ------------------------------------
// Started from android_setOpenglesRenderer. Waits until ANGLE loads (lazy-init
// after first gRPC screenshot), enables ID3D11Multithread protection for safe
// concurrent immediate-context access, then polls the render target at ~60 Hz.

static std::atomic<bool> g_bg_thread_started{false};
static ID3D11Multithread* g_d3d11_mt = nullptr;

static void dump_gpu_modules(const char* tag)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                           GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return;
    MODULEENTRY32 me;
    me.dwSize = sizeof(me);
    bool found_any = false;
    for (BOOL ok = Module32First(snap, &me); ok; ok = Module32Next(snap, &me)) {
        const char* n = me.szModule;
        if (_stricmp(n, "vulkan-1.dll") == 0   ||
            _stricmp(n, "libEGL.dll") == 0      ||
            _stricmp(n, "libGLESv2.dll") == 0   ||
            _stricmp(n, "d3d11.dll") == 0        ||
            _stricmp(n, "d3d12.dll") == 0        ||
            _stricmp(n, "d3d9.dll") == 0         ||
            strstr(n, "vk_") != NULL             ||
            strstr(n, "angle") != NULL           ||
            strstr(n, "swift") != NULL           ||
            strstr(n, "gfxstream") != NULL) {
            char line[300];
            snprintf(line, sizeof(line), "gpu_module[%s]: %s base=%p\n",
                     tag, n, (void*)me.modBaseAddr);
            chimera_gfxstream_proxy_log(line);
            found_any = true;
        }
    }
    CloseHandle(snap);
    if (!found_any) {
        char line[128];
        snprintf(line, sizeof(line), "gpu_module[%s]: none found\n", tag);
        chimera_gfxstream_proxy_log(line);
    }
}

static DWORD WINAPI d3d11_bg_poll_thread(LPVOID)
{
    chimera_gfxstream_proxy_log("d3d11_bg_thread: polling for ANGLE/D3D11 device\n");

    // Wait up to 60s for libEGL.dll (ANGLE loads lazily); dump GPU modules at
    // 5s mark regardless so we can identify the actual headless rendering backend.
    ID3D11Device* device = nullptr;
    for (int i = 0; i < 3000; ++i) {  // 3000 * 20ms = 60s
        device = get_angle_device();
        if (device) break;
        if (i == 250) {  // 5s mark — dump while emulator is running
            chimera_gfxstream_proxy_log("d3d11_bg_thread: 5s mark — dumping GPU modules\n");
            dump_gpu_modules("5s");
        }
        Sleep(20);
    }
    if (!device) {
        chimera_gfxstream_proxy_log("d3d11_bg_thread: device never available — final GPU module dump\n");
        dump_gpu_modules("60s");
        chimera_gfxstream_proxy_log("d3d11_bg_thread: exiting (no ANGLE device)\n");
        return 0;
    }
    {
        char line[128];
        snprintf(line, sizeof(line), "d3d11_bg_thread: device=%p enabling MT protection\n",
                 (void*)device);
        chimera_gfxstream_proxy_log(line);
    }

    // ID3D11Multithread serialises our background calls with gfxstream's render thread
    // so OMGetRenderTargets and CopyResource are safe from this thread.
    ID3D11Multithread* mt = nullptr;
    HRESULT hr = device->QueryInterface(__uuidof(ID3D11Multithread), (void**)&mt);
    if (FAILED(hr) || !mt) {
        chimera_gfxstream_proxy_log("d3d11_bg_thread: ID3D11Multithread unavailable\n");
        return 0;
    }
    const BOOL wasOn = mt->SetMultithreadProtected(TRUE);
    {
        char line[128];
        snprintf(line, sizeof(line),
                 "d3d11_bg_thread: MT on (was=%s), starting 60Hz poll\n",
                 wasOn ? "on" : "off");
        chimera_gfxstream_proxy_log(line);
    }
    g_d3d11_mt = mt;

    for (;;) {
        Sleep(16);
        mt->Enter();
        chimera_proxy_try_publish_d3d11_frame(1920, 1080);
        mt->Leave();
    }
    return 0;
}

extern "C" void chimera_proxy_start_d3d11_bg_thread(void)
{
    bool expected = false;
    if (!g_bg_thread_started.compare_exchange_strong(expected, true,
            std::memory_order_acquire, std::memory_order_relaxed)) {
        return;
    }
    HANDLE ht = CreateThread(NULL, 0, d3d11_bg_poll_thread, NULL, 0, NULL);
    if (ht) {
        chimera_gfxstream_proxy_log("d3d11_bg_thread: started\n");
        CloseHandle(ht);
    } else {
        chimera_gfxstream_proxy_log("d3d11_bg_thread: CreateThread FAILED\n");
        g_bg_thread_started.store(false, std::memory_order_relaxed);
    }
}
