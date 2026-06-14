#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <memory>
#include <utility>

#include "gfxstream/host/Features.h"
#include "render-utils/RenderLib.h"
#include "render-utils/render_api.h"

extern "C" void chimera_gfxstream_proxy_log(const char* line);
extern "C" FARPROC chimera_gfxstream_resolve_stock_export(const char* name);
extern "C" void chimera_proxy_try_publish_d3d11_frame(int width, int height);

/* chimera_on_post is the non-static frame callback defined in gfxstream_proxy.c.
   Its signature matches the raw-pointer OnPostCallback from SDK 13278158, but in
   SDK 15261927 the ABI changed to std::function<…> (passed by hidden pointer).
   We wrap it here in C++ so the compiler generates the correct x64 hidden-pointer call. */
extern "C" void chimera_on_post(void* context,
                                 uint32_t displayId,
                                 int width,
                                 int height,
                                 int ydir,
                                 int format,
                                 int type,
                                 unsigned char* pixels);

namespace gfxstream::host {

FeatureSet::FeatureSet(const FeatureSet& rhs) : FeatureSet() {
    *this = rhs;
}

FeatureSet& FeatureSet::operator=(const FeatureSet& rhs) {
    for (const auto& [featureName, featureInfo] : rhs.map) {
        auto it = map.find(featureName);
        if (it != map.end() && it->second != nullptr) {
            *it->second = *featureInfo;
        }
        // Skip features unknown to this (older) source — SDK may have newer features
    }
    return *this;
}

} // namespace gfxstream::host

namespace {

void logLine(const char* line) {
    chimera_gfxstream_proxy_log(line);
}

void logSize(const char* prefix, int width, int height) {
    char line[160] = {};
    std::snprintf(line, sizeof(line), "%s size=%dx%d\n", prefix, width, height);
    logLine(line);
}

bool truthyEnv(const char* name) {
    const char* enabled = std::getenv(name);
    return enabled && enabled[0] != '\0' && enabled[0] != '0' &&
           enabled[0] != 'f' && enabled[0] != 'F';
}

std::atomic<long long> g_frameListenerCount{0};
gfxstream::Renderer::FrameBufferChangeEventListener g_frameListener;

void maybeInstallFrameListener(gfxstream::RendererPtr* rendererPtr) {
    if (!rendererPtr || !rendererPtr->get()) {
        return;
    }
    static std::atomic<bool> installed{false};
    bool expected = false;
    if (!installed.compare_exchange_strong(expected, true)) {
        return;
    }
    /* Always install the listener — this is the primary frame notification path
       since vtable hooking (clone or in-place) breaks the emulator's gRPC
       screenshot handler.  The listener fires on the render thread where the
       ANGLE EGL context is current, making D3D11 texture probing possible. */
    g_frameListener = [](const gfxstream::FrameBufferChangeEvent evt) {
        const long long count = ++g_frameListenerCount;
        if (count <= 20 || (count % 120) == 0) {
            char line[192] = {};
            std::snprintf(line, sizeof(line),
                          "renderlib_listener frame count=%lld change=%d frame=%llu\n",
                          count, static_cast<int>(evt.change),
                          static_cast<unsigned long long>(evt.frameNumber));
            logLine(line);
        }
        /* Probe ANGLE D3D11 render target on every frame post (change==1).
           SEH not available in lambdas; chimera_proxy_try_publish_d3d11_frame
           has its own internal state machine and is safe to call redundantly. */
        chimera_proxy_try_publish_d3d11_frame(1920, 1080);
    };
    /* addListener internally accesses the EGL display compositor sub-object.
       In headless (-no-window) mode this object is never created, so the call
       crashes with a NULL-deref.  Wrap with SEH and fall through gracefully;
       the chimera_on_post path (registered via setPostCallback) is the fallback
       frame notification in headless mode. */
    BOOL addListenerOk = FALSE;
    __try {
        (*rendererPtr)->addListener(&g_frameListener);
        addListenerOk = TRUE;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logLine("renderlib_listener install=EXCEPTION (headless EGL sub-object not ready)\n");
    }
    logLine(addListenerOk ? "renderlib_listener install=ok\n"
                          : "renderlib_listener install=skipped (headless)\n");
}

class RendererWrapper final : public gfxstream::Renderer {
public:
    explicit RendererWrapper(gfxstream::RendererPtr inner) : m_inner(std::move(inner)) {}

    gfxstream::RenderChannelPtr createRenderChannel(
            android::base::Stream* loadStream = nullptr,
            uint32_t virtioGpuContextId = static_cast<uint32_t>(-1)) override {
        return m_inner->createRenderChannel(loadStream, virtioGpuContextId);
    }
    void* addressSpaceGraphicsConsumerCreate(
            struct asg_context context,
            android::base::Stream* loadStream,
            android::emulation::asg::ConsumerCallbacks callbacks,
            uint32_t contextId,
            uint32_t capsetId,
            std::optional<std::string> nameOpt) override {
        return m_inner->addressSpaceGraphicsConsumerCreate(
                context, loadStream, callbacks, contextId, capsetId, std::move(nameOpt));
    }
    void addressSpaceGraphicsConsumerDestroy(void* consumer) override {
        m_inner->addressSpaceGraphicsConsumerDestroy(consumer);
    }
    void addressSpaceGraphicsConsumerPreSave(void* consumer) override {
        m_inner->addressSpaceGraphicsConsumerPreSave(consumer);
    }
    void addressSpaceGraphicsConsumerSave(void* consumer, android::base::Stream* stream) override {
        m_inner->addressSpaceGraphicsConsumerSave(consumer, stream);
    }
    void addressSpaceGraphicsConsumerPostSave(void* consumer) override {
        m_inner->addressSpaceGraphicsConsumerPostSave(consumer);
    }
    void addressSpaceGraphicsConsumerRegisterPostLoadRenderThread(void* consumer) override {
        m_inner->addressSpaceGraphicsConsumerRegisterPostLoadRenderThread(consumer);
    }
    HardwareStrings getHardwareStrings() override { return m_inner->getHardwareStrings(); }
    void setPostCallback(OnPostCallback onPost,
                         void* context,
                         bool useBgraReadback,
                         uint32_t displayId) override {
        char line[192] = {};
        std::snprintf(line, sizeof(line),
                      "renderlib_wrapper setPostCallback cb=%p ctx=%p bgra=%d display=%u\n",
                      reinterpret_cast<void*>(onPost), context, useBgraReadback ? 1 : 0, displayId);
        logLine(line);
        m_inner->setPostCallback(onPost, context, useBgraReadback, displayId);
    }
    void addListener(FrameBufferChangeEventListener* listener) override {
        m_inner->addListener(listener);
    }
    void removeListener(FrameBufferChangeEventListener* listener) override {
        m_inner->removeListener(listener);
    }
    bool asyncReadbackSupported() override { return m_inner->asyncReadbackSupported(); }
    ReadPixelsCallback getReadPixelsCallback() override { return m_inner->getReadPixelsCallback(); }
    FlushReadPixelPipeline getFlushReadPixelPipeline() override {
        return m_inner->getFlushReadPixelPipeline();
    }
    bool showOpenGLSubwindow(FBNativeWindowType window,
                             int wx,
                             int wy,
                             int ww,
                             int wh,
                             int fbw,
                             int fbh,
                             float dpr,
                             float zRot,
                             bool deleteExisting,
                             bool hideWindow) override {
        return m_inner->showOpenGLSubwindow(
                window, wx, wy, ww, wh, fbw, fbh, dpr, zRot, deleteExisting, hideWindow);
    }
    bool destroyOpenGLSubwindow() override { return m_inner->destroyOpenGLSubwindow(); }
    void setOpenGLDisplayRotation(float zRot) override {
        m_inner->setOpenGLDisplayRotation(zRot);
    }
    void setOpenGLDisplayTranslation(float px, float py) override {
        m_inner->setOpenGLDisplayTranslation(px, py);
    }
    void repaintOpenGLDisplay() override { m_inner->repaintOpenGLDisplay(); }
    bool hasGuestPostedAFrame() override { return m_inner->hasGuestPostedAFrame(); }
    void resetGuestPostedAFrame() override { m_inner->resetGuestPostedAFrame(); }
    void setScreenMask(int width, int height, const unsigned char* rgbaData) override {
        m_inner->setScreenMask(width, height, rgbaData);
    }
    void setMultiDisplay(uint32_t id,
                         int32_t x,
                         int32_t y,
                         uint32_t w,
                         uint32_t h,
                         uint32_t dpi,
                         bool add) override {
        m_inner->setMultiDisplay(id, x, y, w, h, dpi, add);
    }
    void setMultiDisplayColorBuffer(uint32_t id, uint32_t cb) override {
        m_inner->setMultiDisplayColorBuffer(id, cb);
    }
    void onGuestGraphicsProcessCreate(uint64_t puid) override {
        m_inner->onGuestGraphicsProcessCreate(puid);
    }
    void cleanupProcGLObjects(uint64_t puid) override { m_inner->cleanupProcGLObjects(puid); }
    void waitForProcessCleanup() override { m_inner->waitForProcessCleanup(); }
    AndroidVirtioGpuOps* getVirtioGpuOps(void) override { return m_inner->getVirtioGpuOps(); }
    void stop(bool wait) override { m_inner->stop(wait); }
    void finish() override { m_inner->finish(); }
    void pauseAllPreSave() override { m_inner->pauseAllPreSave(); }
    void resumeAll() override { m_inner->resumeAll(); }
    void save(android::base::Stream* stream,
              const android::snapshot::ITextureSaverPtr& textureSaver) override {
        m_inner->save(stream, textureSaver);
    }
    bool load(android::base::Stream* stream,
              const android::snapshot::ITextureLoaderPtr& textureLoader) override {
        return m_inner->load(stream, textureLoader);
    }
    void fillGLESUsages(android_studio::EmulatorGLESUsages* usages) override {
        m_inner->fillGLESUsages(usages);
    }
    int getScreenshot(unsigned int nChannels,
                      unsigned int* width,
                      unsigned int* height,
                      uint8_t* pixels,
                      size_t* cPixels,
                      int displayId = 0,
                      int desiredWidth = 0,
                      int desiredHeight = 0,
                      int desiredRotation = 0,
                      gfxstream::Rect rect = {{0, 0}, {0, 0}}) override {
        return m_inner->getScreenshot(
                nChannels, width, height, pixels, cPixels, displayId, desiredWidth,
                desiredHeight, desiredRotation, rect);
    }
    void snapshotOperationCallback(int snapshotterOp, int snapshotterStage) override {
        m_inner->snapshotOperationCallback(snapshotterOp, snapshotterStage);
    }
    void setVsyncHz(int vsyncHz) override { m_inner->setVsyncHz(vsyncHz); }
    void setDisplayConfigs(int configId, int w, int h, int dpiX, int dpiY) override {
        m_inner->setDisplayConfigs(configId, w, h, dpiX, dpiY);
    }
    void setDisplayActiveConfig(int configId) override {
        m_inner->setDisplayActiveConfig(configId);
    }
    const void* getEglDispatch() override { return m_inner->getEglDispatch(); }
    const void* getGles2Dispatch() override { return m_inner->getGles2Dispatch(); }

private:
    gfxstream::RendererPtr m_inner;
};

class RenderLibWrapper final : public gfxstream::RenderLib {
public:
    explicit RenderLibWrapper(gfxstream::RenderLibPtr inner) : m_inner(std::move(inner)) {}

    void setRenderer(SelectedRenderer renderer) override { m_inner->setRenderer(renderer); }
    void setAvdInfo(bool phone, int api) override { m_inner->setAvdInfo(phone, api); }
    void getGlesVersion(int* maj, int* min) override { m_inner->getGlesVersion(maj, min); }
    void setLogger(emugl_logger_struct logger) override { m_inner->setLogger(logger); }
    void setGLObjectCounter(android::base::GLObjectCounter* counter) override {
        m_inner->setGLObjectCounter(counter);
    }
    void setCrashReporter(emugl_crash_reporter_t reporter) override {
        m_inner->setCrashReporter(reporter);
    }
    void setFeatureController(emugl_feature_is_enabled_t featureController) override {
        m_inner->setFeatureController(featureController);
    }
    void setSyncDevice(emugl_sync_create_timeline_t createTimeline,
                       emugl_sync_create_fence_t createFence,
                       emugl_sync_timeline_inc_t timelineInc,
                       emugl_sync_destroy_timeline_t destroyTimeline,
                       emugl_sync_register_trigger_wait_t registerTriggerWait,
                       emugl_sync_device_exists_t deviceExists) override {
        m_inner->setSyncDevice(createTimeline, createFence, timelineInc, destroyTimeline,
                               registerTriggerWait, deviceExists);
    }
    void setDmaOps(emugl_dma_ops ops) override { m_inner->setDmaOps(ops); }
    void setVmOps(const QAndroidVmOperations& vmOperations) override {
        m_inner->setVmOps(vmOperations);
    }
    void setAddressSpaceDeviceControlOps(struct address_space_device_control_ops* ops) override {
        m_inner->setAddressSpaceDeviceControlOps(ops);
    }
    void setWindowOps(const QAndroidEmulatorWindowAgent& windowOperations,
                      const QAndroidMultiDisplayAgent& multiDisplayOperations) override {
        m_inner->setWindowOps(windowOperations, multiDisplayOperations);
    }
    void setUsageTracker(android::base::CpuUsage* cpuUsage,
                         android::base::MemoryTracker* memUsage) override {
        m_inner->setUsageTracker(cpuUsage, memUsage);
    }
    void setGrallocImplementation(GrallocImplementation gralloc) override {
        m_inner->setGrallocImplementation(gralloc);
    }
    bool getOpt(gfxstream::RenderOpt* opt) override { return m_inner->getOpt(opt); }
    gfxstream::RendererPtr initRenderer(int width,
                                        int height,
                                        gfxstream::host::FeatureSet features,
                                        bool useSubWindow,
                                        bool egl2egl) override {
        logSize("renderlib_wrapper initRenderer", width, height);
        auto renderer = m_inner->initRenderer(width, height, features, useSubWindow, egl2egl);
        if (!renderer) {
            logLine("renderlib_wrapper initRenderer result=null\n");
            return nullptr;
        }
        logLine("renderlib_wrapper initRenderer result=wrapped\n");
        return std::make_shared<RendererWrapper>(std::move(renderer));
    }
    android::emulation::OnLastColorBufferRef getOnLastColorBufferRef() override {
        return m_inner->getOnLastColorBufferRef();
    }

private:
    gfxstream::RenderLibPtr m_inner;
};

} // namespace

#pragma warning(suppress: 4190)
extern "C" __declspec(dllexport) gfxstream::RenderLibPtr initLibrary() {
    using init_fn_t = gfxstream::RenderLibPtr(*)();
    auto proc = reinterpret_cast<init_fn_t>(chimera_gfxstream_resolve_stock_export("initLibrary"));
    if (!proc) {
        logLine("forward name=initLibrary resolve=fail\n");
        return nullptr;
    }
    logLine("forward name=initLibrary calling\n");
    auto result = proc();
    logLine(result ? "forward name=initLibrary result=ok\n"
                   : "forward name=initLibrary result=null\n");
    if (result && truthyEnv("CHIMERA_GFXSTREAM_PROXY_WRAP_RENDERLIB")) {
        logLine("forward name=initLibrary wrapping renderlib\n");
        return std::make_unique<RenderLibWrapper>(std::move(result));
    }
    return result;
}

/* Register chimera_on_post via renderer->setPostCallback() using the correct x64 MSVC ABI.
   SDK 15261927 uses OnPostCallback = std::function<void(…)> (non-trivially copyable), so
   the hidden-pointer calling convention applies: rdx = &std::function, not the raw pointer.
   Passing a raw function pointer in rdx (as the old C call did) triggers a std::function
   move that writes NULL into the proxy's code section → WRITE AV.  This C++ wrapper
   constructs the std::function on the heap and passes its address, satisfying the ABI.

   IMPORTANT — heap allocation rationale:
   The stock SDK 15261927 OnPostCallback may have a different std::function layout than our
   PostCallbackStdFn.  When the stock move-constructs from our object it can write a
   "moved-from" marker into the wrong offsets, leaving the source in a corrupted (not just
   empty) state.  If cb lives on the stack, its destructor then reads a garbage impl-pointer
   (observed: 0x4F) and crashes.  Allocating on the heap and never freeing sidesteps both
   the destructor crash and any use-after-free if the stock stores the raw address. */

using PostCallbackFnType = void(void*, uint32_t, int, int, int, int, int, unsigned char*);
using PostCallbackStdFn  = std::function<PostCallbackFnType>;

// __try requires no C++ objects with destructors in scope (C2712).
// Separate the vtable invocation into a destructor-free helper so we can use SEH.
static bool invoke_set_post_cb_vtable(void* renderer, PostCallbackStdFn* cb)
{
    using SetPostCbAbi = void(__cdecl*)(void*, PostCallbackStdFn*, void*, bool, unsigned int);
    void** vtable = *reinterpret_cast<void***>(renderer);
    auto fn = reinterpret_cast<SetPostCbAbi>(vtable[8]);
    __try {
        fn(renderer, cb, nullptr, false, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Heap-allocated once; never freed so it remains valid for any pointer the stock may store.
static std::atomic<bool> g_post_cb_std_registered{false};
static PostCallbackStdFn* g_post_cb_std_ptr = nullptr;

extern "C" void chimera_try_set_post_callback_std(void* rendererSharedPtr) {
    if (!rendererSharedPtr) return;
    void* renderer = *reinterpret_cast<void**>(rendererSharedPtr);
    if (!renderer) return;

    bool expected = false;
    if (!g_post_cb_std_registered.compare_exchange_strong(
            expected, true, std::memory_order_acquire, std::memory_order_relaxed)) {
        return;  // already registered (or in progress on another thread)
    }

    // Allocate permanently — the stock may store a raw pointer to this object.
    g_post_cb_std_ptr = new PostCallbackStdFn(static_cast<PostCallbackFnType*>(chimera_on_post));
    chimera_gfxstream_proxy_log("setPostCallback_cpp allocated_cb\n");

    const bool ok = invoke_set_post_cb_vtable(renderer, g_post_cb_std_ptr);
    chimera_gfxstream_proxy_log(ok
        ? "android_setOpenglesRenderer setPostCallback_cpp_stdfunction=ok\n"
        : "android_setOpenglesRenderer setPostCallback_cpp_stdfunction=EXCEPTION\n");
    chimera_gfxstream_proxy_log("setPostCallback_cpp done\n");
}

extern "C" void chimera_gfxstream_try_wrap_renderer_shared_ptr(void* rendererSharedPtr) {
    if (!rendererSharedPtr) {
        logLine("renderlib_wrapper android_setOpenglesRenderer sharedPtr=null\n");
        return;
    }

    auto* rendererPtr = reinterpret_cast<gfxstream::RendererPtr*>(rendererSharedPtr);
    if (!rendererPtr || !rendererPtr->get()) {
        logLine("renderlib_wrapper android_setOpenglesRenderer renderer=null\n");
        return;
    }

    maybeInstallFrameListener(rendererPtr);

    const char* enabled = std::getenv("CHIMERA_GFXSTREAM_PROXY_WRAP_RENDERER");
    if (!enabled || enabled[0] == '\0' || enabled[0] == '0') {
        return;
    }

    *rendererPtr = std::make_shared<RendererWrapper>(*rendererPtr);
    logLine("renderlib_wrapper android_setOpenglesRenderer result=wrapped\n");
}
