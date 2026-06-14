#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

extern IMAGE_DOS_HEADER __ImageBase;

extern void chimera_gfxstream_try_wrap_renderer_shared_ptr(void* renderer_shared_ptr);
extern void chimera_try_set_post_callback_std(void* renderer_shared_ptr);
extern void chimera_proxy_start_d3d11_bg_thread(void);

typedef void(__cdecl *stream_renderer_flush_fn)(uint32_t res_handle);
typedef struct stream_renderer_param {
    uint64_t key;
    uint64_t value;
} stream_renderer_param;
typedef struct stream_renderer_resource_create_args {
    uint32_t handle;
    uint32_t target;
    uint32_t format;
    uint32_t bind;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t array_size;
    uint32_t last_level;
    uint32_t nr_samples;
    uint32_t flags;
} stream_renderer_resource_create_args;
typedef struct stream_renderer_box {
    uint32_t x, y, z;
    uint32_t w, h, d;
} stream_renderer_box;
typedef struct stream_renderer_create_blob_args {
    uint32_t blob_mem;
    uint32_t blob_flags;
    uint64_t blob_id;
    uint64_t size;
} stream_renderer_create_blob_args;
typedef struct stream_renderer_handle {
    int64_t os_handle;
    uint32_t handle_type;
} stream_renderer_handle;
typedef struct stream_renderer_device_id {
    uint8_t device_uuid[16];
    uint8_t driver_uuid[16];
} stream_renderer_device_id;
typedef struct stream_renderer_vulkan_info_args {
    uint32_t memory_index;
    stream_renderer_device_id device_id;
} stream_renderer_vulkan_info_args;
typedef struct iovec {
    void* iov_base;
    size_t iov_len;
} iovec;
typedef struct stream_renderer_resource_info {
    uint32_t handle;
    uint32_t virgl_format;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t flags;
    uint32_t tex_id;
    uint32_t stride;
    int32_t drm_fourcc;
} stream_renderer_resource_info;
typedef int(__cdecl *stream_renderer_init_fn)(stream_renderer_param* params,
                                              uint64_t num_params);
typedef int(__cdecl *stream_renderer_context_create_fn)(uint32_t ctx_id,
                                                        uint32_t nlen,
                                                        const char* name,
                                                        uint32_t context_init);
typedef int(__cdecl *stream_renderer_resource_create_fn)(
    stream_renderer_resource_create_args* args, iovec* iov, uint32_t num_iovs);
typedef int(__cdecl *stream_renderer_create_blob_fn)(
    uint32_t ctx_id, uint32_t res_handle, const stream_renderer_create_blob_args* create_blob,
    const iovec* iovecs, uint32_t num_iovs, const stream_renderer_handle* handle);
typedef int(__cdecl *stream_renderer_export_blob_fn)(uint32_t res_handle,
                                                     stream_renderer_handle* handle);
typedef void(__cdecl *stream_renderer_ctx_resource_fn)(int ctx_id, int res_handle);
typedef int(__cdecl *stream_renderer_resource_map_info_fn)(uint32_t res_handle,
                                                           uint32_t* map_info);
typedef int(__cdecl *stream_renderer_transfer_read_iov_fn)(
    uint32_t handle, uint32_t ctx_id, uint32_t level, uint32_t stride,
    uint32_t layer_stride, stream_renderer_box* box, uint64_t offset, iovec* iov,
    int iovec_cnt);
typedef int(__cdecl *stream_renderer_transfer_write_iov_fn)(
    uint32_t handle, uint32_t ctx_id, int level, uint32_t stride,
    uint32_t layer_stride, stream_renderer_box* box, uint64_t offset, iovec* iov,
    unsigned int iovec_cnt);
typedef int(__cdecl *stream_renderer_vulkan_info_fn)(uint32_t res_handle,
                                                     stream_renderer_vulkan_info_args* info);
typedef int(__cdecl *stream_renderer_resource_get_info_fn)(
    int res_handle, stream_renderer_resource_info* info);
typedef void(__cdecl *gfxstream_backend_setup_window_fn)(void* native_window_handle,
                                                         int32_t window_x,
                                                         int32_t window_y,
                                                         int32_t window_width,
                                                         int32_t window_height,
                                                         int32_t fb_width,
                                                         int32_t fb_height);
typedef void(__cdecl *gfxstream_backend_screen_rgba_fn)(int width,
                                                        int height,
                                                        const unsigned char* rgbaData);
typedef void(__cdecl *android_set_opengles_renderer_fn)(void* renderer_shared_ptr);
typedef int  (__cdecl *pfn_egl_swap_buffers_t)(void* display, void* surface);
typedef void(__cdecl *on_post_func)(void* context, uint32_t displayId, int width,
                                    int height, int ydir, int format, int type,
                                    unsigned char* pixels);
typedef void(__cdecl *android_set_post_callback_fn)(on_post_func onPost,
                                                    void* onPostContext,
                                                    bool useBgraReadback,
                                                    uint32_t displayId);
typedef void(__cdecl *renderer_set_post_callback_fn)(void* self,
                                                     on_post_func onPost,
                                                     void* context,
                                                     bool useBgraReadback,
                                                     uint32_t displayId);
typedef void(__cdecl *renderer_listener_fn)(void* self, void* listener);
typedef void* (__cdecl *renderer_get_pointer_fn)(void* self);
typedef bool(__cdecl *renderer_bool_noargs_fn)(void* self);
typedef void(__cdecl *renderer_void_noargs_fn)(void* self);
typedef bool(__cdecl *renderer_show_subwindow_fn)(void* self,
                                                  void* window,
                                                  int wx,
                                                  int wy,
                                                  int ww,
                                                  int wh,
                                                  int fbw,
                                                  int fbh,
                                                  float dpr,
                                                  float zRot,
                                                  bool deleteExisting,
                                                  bool hideWindow);
typedef void(__cdecl *renderer_screen_mask_fn)(void* self,
                                               int width,
                                               int height,
                                               const unsigned char* rgbaData);
typedef void(__cdecl *renderer_multi_display_fn)(void* self,
                                                 uint32_t id,
                                                 int32_t x,
                                                 int32_t y,
                                                 uint32_t w,
                                                 uint32_t h,
                                                 uint32_t dpi,
                                                 bool add);
typedef void(__cdecl *renderer_multi_display_color_buffer_fn)(void* self,
                                                              uint32_t id,
                                                              uint32_t cb);
typedef void* (__cdecl *renderer_get_virtio_gpu_ops_fn)(void* self);
typedef void(__cdecl *renderer_set_vsync_hz_fn)(void* self, int vsyncHz);
typedef void(__cdecl *renderer_set_display_configs_fn)(void* self,
                                                       int configId,
                                                       int w,
                                                       int h,
                                                       int dpiX,
                                                       int dpiY);
typedef void(__cdecl *renderer_set_display_active_config_fn)(void* self,
                                                             int configId);
typedef struct renderer_pos {
    int x;
    int y;
} renderer_pos;
typedef struct renderer_size {
    int w;
    int h;
} renderer_size;
typedef struct renderer_rect {
    renderer_pos pos;
    renderer_size size;
} renderer_rect;
typedef int(__cdecl *renderer_get_screenshot_fn)(void* self,
                                                 unsigned int nChannels,
                                                 unsigned int* width,
                                                 unsigned int* height,
                                                 uint8_t* pixels,
                                                 size_t* cPixels,
                                                 int displayId,
                                                 int desiredWidth,
                                                 int desiredHeight,
                                                 int desiredRotation,
                                                 renderer_rect rect);
typedef void(__cdecl *renderer_snapshot_callback_fn)(void* self,
                                                     int snapshotterOp,
                                                     int snapshotterStage);

static HMODULE g_stock_backend = NULL;
static PVOID g_veh_handle = NULL;
static stream_renderer_flush_fn g_stream_renderer_flush = NULL;
static stream_renderer_init_fn g_stream_renderer_init = NULL;
static stream_renderer_context_create_fn g_stream_renderer_context_create = NULL;
static stream_renderer_resource_create_fn g_stream_renderer_resource_create = NULL;
static stream_renderer_create_blob_fn g_stream_renderer_create_blob = NULL;
static stream_renderer_export_blob_fn g_stream_renderer_export_blob = NULL;
static stream_renderer_ctx_resource_fn g_stream_renderer_ctx_attach_resource = NULL;
static stream_renderer_ctx_resource_fn g_stream_renderer_ctx_detach_resource = NULL;
static stream_renderer_resource_map_info_fn g_stream_renderer_resource_map_info = NULL;
static stream_renderer_transfer_read_iov_fn g_stream_renderer_transfer_read_iov = NULL;
static stream_renderer_transfer_write_iov_fn g_stream_renderer_transfer_write_iov = NULL;
static stream_renderer_vulkan_info_fn g_stream_renderer_vulkan_info = NULL;
static stream_renderer_resource_get_info_fn g_stream_renderer_resource_get_info = NULL;
static gfxstream_backend_setup_window_fn g_gfxstream_backend_setup_window = NULL;
static gfxstream_backend_screen_rgba_fn g_gfxstream_backend_set_screen_mask = NULL;
static gfxstream_backend_screen_rgba_fn g_gfxstream_backend_set_screen_background = NULL;
static android_set_opengles_renderer_fn g_android_set_opengles_renderer = NULL;
static android_set_post_callback_fn g_android_set_post_callback = NULL;
static volatile LONG64 g_flush_count = 0;
static volatile LONG64 g_probe_init_count = 0;
static volatile LONG64 g_probe_context_create_count = 0;
static volatile LONG64 g_probe_resource_create_count = 0;
static volatile LONG64 g_probe_create_blob_count = 0;
static volatile LONG64 g_probe_export_blob_count = 0;
static volatile LONG64 g_probe_attach_count = 0;
static volatile LONG64 g_probe_detach_count = 0;
static volatile LONG64 g_probe_map_info_count = 0;
static volatile LONG64 g_probe_transfer_read_count = 0;
static volatile LONG64 g_probe_transfer_write_count = 0;
static volatile LONG64 g_probe_vulkan_info_count = 0;
static volatile LONG64 g_probe_setup_window_count = 0;
static volatile LONG64 g_probe_screen_mask_count = 0;
static volatile LONG64 g_probe_screen_background_count = 0;
static volatile LONG g_renderer_vtable_logged = 0;
static volatile LONG g_renderer_vtable_hooked = 0;
static pfn_egl_swap_buffers_t g_orig_eglSwapBuffers = NULL;
static volatile LONG64 g_egl_swap_count = 0;
static volatile LONG g_egl_swap_hook_installed = 0;
static volatile LONG64 g_hook_set_post_callback_count = 0;
static volatile LONG64 g_hook_add_listener_count = 0;
static volatile LONG64 g_hook_remove_listener_count = 0;
static volatile LONG64 g_hook_async_readback_supported_count = 0;
static volatile LONG64 g_hook_get_read_pixels_callback_count = 0;
static volatile LONG64 g_hook_get_flush_read_pixel_pipeline_count = 0;
static volatile LONG64 g_hook_show_subwindow_count = 0;
static volatile LONG64 g_hook_repaint_count = 0;
static volatile LONG64 g_hook_has_guest_frame_count = 0;
static volatile LONG64 g_hook_reset_guest_frame_count = 0;
static volatile LONG64 g_hook_screen_mask_count = 0;
static volatile LONG64 g_hook_multi_display_count = 0;
static volatile LONG64 g_hook_multi_display_color_buffer_count = 0;
static volatile LONG64 g_hook_get_virtio_gpu_ops_count = 0;
static volatile LONG64 g_hook_get_screenshot_count = 0;
static volatile LONG64 g_hook_snapshot_callback_count = 0;
static volatile LONG64 g_hook_set_vsync_count = 0;
static volatile LONG64 g_hook_set_display_configs_count = 0;
static volatile LONG64 g_hook_set_display_active_config_count = 0;
static volatile LONG64 g_forward_log_count = 0;
static HANDLE g_log_file = INVALID_HANDLE_VALUE;
static char g_log_path[MAX_PATH] = {0};
static LONG g_log_state = 0;

static HMODULE load_stock_backend(void);
static renderer_set_post_callback_fn g_orig_renderer_set_post_callback = NULL;
static renderer_listener_fn g_orig_renderer_add_listener = NULL;
static renderer_listener_fn g_orig_renderer_remove_listener = NULL;
static renderer_bool_noargs_fn g_orig_renderer_async_readback_supported = NULL;
static renderer_get_pointer_fn g_orig_renderer_get_read_pixels_callback = NULL;
static renderer_get_pointer_fn g_orig_renderer_get_flush_read_pixel_pipeline = NULL;
static renderer_show_subwindow_fn g_orig_renderer_show_subwindow = NULL;
static renderer_void_noargs_fn g_orig_renderer_repaint = NULL;
static renderer_bool_noargs_fn g_orig_renderer_has_guest_frame = NULL;
static renderer_void_noargs_fn g_orig_renderer_reset_guest_frame = NULL;
static renderer_screen_mask_fn g_orig_renderer_screen_mask = NULL;
static renderer_multi_display_fn g_orig_renderer_multi_display = NULL;
static renderer_multi_display_color_buffer_fn g_orig_renderer_multi_display_color_buffer = NULL;
static renderer_get_virtio_gpu_ops_fn g_orig_renderer_get_virtio_gpu_ops = NULL;
static renderer_get_screenshot_fn g_orig_renderer_get_screenshot = NULL;
static renderer_snapshot_callback_fn g_orig_renderer_snapshot_callback = NULL;
static renderer_set_vsync_hz_fn g_orig_renderer_set_vsync_hz = NULL;
static renderer_set_display_configs_fn g_orig_renderer_set_display_configs = NULL;
static renderer_set_display_active_config_fn g_orig_renderer_set_display_active_config = NULL;

typedef struct post_callback_state {
    on_post_func callback;
    void* context;
    volatile LONG64 count;
} post_callback_state;

static post_callback_state g_post_callbacks[16] = {{0}};

typedef struct tracked_forward {
    const char* name;
    volatile LONG64 count;
} tracked_forward;

static tracked_forward g_tracked_forwards[] = {
    {"initLibrary", 0},
    {"android_setOpenglesRenderer", 0},
    {"android_stopOpenglesRenderer", 0},
    {"android_setPostCallback", 0},
    {"stream_renderer_init", 0},
    {"stream_renderer_fill_caps", 0},
    {"stream_renderer_submit_cmd", 0},
    {"stream_renderer_context_create", 0},
    {"stream_renderer_context_destroy", 0},
    {"stream_renderer_resource_create", 0},
    {"stream_renderer_resource_get_info", 0},
    {"stream_renderer_transfer_read_iov", 0},
    {"stream_renderer_transfer_write_iov", 0},
    {"gfxstream_backend_setup_window", 0},
    {"gfxstream_backend_set_screen_background", 0},
};

__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;

static void close_log_file(void) {
    if (g_log_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_log_file);
        g_log_file = INVALID_HANDLE_VALUE;
    }
}

void chimera_gfxstream_proxy_log(const char *line) {
    if (g_log_state < 0) {
        return;
    }
    if (g_log_state == 0) {
        const DWORD copied = GetEnvironmentVariableA(
            "CHIMERA_GFXSTREAM_PROXY_LOG", g_log_path, (DWORD)sizeof(g_log_path));
        if (copied == 0 || copied >= sizeof(g_log_path)) {
            g_log_path[0] = '\0';
            g_log_state = -1;
            return;
        }
        g_log_state = 1;
    }
    if (g_log_file == INVALID_HANDLE_VALUE) {
        g_log_file = CreateFileA(g_log_path, FILE_APPEND_DATA,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                 OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (g_log_file == INVALID_HANDLE_VALUE) {
            return;
        }
    }
    DWORD written = 0;
    WriteFile(g_log_file, line, (DWORD)strlen(line), &written, NULL);
}

static LONG64 increment_tracked_forward(const char *name) {
    for (size_t i = 0; i < sizeof(g_tracked_forwards) / sizeof(g_tracked_forwards[0]); ++i) {
        if (strcmp(name, g_tracked_forwards[i].name) == 0) {
            return InterlockedIncrement64(&g_tracked_forwards[i].count);
        }
    }
    return 0;
}

static int should_log_tracked_forward(LONG64 count) {
    if (g_log_state < 0) {
        return 0;
    }
    return count > 0 && (count <= 20 || (count % 300) == 0);
}

static int should_log_probe(volatile LONG64* counter, LONG64* count) {
    *count = InterlockedIncrement64(counter);
    return should_log_tracked_forward(*count);
}

static uint64_t renderer_param_value(stream_renderer_param* params,
                                     uint64_t num_params,
                                     uint64_t key) {
    if (params == NULL) {
        return 0;
    }
    for (uint64_t i = 0; i < num_params; ++i) {
        if (params[i].key == key) {
            return params[i].value;
        }
    }
    return 0;
}

static bool truthy_env(const char* name) {
    char value[16] = {0};
    const DWORD copied = GetEnvironmentVariableA(name, value, (DWORD)sizeof(value));
    return copied > 0 && copied < sizeof(value) &&
           value[0] != '\0' && value[0] != '0' &&
           value[0] != 'f' && value[0] != 'F';
}

static void __cdecl hooked_renderer_set_post_callback(
    void* self, on_post_func onPost, void* context, bool useBgraReadback, uint32_t displayId) {
    LONG64 count = 0;
    if (should_log_probe(&g_hook_set_post_callback_count, &count)) {
        char line[224] = {0};
        snprintf(line, sizeof(line),
                 "renderer_hook setPostCallback count=%lld cb=%p ctx=%p bgra=%d display=%u\n",
                 (long long)count, onPost, context, useBgraReadback ? 1 : 0, displayId);
        chimera_gfxstream_proxy_log(line);
    }
    if (g_orig_renderer_set_post_callback) {
        g_orig_renderer_set_post_callback(self, onPost, context, useBgraReadback, displayId);
    }
}

static void __cdecl hooked_renderer_add_listener(void* self, void* listener) {
    if (g_orig_renderer_add_listener) {
        g_orig_renderer_add_listener(self, listener);
    }
    LONG64 count = 0;
    if (should_log_probe(&g_hook_add_listener_count, &count)) {
        char line[160] = {0};
        snprintf(line, sizeof(line),
                 "renderer_hook addListener count=%lld listener=%p\n",
                 (long long)count, listener);
        chimera_gfxstream_proxy_log(line);
    }
}

static void __cdecl hooked_renderer_remove_listener(void* self, void* listener) {
    if (g_orig_renderer_remove_listener) {
        g_orig_renderer_remove_listener(self, listener);
    }
    LONG64 count = 0;
    if (should_log_probe(&g_hook_remove_listener_count, &count)) {
        char line[160] = {0};
        snprintf(line, sizeof(line),
                 "renderer_hook removeListener count=%lld listener=%p\n",
                 (long long)count, listener);
        chimera_gfxstream_proxy_log(line);
    }
}

static bool __cdecl hooked_renderer_async_readback_supported(void* self) {
    bool result = g_orig_renderer_async_readback_supported
        ? g_orig_renderer_async_readback_supported(self)
        : false;
    LONG64 count = 0;
    if (should_log_probe(&g_hook_async_readback_supported_count, &count)) {
        char line[176] = {0};
        snprintf(line, sizeof(line),
                 "renderer_hook asyncReadbackSupported count=%lld result=%d\n",
                 (long long)count, result ? 1 : 0);
        chimera_gfxstream_proxy_log(line);
    }
    return result;
}

static void* __cdecl hooked_renderer_get_read_pixels_callback(void* self) {
    void* result = g_orig_renderer_get_read_pixels_callback
        ? g_orig_renderer_get_read_pixels_callback(self)
        : NULL;
    LONG64 count = 0;
    if (should_log_probe(&g_hook_get_read_pixels_callback_count, &count)) {
        char line[176] = {0};
        snprintf(line, sizeof(line),
                 "renderer_hook getReadPixelsCallback count=%lld result=%p\n",
                 (long long)count, result);
        chimera_gfxstream_proxy_log(line);
    }
    return result;
}

static void* __cdecl hooked_renderer_get_flush_read_pixel_pipeline(void* self) {
    void* result = g_orig_renderer_get_flush_read_pixel_pipeline
        ? g_orig_renderer_get_flush_read_pixel_pipeline(self)
        : NULL;
    LONG64 count = 0;
    if (should_log_probe(&g_hook_get_flush_read_pixel_pipeline_count, &count)) {
        char line[192] = {0};
        snprintf(line, sizeof(line),
                 "renderer_hook getFlushReadPixelPipeline count=%lld result=%p\n",
                 (long long)count, result);
        chimera_gfxstream_proxy_log(line);
    }
    return result;
}

static bool __cdecl hooked_renderer_show_subwindow(void* self,
                                                   void* window,
                                                   int wx,
                                                   int wy,
                                                   int ww,
                                                   int wh,
                                                   int fbw,
                                                   int fbh,
                                                   float dpr,
                                                   float zRot,
                                                   bool deleteExisting,
                                                   bool hideWindow) {
    bool result = false;
    if (g_orig_renderer_show_subwindow) {
        result = g_orig_renderer_show_subwindow(self, window, wx, wy, ww, wh, fbw, fbh,
                                                dpr, zRot, deleteExisting, hideWindow);
    }
    LONG64 count = 0;
    if (should_log_probe(&g_hook_show_subwindow_count, &count)) {
        char line[256] = {0};
        snprintf(line, sizeof(line),
                 "renderer_hook showOpenGLSubwindow count=%lld result=%d hwnd=%p win=%dx%d fb=%dx%d hide=%d\n",
                 (long long)count, result ? 1 : 0, window, ww, wh, fbw, fbh,
                 hideWindow ? 1 : 0);
        chimera_gfxstream_proxy_log(line);
    }
    return result;
}

static void __cdecl hooked_renderer_repaint(void* self) {
    if (g_orig_renderer_repaint) {
        g_orig_renderer_repaint(self);
    }
    LONG64 count = 0;
    if (should_log_probe(&g_hook_repaint_count, &count)) {
        char line[128] = {0};
        snprintf(line, sizeof(line), "renderer_hook repaint count=%lld\n", (long long)count);
        chimera_gfxstream_proxy_log(line);
    }
}

static bool __cdecl hooked_renderer_has_guest_frame(void* self) {
    bool result = g_orig_renderer_has_guest_frame ? g_orig_renderer_has_guest_frame(self) : false;
    LONG64 count = 0;
    if (should_log_probe(&g_hook_has_guest_frame_count, &count)) {
        char line[160] = {0};
        snprintf(line, sizeof(line),
                 "renderer_hook hasGuestPostedAFrame count=%lld result=%d\n",
                 (long long)count, result ? 1 : 0);
        chimera_gfxstream_proxy_log(line);
    }
    return result;
}

static void __cdecl hooked_renderer_reset_guest_frame(void* self) {
    if (g_orig_renderer_reset_guest_frame) {
        g_orig_renderer_reset_guest_frame(self);
    }
    LONG64 count = 0;
    if (should_log_probe(&g_hook_reset_guest_frame_count, &count)) {
        char line[160] = {0};
        snprintf(line, sizeof(line),
                 "renderer_hook resetGuestPostedAFrame count=%lld\n",
                 (long long)count);
        chimera_gfxstream_proxy_log(line);
    }
}

static void __cdecl hooked_renderer_screen_mask(void* self,
                                                int width,
                                                int height,
                                                const unsigned char* rgbaData) {
    if (g_orig_renderer_screen_mask) {
        g_orig_renderer_screen_mask(self, width, height, rgbaData);
    }
    LONG64 count = 0;
    if (should_log_probe(&g_hook_screen_mask_count, &count)) {
        char line[176] = {0};
        snprintf(line, sizeof(line),
                 "renderer_hook setScreenMask count=%lld size=%dx%d data=%p\n",
                 (long long)count, width, height, rgbaData);
        chimera_gfxstream_proxy_log(line);
    }
}

static void __cdecl hooked_renderer_multi_display(void* self,
                                                  uint32_t id,
                                                  int32_t x,
                                                  int32_t y,
                                                  uint32_t w,
                                                  uint32_t h,
                                                  uint32_t dpi,
                                                  bool add) {
    if (g_orig_renderer_multi_display) {
        g_orig_renderer_multi_display(self, id, x, y, w, h, dpi, add);
    }
    LONG64 count = 0;
    if (should_log_probe(&g_hook_multi_display_count, &count)) {
        char line[224] = {0};
        snprintf(line, sizeof(line),
                 "renderer_hook setMultiDisplay count=%lld id=%u pos=%d,%d size=%ux%u dpi=%u add=%d\n",
                 (long long)count, id, x, y, w, h, dpi, add ? 1 : 0);
        chimera_gfxstream_proxy_log(line);
    }
}

static void __cdecl hooked_renderer_multi_display_color_buffer(void* self,
                                                               uint32_t id,
                                                               uint32_t cb) {
    if (g_orig_renderer_multi_display_color_buffer) {
        g_orig_renderer_multi_display_color_buffer(self, id, cb);
    }
    LONG64 count = 0;
    if (should_log_probe(&g_hook_multi_display_color_buffer_count, &count)) {
        char line[176] = {0};
        snprintf(line, sizeof(line),
                 "renderer_hook setMultiDisplayColorBuffer count=%lld id=%u cb=%u\n",
                 (long long)count, id, cb);
        chimera_gfxstream_proxy_log(line);
    }
}

static void* __cdecl hooked_renderer_get_virtio_gpu_ops(void* self) {
    void* result = g_orig_renderer_get_virtio_gpu_ops
        ? g_orig_renderer_get_virtio_gpu_ops(self)
        : NULL;
    LONG64 count = 0;
    if (should_log_probe(&g_hook_get_virtio_gpu_ops_count, &count)) {
        char line[160] = {0};
        snprintf(line, sizeof(line),
                 "renderer_hook getVirtioGpuOps count=%lld result=%p\n",
                 (long long)count, result);
        chimera_gfxstream_proxy_log(line);
    }
    return result;
}

/* Forward declaration — defined in gfxstream_proxy_d3d11.cpp */
extern void chimera_proxy_try_publish_d3d11_frame(int width, int height);

static int __cdecl hooked_renderer_get_screenshot(void* self,
                                                  unsigned int nChannels,
                                                  unsigned int* width,
                                                  unsigned int* height,
                                                  uint8_t* pixels,
                                                  size_t* cPixels,
                                                  int displayId,
                                                  int desiredWidth,
                                                  int desiredHeight,
                                                  int desiredRotation,
                                                  renderer_rect rect) {
    /* Log entry FIRST (before any crash-prone D3D11 code) so we know slot 35 was called
       even if the subsequent D3D11 probe crashes the process. */
    const LONG64 enterCount = InterlockedIncrement64(&g_hook_get_screenshot_count);
    if (enterCount <= 5 || (enterCount % 300) == 0) {
        char eline[128] = {0};
        snprintf(eline, sizeof(eline),
                 "renderer_hook getScreenshot entering count=%lld tid=%lu desired=%dx%d\n",
                 (long long)enterCount, (unsigned long)GetCurrentThreadId(),
                 desiredWidth, desiredHeight);
        chimera_gfxstream_proxy_log(eline);
    }
    const int w = (desiredWidth  > 0) ? desiredWidth  : 1920;
    const int h = (desiredHeight > 0) ? desiredHeight : 1080;
    /* Guard D3D11/EGL probe with SEH — a crash here must not take down the emulator. */
    __try {
        chimera_proxy_try_publish_d3d11_frame(w, h);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        chimera_gfxstream_proxy_log("renderer_hook getScreenshot d3d11_probe=EXCEPTION\n");
    }
    const size_t beforePixels = cPixels ? *cPixels : 0;
    const unsigned int beforeWidth = width ? *width : 0;
    const unsigned int beforeHeight = height ? *height : 0;
    const int result = g_orig_renderer_get_screenshot
        ? g_orig_renderer_get_screenshot(self, nChannels, width, height, pixels, cPixels,
                                         displayId, desiredWidth, desiredHeight,
                                         desiredRotation, rect)
        : -1;
    if (enterCount <= 5 || (enterCount % 300) == 0) {
        char line[320] = {0};
        snprintf(line, sizeof(line),
                 "renderer_hook getScreenshot done count=%lld result=%d in=%ux%u/%llu out=%ux%u/%llu\n",
                 (long long)enterCount, result, beforeWidth, beforeHeight,
                 (unsigned long long)beforePixels, width ? *width : 0,
                 height ? *height : 0, (unsigned long long)(cPixels ? *cPixels : 0));
        chimera_gfxstream_proxy_log(line);
    }
    return result;
}

static void __cdecl hooked_renderer_snapshot_callback(void* self,
                                                      int snapshotterOp,
                                                      int snapshotterStage) {
    if (g_orig_renderer_snapshot_callback) {
        g_orig_renderer_snapshot_callback(self, snapshotterOp, snapshotterStage);
    }
    LONG64 count = 0;
    if (should_log_probe(&g_hook_snapshot_callback_count, &count)) {
        char line[176] = {0};
        snprintf(line, sizeof(line),
                 "renderer_hook snapshotOperationCallback count=%lld op=%d stage=%d\n",
                 (long long)count, snapshotterOp, snapshotterStage);
        chimera_gfxstream_proxy_log(line);
    }
}

static void __cdecl hooked_renderer_set_vsync_hz(void* self, int vsyncHz) {
    if (g_orig_renderer_set_vsync_hz) {
        g_orig_renderer_set_vsync_hz(self, vsyncHz);
    }
    LONG64 count = 0;
    if (should_log_probe(&g_hook_set_vsync_count, &count)) {
        char line[144] = {0};
        snprintf(line, sizeof(line),
                 "renderer_hook setVsyncHz count=%lld hz=%d\n",
                 (long long)count, vsyncHz);
        chimera_gfxstream_proxy_log(line);
    }
}

static void __cdecl hooked_renderer_set_display_configs(void* self,
                                                        int configId,
                                                        int w,
                                                        int h,
                                                        int dpiX,
                                                        int dpiY) {
    if (g_orig_renderer_set_display_configs) {
        g_orig_renderer_set_display_configs(self, configId, w, h, dpiX, dpiY);
    }
    LONG64 count = 0;
    if (should_log_probe(&g_hook_set_display_configs_count, &count)) {
        char line[192] = {0};
        snprintf(line, sizeof(line),
                 "renderer_hook setDisplayConfigs count=%lld config=%d size=%dx%d dpi=%dx%d\n",
                 (long long)count, configId, w, h, dpiX, dpiY);
        chimera_gfxstream_proxy_log(line);
    }
}

static void __cdecl hooked_renderer_set_display_active_config(void* self, int configId) {
    if (g_orig_renderer_set_display_active_config) {
        g_orig_renderer_set_display_active_config(self, configId);
    }
    LONG64 count = 0;
    if (should_log_probe(&g_hook_set_display_active_config_count, &count)) {
        char line[160] = {0};
        snprintf(line, sizeof(line),
                 "renderer_hook setDisplayActiveConfig count=%lld config=%d\n",
                 (long long)count, configId);
        chimera_gfxstream_proxy_log(line);
    }
}

static void maybe_hook_renderer_vtable(void* renderer, void** vtable) {
    if (renderer == NULL || vtable == NULL) {
        return;
    }
    if (InterlockedCompareExchange(&g_renderer_vtable_hooked, 1, 0) != 0) {
        return;
    }
    /* Both vtable approaches (clone and in-place) break the emulator:
       Clone: dynamic_cast<FrameBuffer*> returns nullptr inside the gRPC handler.
       In-place: triggers STATUS_HEAP_CORRUPTION (0xC0000374) in the emulator
       immediately after patching — likely a CFI/vtable integrity check inside
       gfxstream or ANGLE.  Frame interception is therefore done via eglSwapBuffers
       IAT hook in patch_egl_swap_hook() instead. */
    (void)vtable;
    chimera_gfxstream_proxy_log("renderer_hook install=skipped (using eglSwapBuffers IAT)\n");
}

static void log_renderer_vtable(void* renderer) {
    if (renderer == NULL) {
        return;
    }
    if (InterlockedCompareExchange(&g_renderer_vtable_logged, 1, 0) != 0) {
        return;
    }
    HMODULE stock = load_stock_backend();
    void** vtable = NULL;
    __try {
        vtable = *(void***)renderer;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        chimera_gfxstream_proxy_log("renderer_vtable probe=exception reading vtable\n");
        return;
    }
    char line[256] = {0};
    snprintf(line, sizeof(line),
             "renderer_vtable renderer=%p vtable=%p stockBase=%p\n",
             renderer, vtable, stock);
    chimera_gfxstream_proxy_log(line);
    for (int i = 0; i < 48; ++i) {
        void* slot = NULL;
        __try {
            slot = vtable ? vtable[i] : NULL;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            snprintf(line, sizeof(line), "renderer_vtable slot=%d read=exception\n", i);
            chimera_gfxstream_proxy_log(line);
            break;
        }
        const unsigned long long rva = stock
            ? (unsigned long long)((uintptr_t)slot - (uintptr_t)stock)
            : 0;
        snprintf(line, sizeof(line),
                 "renderer_vtable slot=%02d ptr=%p stockRva=0x%llx\n",
                 i, slot, rva);
        chimera_gfxstream_proxy_log(line);
    }
    /* Isolation gate: skip vtable hook to test if it's responsible for gRPC hangs. */
    char skipHook[4] = {0};
    GetEnvironmentVariableA("CHIMERA_PROXY_SKIP_VTABLE_HOOK", skipHook, sizeof(skipHook));
    if (skipHook[0] == '1') {
        chimera_gfxstream_proxy_log("renderer_hook install=skipped (isolation gate)\n");
    } else {
        maybe_hook_renderer_vtable(renderer, vtable);
    }
}

/* ---- VEH crash diagnostic ------------------------------------------- */

static LONG NTAPI proxy_crash_veh(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_PRIV_INSTRUCTION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    PCONTEXT ctx = ep->ContextRecord;
    void* addr = ep->ExceptionRecord->ExceptionAddress;
    char line[512] = {0};
    snprintf(line, sizeof(line),
             "proxy_exception code=0x%08lX crashAddr=%p stockBase=%p tid=%lu\n",
             (unsigned long)code, addr, (void*)g_stock_backend,
             (unsigned long)GetCurrentThreadId());
    chimera_gfxstream_proxy_log(line);
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        snprintf(line, sizeof(line), "proxy_exception_av rw=%llu badAddr=%p\n",
                 (unsigned long long)ep->ExceptionRecord->ExceptionInformation[0],
                 (void*)ep->ExceptionRecord->ExceptionInformation[1]);
        chimera_gfxstream_proxy_log(line);
    }
    /* Dump general-purpose registers for root cause analysis */
    snprintf(line, sizeof(line),
             "proxy_exception_regs rcx=%p rdx=%p r8=%p r9=%p\n",
             (void*)ctx->Rcx, (void*)ctx->Rdx, (void*)ctx->R8, (void*)ctx->R9);
    chimera_gfxstream_proxy_log(line);
    snprintf(line, sizeof(line),
             "proxy_exception_regs rax=%p rbx=%p rsi=%p rdi=%p r10=%p r11=%p\n",
             (void*)ctx->Rax, (void*)ctx->Rbx, (void*)ctx->Rsi, (void*)ctx->Rdi,
             (void*)ctx->R10, (void*)ctx->R11);
    chimera_gfxstream_proxy_log(line);
    /* Use RtlCaptureStackBackTrace for unwind-accurate call chain,
       then annotate each frame with module name + RVA. */
    void* frames[64] = {0};
    USHORT nFrames = 0;
    __try {
        nFrames = RtlCaptureStackBackTrace(0, 64, frames, NULL);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        nFrames = 0;
    }
    for (USHORT fi = 0; fi < nFrames; ++fi) {
        void* val = frames[fi];
        if (!val) continue;
        HMODULE hMod = NULL;
        char modName[MAX_PATH] = {0};
        BOOL inMod = FALSE;
        __try {
            inMod = GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCSTR)val, &hMod);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            inMod = FALSE;
        }
        if (!inMod || !hMod) {
            snprintf(line, sizeof(line),
                     "proxy_bt[%02u] addr=%p (no module)\n", fi, val);
        } else {
            __try {
                GetModuleFileNameA(hMod, modName, (DWORD)(sizeof(modName) - 1));
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                modName[0] = '\0';
            }
            const char* modBase = modName;
            for (const char* p = modName; *p; ++p) {
                if (*p == '\\' || *p == '/') modBase = p + 1;
            }
            uintptr_t modRva = (uintptr_t)val - (uintptr_t)hMod;
            snprintf(line, sizeof(line),
                     "proxy_bt[%02u] addr=%p rva=0x%llX mod=%s\n",
                     fi, val, (unsigned long long)modRva, modBase);
        }
        chimera_gfxstream_proxy_log(line);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* ---- IAT patch: redirect stock's self-name queries back to stock ---- */

static HMODULE WINAPI my_stock_GetModuleHandleA(LPCSTR name) {
    if (name && _stricmp(name, "libgfxstream_backend.dll") == 0 && g_stock_backend) {
        chimera_gfxstream_proxy_log("iat_hook GetModuleHandleA->stock\n");
        return g_stock_backend;
    }
    return GetModuleHandleA(name);
}

static HMODULE WINAPI my_stock_GetModuleHandleW(LPCWSTR name) {
    if (name && _wcsicmp(name, L"libgfxstream_backend.dll") == 0 && g_stock_backend) {
        chimera_gfxstream_proxy_log("iat_hook GetModuleHandleW->stock\n");
        return g_stock_backend;
    }
    return GetModuleHandleW(name);
}

static HMODULE WINAPI my_stock_LoadLibraryA(LPCSTR name) {
    if (name && _stricmp(name, "libgfxstream_backend.dll") == 0 && g_stock_backend) {
        chimera_gfxstream_proxy_log("iat_hook LoadLibraryA->stock\n");
        return g_stock_backend;
    }
    return LoadLibraryA(name);
}

/* ---- Vulkan frame-capture hooks ----
   The stock loads ALL Vulkan functions via GetProcAddress on vulkan-1.dll.
   We intercept key calls to observe and eventually capture GPU frames.

   VkPresentInfoKHR layout (Vulkan spec, packed per Microsoft x64 ABI):
     uint32_t sType, const void* pNext,
     uint32_t waitSemaphoreCount, const VkSemaphore* pWaitSemaphores,
     uint32_t swapchainCount, const VkSwapchainKHR* pSwapchains,
     const uint32_t* pImageIndices, VkResult* pResults               */
typedef uint32_t vkresult_t;
typedef struct {
    uint32_t       sType;
    const void*    pNext;
    uint32_t       waitSemaphoreCount;
    const uint64_t* pWaitSemaphores;
    uint32_t       swapchainCount;
    const uint64_t* pSwapchains;
    const uint32_t* pImageIndices;
    vkresult_t*    pResults;
} vk_present_info_t;

typedef vkresult_t (WINAPI *pfn_vkQueuePresentKHR_t)(void* queue, const vk_present_info_t* pPresentInfo);
typedef void* (WINAPI *pfn_vkGetDeviceProcAddr_t)(void* device, const char* pName);

static pfn_vkQueuePresentKHR_t  g_real_vkQueuePresentKHR  = NULL;
static pfn_vkGetDeviceProcAddr_t g_real_vkGetDeviceProcAddr = NULL;
static volatile LONG64 g_vk_present_count = 0;

static vkresult_t WINAPI hooked_vkQueuePresentKHR(void* queue, const vk_present_info_t* pPresentInfo)
{
    const LONG64 cnt = InterlockedIncrement64(&g_vk_present_count);
    if (cnt <= 10 || (cnt % 60) == 0) {
        uint32_t sc  = pPresentInfo ? pPresentInfo->swapchainCount : 0;
        uint32_t idx = (pPresentInfo && pPresentInfo->pImageIndices) ? pPresentInfo->pImageIndices[0] : 0;
        char line[128];
        snprintf(line, sizeof(line),
                 "vkQueuePresentKHR count=%lld queue=%p sc=%u idx=%u\n",
                 (long long)cnt, queue, sc, idx);
        chimera_gfxstream_proxy_log(line);
    }
    return g_real_vkQueuePresentKHR ? g_real_vkQueuePresentKHR(queue, pPresentInfo) : 0 /*VK_SUCCESS*/;
}

/* Device-level lookup: intercept vkQueuePresentKHR if the stock calls vkGetDeviceProcAddr. */
static void* WINAPI hooked_vkGetDeviceProcAddr(void* device, const char* pName)
{
    void* result = g_real_vkGetDeviceProcAddr
        ? g_real_vkGetDeviceProcAddr(device, pName)
        : NULL;
    if (pName && strcmp(pName, "vkQueuePresentKHR") == 0 && result) {
        if (!g_real_vkQueuePresentKHR)
            g_real_vkQueuePresentKHR = (pfn_vkQueuePresentKHR_t)result;
        return (void*)hooked_vkQueuePresentKHR;
    }
    return result;
}

/* ---- GetProcAddress hook: log Vulkan/EGL lookups from stock DLL ----
   The stock DLL loads Vulkan entirely dynamically via GetProcAddress.
   Intercepting these calls reveals the exact Vulkan API surface and lets
   us return hooked function pointers for key frame-capture entry points. */
typedef FARPROC (WINAPI *pfn_GetProcAddress_t)(HMODULE, LPCSTR);
static pfn_GetProcAddress_t g_real_GetProcAddress = NULL;
static volatile LONG64 g_gpa_vk_count = 0;

static FARPROC WINAPI hooked_stock_GetProcAddress(HMODULE hModule, LPCSTR lpProcName) {
    /* Call the REAL GetProcAddress directly (not via IAT) to avoid recursion. */
    FARPROC result = g_real_GetProcAddress
        ? g_real_GetProcAddress(hModule, lpProcName)
        : NULL;

    /* Log only Vulkan (vk*) and EGL (egl*) lookups; skip ordinals (high bits set). */
    if (lpProcName && ((uintptr_t)lpProcName >> 16)) {
        const char* n = lpProcName;
        if ((n[0]=='v' && n[1]=='k') ||
            (n[0]=='e' && n[1]=='g' && n[2]=='l')) {
            LONG64 cnt = InterlockedIncrement64(&g_gpa_vk_count);
            if (cnt <= 128 || (cnt % 64) == 0) {
                char line[192];
                snprintf(line, sizeof(line),
                         "stock_GetProcAddress[%lld] name=%s result=%p\n",
                         (long long)cnt, n, (void*)result);
                chimera_gfxstream_proxy_log(line);
            }

            /* Intercept key frame-capture entry points */
            if (strcmp(n, "vkQueuePresentKHR") == 0 && result) {
                g_real_vkQueuePresentKHR = (pfn_vkQueuePresentKHR_t)result;
                chimera_gfxstream_proxy_log("stock_GetProcAddress: hooking vkQueuePresentKHR\n");
                return (FARPROC)hooked_vkQueuePresentKHR;
            }
            if (strcmp(n, "vkGetDeviceProcAddr") == 0 && result) {
                g_real_vkGetDeviceProcAddr = (pfn_vkGetDeviceProcAddr_t)result;
                chimera_gfxstream_proxy_log("stock_GetProcAddress: hooking vkGetDeviceProcAddr\n");
                return (FARPROC)hooked_vkGetDeviceProcAddr;
            }
        }
    }
    return result;
}

static void patch_iat_in_module(HMODULE hMod,
                                 const char* importDll,
                                 const char* funcName,
                                 FARPROC replacement) {
    BYTE* base = (BYTE*)hMod;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;

    DWORD importRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!importRva) return;

    IMAGE_IMPORT_DESCRIPTOR* imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + importRva);
    for (; imp->Name; ++imp) {
        const char* dll = (const char*)(base + imp->Name);
        if (_stricmp(dll, importDll) != 0) continue;

        IMAGE_THUNK_DATA* orig = imp->OriginalFirstThunk
            ? (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk) : NULL;
        IMAGE_THUNK_DATA* iat  = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);

        for (DWORD i = 0; iat[i].u1.Function; ++i) {
            if (!orig) continue;
            if (orig[i].u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            IMAGE_IMPORT_BY_NAME* byName =
                (IMAGE_IMPORT_BY_NAME*)(base + (DWORD)orig[i].u1.AddressOfData);
            if (_stricmp((const char*)byName->Name, funcName) != 0) continue;

            DWORD old = 0;
            VirtualProtect(&iat[i].u1.Function, sizeof(FARPROC), PAGE_READWRITE, &old);
            iat[i].u1.Function = (ULONG_PTR)replacement;
            VirtualProtect(&iat[i].u1.Function, sizeof(FARPROC), old, &old);

            char line[160] = {0};
            snprintf(line, sizeof(line), "iat_patched dll=%s func=%s ok\n", importDll, funcName);
            chimera_gfxstream_proxy_log(line);
            return;
        }
        char line[160] = {0};
        snprintf(line, sizeof(line), "iat_patched dll=%s func=%s not_found\n", importDll, funcName);
        chimera_gfxstream_proxy_log(line);
        return;
    }
}

static void patch_stock_name_resolution(void) {
    if (!g_stock_backend) return;

    /* Cache the REAL GetProcAddress before we patch the IAT, so hooked_stock_GetProcAddress
       can call it without going through the stock DLL's (now-patched) IAT slot. */
    if (!g_real_GetProcAddress) {
        g_real_GetProcAddress = (pfn_GetProcAddress_t)GetProcAddress(
            GetModuleHandleA("KERNEL32.dll"), "GetProcAddress");
    }
    patch_iat_in_module(g_stock_backend, "KERNEL32.dll", "GetProcAddress",
                        (FARPROC)hooked_stock_GetProcAddress);
    chimera_gfxstream_proxy_log("iat_patched dll=KERNEL32.dll func=GetProcAddress ok\n");

    /* Stock DLL calls GetModuleHandleA("libgfxstream_backend.dll") internally to get its
       own HMODULE. With our proxy loaded under that name it returns the wrong handle.
       Redirect these IAT entries to our stubs that return g_stock_backend instead. */
    patch_iat_in_module(g_stock_backend, "KERNEL32.dll", "GetModuleHandleA",
                        (FARPROC)my_stock_GetModuleHandleA);
    patch_iat_in_module(g_stock_backend, "KERNEL32.dll", "GetModuleHandleW",
                        (FARPROC)my_stock_GetModuleHandleW);
    patch_iat_in_module(g_stock_backend, "KERNEL32.dll", "LoadLibraryA",
                        (FARPROC)my_stock_LoadLibraryA);
}

/* ---- eglSwapBuffers IAT hook ------------------------------------------ */
/* vtable in-place and clone approaches both corrupt the emulator (heap corruption /
   dynamic_cast NULL).  Hooking eglSwapBuffers via the stock's IAT is safe (IAT is a
   writable data section) and fires on the render thread with the EGL context current,
   giving us the right moment to probe the ANGLE D3D11 render target. */

/* Forward declaration — defined in gfxstream_proxy_d3d11.cpp */
extern void chimera_proxy_try_publish_d3d11_frame(int width, int height);

static int __cdecl my_egl_swap_buffers(void* display, void* surface) {
    const LONG64 count = InterlockedIncrement64(&g_egl_swap_count);
    if (count <= 5 || (count % 60) == 0) {
        char line[192] = {0};
        snprintf(line, sizeof(line),
                 "eglSwapBuffers_hook count=%lld disp=%p surf=%p tid=%lu\n",
                 (long long)count, display, surface, (unsigned long)GetCurrentThreadId());
        chimera_gfxstream_proxy_log(line);
    }
    /* Probe ANGLE D3D11 RT — EGL context is current here (render thread). */
    __try {
        chimera_proxy_try_publish_d3d11_frame(1920, 1080);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        chimera_gfxstream_proxy_log("eglSwapBuffers_hook d3d11_probe=EXCEPTION\n");
    }
    return g_orig_eglSwapBuffers ? g_orig_eglSwapBuffers(display, surface) : 1;
}

static void patch_egl_swap_hook(void) {
    if (InterlockedCompareExchange(&g_egl_swap_hook_installed, 1, 0) != 0) return;
    if (!g_stock_backend) {
        chimera_gfxstream_proxy_log("eglSwapBuffers_hook=skipped (no stock)\n");
        return;
    }
    HMODULE egl = GetModuleHandleA("libEGL.dll");
    if (!egl) {
        chimera_gfxstream_proxy_log("eglSwapBuffers_hook=skipped (libEGL not loaded)\n");
        return;
    }
    g_orig_eglSwapBuffers = (pfn_egl_swap_buffers_t)GetProcAddress(egl, "eglSwapBuffers");
    patch_iat_in_module(g_stock_backend, "libEGL.dll", "eglSwapBuffers",
                        (FARPROC)my_egl_swap_buffers);
}

/* ---- heartbeat -------------------------------------------------------- */

static DWORD WINAPI proxy_heartbeat_thread(LPVOID param) {
    (void)param;
    DWORD tick = 0;
    for (;;) {
        Sleep(10000);
        tick += 10;
        char line[96] = {0};
        snprintf(line, sizeof(line), "proxy_heartbeat t=%us\n", (unsigned)tick);
        chimera_gfxstream_proxy_log(line);
    }
    return 0;
}

static volatile LONG g_heartbeat_started = 0;

static void maybe_start_heartbeat(void) {
    if (InterlockedCompareExchange(&g_heartbeat_started, 1, 0) != 0) {
        return;
    }
    HANDLE ht = CreateThread(NULL, 0, proxy_heartbeat_thread, NULL, 0, NULL);
    if (ht) {
        chimera_gfxstream_proxy_log("proxy_heartbeat_thread=started\n");
        CloseHandle(ht);
    } else {
        chimera_gfxstream_proxy_log("proxy_heartbeat_thread=FAILED\n");
    }
}

static HMODULE load_stock_backend(void) {
    if (g_stock_backend != NULL) {
        return g_stock_backend;
    }

    char module_path[MAX_PATH] = {0};
    const DWORD path_len = GetModuleFileNameA((HINSTANCE)&__ImageBase, module_path,
                                              (DWORD)sizeof(module_path));
    if (path_len == 0 || path_len >= sizeof(module_path)) {
        return NULL;
    }

    for (char *cursor = module_path + path_len; cursor != module_path; --cursor) {
        if (*cursor == '\\' || *cursor == '/') {
            cursor[1] = '\0';
            break;
        }
    }

    char stock_path[MAX_PATH] = {0};
    const int written = snprintf(stock_path, sizeof(stock_path), "%slibgfxstream_backend_stock.dll",
                                 module_path);
    if (written <= 0 || written >= (int)sizeof(stock_path)) {
        return NULL;
    }

    g_stock_backend = LoadLibraryA(stock_path);
    if (g_stock_backend == NULL) {
        chimera_gfxstream_proxy_log("load_stock_backend=fail\n");
    } else {
        chimera_gfxstream_proxy_log("load_stock_backend=ok\n");
        /* Redirect stock DLL's own-name queries (GetModuleHandleA/LoadLibraryA with
           "libgfxstream_backend.dll") back to g_stock_backend before any stock code
           runs, so stock's initRenderer can safely get its own HMODULE. */
        patch_stock_name_resolution();
        /* Start heartbeat here — after stock DLL is loaded and DllMain has returned.
           CreateThread from DllMain can cause early process exit on some GPU paths. */
        maybe_start_heartbeat();
    }
    return g_stock_backend;
}

FARPROC chimera_gfxstream_resolve_stock_export(const char *name) {
    HMODULE stock = load_stock_backend();
    if (stock == NULL) {
        return NULL;
    }
    FARPROC proc = GetProcAddress(stock, name);
    if (proc == NULL) {
        char line[160] = {0};
        snprintf(line, sizeof(line), "resolve_stock_export=fail name=%s\n", name);
        chimera_gfxstream_proxy_log(line);
    }
    return proc;
}

typedef uintptr_t(__cdecl *forward12_fn)(uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                                         uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                                         uintptr_t, uintptr_t, uintptr_t, uintptr_t);

uintptr_t chimera_gfxstream_forward_call(const char *name,
                                         uintptr_t a1,
                                         uintptr_t a2,
                                         uintptr_t a3,
                                         uintptr_t a4,
                                         uintptr_t a5,
                                         uintptr_t a6,
                                         uintptr_t a7,
                                         uintptr_t a8,
                                         uintptr_t a9,
                                         uintptr_t a10,
                                         uintptr_t a11,
                                         uintptr_t a12) {
    FARPROC proc = chimera_gfxstream_resolve_stock_export(name);
    if (proc == NULL) {
        return 0;
    }
    const LONG64 global_count = InterlockedIncrement64(&g_forward_log_count);
    const LONG64 tracked_count = increment_tracked_forward(name);
    if (should_log_tracked_forward(tracked_count)) {
        char line[192] = {0};
        snprintf(line, sizeof(line),
                 "forward count=%lld global=%lld name=%s a1=%llu a2=%llu a3=%llu a4=%llu\n",
                 (long long)tracked_count, (long long)global_count, name,
                 (unsigned long long)a1, (unsigned long long)a2, (unsigned long long)a3,
                 (unsigned long long)a4);
        chimera_gfxstream_proxy_log(line);
    }
    return ((forward12_fn)proc)(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
}


static post_callback_state* post_callback_for_display(uint32_t displayId) {
    if (displayId < (uint32_t)(sizeof(g_post_callbacks) / sizeof(g_post_callbacks[0]))) {
        return &g_post_callbacks[displayId];
    }
    return &g_post_callbacks[0];
}

/* Non-static so gfxstream_proxy_renderlib.cpp can wrap it in std::function */
void __cdecl chimera_on_post(void* ignored,
                                    uint32_t displayId,
                                    int width,
                                    int height,
                                    int ydir,
                                    int format,
                                    int type,
                                    unsigned char* pixels) {
    (void)ignored;
    post_callback_state* state = post_callback_for_display(displayId);
    const LONG64 count = InterlockedIncrement64(&state->count);
    if (count <= 5 || (count % 120) == 0) {
        char line[224] = {0};
        snprintf(line, sizeof(line),
                 "android_onPost count=%lld display=%u size=%dx%d ydir=%d format=%d type=%d pixels=%p\n",
                 (long long)count, displayId, width, height, ydir, format, type, pixels);
        chimera_gfxstream_proxy_log(line);
    }
    /* Attempt GPU-side D3D11 texture publish via ANGLE EGL interop.
       Uses render-target texture directly; never touches the pixels buffer.
       SEH guard: a crash here must not take down the emulator. */
    if (width == 1920 && height == 1080) {
        __try {
            chimera_proxy_try_publish_d3d11_frame(width, height);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            chimera_gfxstream_proxy_log("chimera_on_post d3d11_probe=EXCEPTION\n");
        }
    }
    if (state->callback != NULL) {
        state->callback(state->context, displayId, width, height, ydir, format, type, pixels);
    }
}

__declspec(dllexport) void android_setOpenglesRenderer(void* renderer_shared_ptr) {
    if (g_android_set_opengles_renderer == NULL) {
        g_android_set_opengles_renderer =
            (android_set_opengles_renderer_fn)chimera_gfxstream_resolve_stock_export("android_setOpenglesRenderer");
    }

    void* renderer = NULL;
    if (renderer_shared_ptr != NULL) {
        renderer = *(void**)renderer_shared_ptr;
    }

    char line[192] = {0};
    snprintf(line, sizeof(line), "android_setOpenglesRenderer sharedPtr=%p renderer=%p\n",
             renderer_shared_ptr, renderer);
    chimera_gfxstream_proxy_log(line);
    log_renderer_vtable(renderer);

    /* Forward to stock FIRST so the renderer's display system is fully
       initialized before we install the frame listener via addListener.
       Calling addListener before stock initializes the display config
       triggers a NULL-pointer deref at r9=0x13A inside 0x18006D800. */
    if (g_android_set_opengles_renderer != NULL) {
        g_android_set_opengles_renderer(renderer_shared_ptr);
    }

    chimera_gfxstream_try_wrap_renderer_shared_ptr(renderer_shared_ptr);

    /* In headless (-no-window) mode the emulator never calls android_setPostCallback.
       SDK 15261927 changed OnPostCallback from void(*)(…) to std::function<…>, so the
       plain C vtable[8] call (passing chimera_on_post directly in rdx) triggers a
       std::function move that writes NULL to [chimera_on_post] (proxy code) → WRITE AV.
       Delegate to C++ which constructs a proper std::function and passes it via hidden
       pointer (correct x64 MSVC ABI for non-trivially-copyable by-value parameters). */
    chimera_try_set_post_callback_std(renderer_shared_ptr);

    /* Hook eglSwapBuffers in the stock's IAT (fires on render thread if stock imports it).
       Also start the background D3D11 polling thread which samples the render target at
       ~60 Hz using ID3D11Multithread, covering the headless case where eglSwapBuffers is
       never called from the stock because libEGL is not in its static IAT. */
    patch_egl_swap_hook();
    chimera_proxy_start_d3d11_bg_thread();
}

__declspec(dllexport) void android_setPostCallback(on_post_func onPost,
                                                   void* onPostContext,
                                                   bool useBgraReadback,
                                                   uint32_t displayId) {
    if (g_android_set_post_callback == NULL) {
        g_android_set_post_callback =
            (android_set_post_callback_fn)chimera_gfxstream_resolve_stock_export("android_setPostCallback");
    }

    post_callback_state* state = post_callback_for_display(displayId);
    state->callback = onPost;
    state->context = onPostContext;
    state->count = 0;

    char line[224] = {0};
    snprintf(line, sizeof(line),
             "android_setPostCallback cb=%p ctx=%p bgra=%d display=%u\n",
             onPost, onPostContext, useBgraReadback ? 1 : 0, displayId);
    chimera_gfxstream_proxy_log(line);

    if (g_android_set_post_callback != NULL) {
        g_android_set_post_callback(onPost ? chimera_on_post : NULL,
                                    onPost ? NULL : onPostContext,
                                    useBgraReadback,
                                    displayId);
    }
}

__declspec(dllexport) int stream_renderer_init(stream_renderer_param* params,
                                               uint64_t num_params) {
    if (g_stream_renderer_init == NULL) {
        g_stream_renderer_init = (stream_renderer_init_fn)
            chimera_gfxstream_resolve_stock_export("stream_renderer_init");
    }
    const int result = g_stream_renderer_init ? g_stream_renderer_init(params, num_params) : -1;
    LONG64 count = 0;
    if (should_log_probe(&g_probe_init_count, &count)) {
        const uint64_t flags = renderer_param_value(params, num_params, 2);
        const uint64_t win_w = renderer_param_value(params, num_params, 4);
        const uint64_t win_h = renderer_param_value(params, num_params, 5);
        char line[224] = {0};
        snprintf(line, sizeof(line),
                 "probe stream_renderer_init count=%lld result=%d params=%llu flags=0x%llx win=%llux%llu debug=%llu\n",
                 (long long)count, result, (unsigned long long)num_params,
                 (unsigned long long)flags, (unsigned long long)win_w,
                 (unsigned long long)win_h,
                 (unsigned long long)renderer_param_value(params, num_params, 6));
        chimera_gfxstream_proxy_log(line);
    }
    return result;
}

__declspec(dllexport) int stream_renderer_context_create(uint32_t ctx_id,
                                                         uint32_t nlen,
                                                         const char* name,
                                                         uint32_t context_init) {
    if (g_stream_renderer_context_create == NULL) {
        g_stream_renderer_context_create = (stream_renderer_context_create_fn)
            chimera_gfxstream_resolve_stock_export("stream_renderer_context_create");
    }
    const int result = g_stream_renderer_context_create
        ? g_stream_renderer_context_create(ctx_id, nlen, name, context_init)
        : -1;
    LONG64 count = 0;
    if (should_log_probe(&g_probe_context_create_count, &count)) {
        char line[224] = {0};
        snprintf(line, sizeof(line),
                 "probe stream_renderer_context_create count=%lld result=%d ctx=%u nlen=%u init=0x%x name=%.*s\n",
                 (long long)count, result, ctx_id, nlen, context_init,
                 (int)(nlen > 48 ? 48 : nlen), name ? name : "");
        chimera_gfxstream_proxy_log(line);
    }
    return result;
}

__declspec(dllexport) int stream_renderer_resource_create(
    stream_renderer_resource_create_args* args, iovec* iov, uint32_t num_iovs) {
    if (g_stream_renderer_resource_create == NULL) {
        g_stream_renderer_resource_create = (stream_renderer_resource_create_fn)
            chimera_gfxstream_resolve_stock_export("stream_renderer_resource_create");
    }
    const int result = g_stream_renderer_resource_create
        ? g_stream_renderer_resource_create(args, iov, num_iovs)
        : -1;
    LONG64 count = 0;
    if (should_log_probe(&g_probe_resource_create_count, &count)) {
        char line[256] = {0};
        snprintf(line, sizeof(line),
                 "probe stream_renderer_resource_create count=%lld result=%d res=%u size=%ux%u fmt=%u bind=0x%x flags=0x%x iovs=%u\n",
                 (long long)count, result, args ? args->handle : 0,
                 args ? args->width : 0, args ? args->height : 0,
                 args ? args->format : 0, args ? args->bind : 0,
                 args ? args->flags : 0, num_iovs);
        chimera_gfxstream_proxy_log(line);
    }
    return result;
}

__declspec(dllexport) int stream_renderer_create_blob(
    uint32_t ctx_id, uint32_t res_handle, const stream_renderer_create_blob_args* create_blob,
    const iovec* iovecs, uint32_t num_iovs, const stream_renderer_handle* handle) {
    if (g_stream_renderer_create_blob == NULL) {
        g_stream_renderer_create_blob = (stream_renderer_create_blob_fn)
            chimera_gfxstream_resolve_stock_export("stream_renderer_create_blob");
    }
    const int result = g_stream_renderer_create_blob
        ? g_stream_renderer_create_blob(ctx_id, res_handle, create_blob, iovecs, num_iovs, handle)
        : -1;
    LONG64 count = 0;
    if (should_log_probe(&g_probe_create_blob_count, &count)) {
        char line[256] = {0};
        snprintf(line, sizeof(line),
                 "probe stream_renderer_create_blob count=%lld result=%d ctx=%u res=%u mem=%u flags=0x%x size=%llu handleType=0x%x iovs=%u\n",
                 (long long)count, result, ctx_id, res_handle,
                 create_blob ? create_blob->blob_mem : 0,
                 create_blob ? create_blob->blob_flags : 0,
                 (unsigned long long)(create_blob ? create_blob->size : 0),
                 handle ? handle->handle_type : 0, num_iovs);
        chimera_gfxstream_proxy_log(line);
    }
    return result;
}

__declspec(dllexport) int stream_renderer_export_blob(uint32_t res_handle,
                                                      stream_renderer_handle* handle) {
    if (g_stream_renderer_export_blob == NULL) {
        g_stream_renderer_export_blob = (stream_renderer_export_blob_fn)
            chimera_gfxstream_resolve_stock_export("stream_renderer_export_blob");
    }
    const int result = g_stream_renderer_export_blob
        ? g_stream_renderer_export_blob(res_handle, handle)
        : -1;
    LONG64 count = 0;
    if (should_log_probe(&g_probe_export_blob_count, &count)) {
        char line[224] = {0};
        snprintf(line, sizeof(line),
                 "probe stream_renderer_export_blob count=%lld result=%d res=%u handleType=0x%x osHandle=%lld\n",
                 (long long)count, result, res_handle,
                 handle ? handle->handle_type : 0,
                 (long long)(handle ? handle->os_handle : 0));
        chimera_gfxstream_proxy_log(line);
    }
    return result;
}

__declspec(dllexport) void stream_renderer_ctx_attach_resource(int ctx_id, int res_handle) {
    if (g_stream_renderer_ctx_attach_resource == NULL) {
        g_stream_renderer_ctx_attach_resource = (stream_renderer_ctx_resource_fn)
            chimera_gfxstream_resolve_stock_export("stream_renderer_ctx_attach_resource");
    }
    if (g_stream_renderer_ctx_attach_resource) {
        g_stream_renderer_ctx_attach_resource(ctx_id, res_handle);
    }
    LONG64 count = 0;
    if (should_log_probe(&g_probe_attach_count, &count)) {
        char line[160] = {0};
        snprintf(line, sizeof(line),
                 "probe stream_renderer_ctx_attach_resource count=%lld ctx=%d res=%d\n",
                 (long long)count, ctx_id, res_handle);
        chimera_gfxstream_proxy_log(line);
    }
}

__declspec(dllexport) void stream_renderer_ctx_detach_resource(int ctx_id, int res_handle) {
    if (g_stream_renderer_ctx_detach_resource == NULL) {
        g_stream_renderer_ctx_detach_resource = (stream_renderer_ctx_resource_fn)
            chimera_gfxstream_resolve_stock_export("stream_renderer_ctx_detach_resource");
    }
    if (g_stream_renderer_ctx_detach_resource) {
        g_stream_renderer_ctx_detach_resource(ctx_id, res_handle);
    }
    LONG64 count = 0;
    if (should_log_probe(&g_probe_detach_count, &count)) {
        char line[160] = {0};
        snprintf(line, sizeof(line),
                 "probe stream_renderer_ctx_detach_resource count=%lld ctx=%d res=%d\n",
                 (long long)count, ctx_id, res_handle);
        chimera_gfxstream_proxy_log(line);
    }
}

__declspec(dllexport) int stream_renderer_resource_map_info(uint32_t res_handle,
                                                           uint32_t* map_info) {
    if (g_stream_renderer_resource_map_info == NULL) {
        g_stream_renderer_resource_map_info = (stream_renderer_resource_map_info_fn)
            chimera_gfxstream_resolve_stock_export("stream_renderer_resource_map_info");
    }
    const int result = g_stream_renderer_resource_map_info
        ? g_stream_renderer_resource_map_info(res_handle, map_info)
        : -1;
    LONG64 count = 0;
    if (should_log_probe(&g_probe_map_info_count, &count)) {
        char line[160] = {0};
        snprintf(line, sizeof(line),
                 "probe stream_renderer_resource_map_info count=%lld result=%d res=%u map=0x%x\n",
                 (long long)count, result, res_handle, map_info ? *map_info : 0);
        chimera_gfxstream_proxy_log(line);
    }
    return result;
}

__declspec(dllexport) int stream_renderer_transfer_read_iov(
    uint32_t handle, uint32_t ctx_id, uint32_t level, uint32_t stride,
    uint32_t layer_stride, stream_renderer_box* box, uint64_t offset, iovec* iov,
    int iovec_cnt) {
    if (g_stream_renderer_transfer_read_iov == NULL) {
        g_stream_renderer_transfer_read_iov = (stream_renderer_transfer_read_iov_fn)
            chimera_gfxstream_resolve_stock_export("stream_renderer_transfer_read_iov");
    }
    const int result = g_stream_renderer_transfer_read_iov
        ? g_stream_renderer_transfer_read_iov(handle, ctx_id, level, stride, layer_stride,
                                              box, offset, iov, iovec_cnt)
        : -1;
    LONG64 count = 0;
    if (should_log_probe(&g_probe_transfer_read_count, &count)) {
        char line[256] = {0};
        snprintf(line, sizeof(line),
                 "probe stream_renderer_transfer_read_iov count=%lld result=%d res=%u ctx=%u box=%ux%u stride=%u iovs=%d\n",
                 (long long)count, result, handle, ctx_id, box ? box->w : 0,
                 box ? box->h : 0, stride, iovec_cnt);
        chimera_gfxstream_proxy_log(line);
    }
    return result;
}

__declspec(dllexport) int stream_renderer_transfer_write_iov(
    uint32_t handle, uint32_t ctx_id, int level, uint32_t stride,
    uint32_t layer_stride, stream_renderer_box* box, uint64_t offset, iovec* iov,
    unsigned int iovec_cnt) {
    if (g_stream_renderer_transfer_write_iov == NULL) {
        g_stream_renderer_transfer_write_iov = (stream_renderer_transfer_write_iov_fn)
            chimera_gfxstream_resolve_stock_export("stream_renderer_transfer_write_iov");
    }
    const int result = g_stream_renderer_transfer_write_iov
        ? g_stream_renderer_transfer_write_iov(handle, ctx_id, level, stride, layer_stride,
                                               box, offset, iov, iovec_cnt)
        : -1;
    LONG64 count = 0;
    if (should_log_probe(&g_probe_transfer_write_count, &count)) {
        char line[256] = {0};
        snprintf(line, sizeof(line),
                 "probe stream_renderer_transfer_write_iov count=%lld result=%d res=%u ctx=%u box=%ux%u stride=%u iovs=%u\n",
                 (long long)count, result, handle, ctx_id, box ? box->w : 0,
                 box ? box->h : 0, stride, iovec_cnt);
        chimera_gfxstream_proxy_log(line);
    }
    return result;
}

__declspec(dllexport) int stream_renderer_vulkan_info(uint32_t res_handle,
                                                     stream_renderer_vulkan_info_args* info) {
    if (g_stream_renderer_vulkan_info == NULL) {
        g_stream_renderer_vulkan_info = (stream_renderer_vulkan_info_fn)
            chimera_gfxstream_resolve_stock_export("stream_renderer_vulkan_info");
    }
    const int result = g_stream_renderer_vulkan_info
        ? g_stream_renderer_vulkan_info(res_handle, info)
        : -1;
    LONG64 count = 0;
    if (should_log_probe(&g_probe_vulkan_info_count, &count)) {
        char line[192] = {0};
        snprintf(line, sizeof(line),
                 "probe stream_renderer_vulkan_info count=%lld result=%d res=%u memoryIndex=%u device0=%02x%02x\n",
                 (long long)count, result, res_handle, info ? info->memory_index : 0,
                 info ? info->device_id.device_uuid[0] : 0,
                 info ? info->device_id.device_uuid[1] : 0);
        chimera_gfxstream_proxy_log(line);
    }
    return result;
}

__declspec(dllexport) void gfxstream_backend_setup_window(void* native_window_handle,
                                                         int32_t window_x,
                                                         int32_t window_y,
                                                         int32_t window_width,
                                                         int32_t window_height,
                                                         int32_t fb_width,
                                                         int32_t fb_height) {
    if (g_gfxstream_backend_setup_window == NULL) {
        g_gfxstream_backend_setup_window = (gfxstream_backend_setup_window_fn)
            chimera_gfxstream_resolve_stock_export("gfxstream_backend_setup_window");
    }
    if (g_gfxstream_backend_setup_window) {
        g_gfxstream_backend_setup_window(native_window_handle, window_x, window_y,
                                         window_width, window_height, fb_width, fb_height);
    }
    LONG64 count = 0;
    if (should_log_probe(&g_probe_setup_window_count, &count)) {
        char line[224] = {0};
        snprintf(line, sizeof(line),
                 "probe gfxstream_backend_setup_window count=%lld hwnd=%p window=%dx%d fb=%dx%d\n",
                 (long long)count, native_window_handle, window_width, window_height,
                 fb_width, fb_height);
        chimera_gfxstream_proxy_log(line);
    }
}

__declspec(dllexport) void gfxstream_backend_set_screen_mask(int width,
                                                            int height,
                                                            const unsigned char* rgbaData) {
    if (g_gfxstream_backend_set_screen_mask == NULL) {
        g_gfxstream_backend_set_screen_mask = (gfxstream_backend_screen_rgba_fn)
            chimera_gfxstream_resolve_stock_export("gfxstream_backend_set_screen_mask");
    }
    if (g_gfxstream_backend_set_screen_mask) {
        g_gfxstream_backend_set_screen_mask(width, height, rgbaData);
    }
    LONG64 count = 0;
    if (should_log_probe(&g_probe_screen_mask_count, &count)) {
        char line[160] = {0};
        snprintf(line, sizeof(line),
                 "probe gfxstream_backend_set_screen_mask count=%lld size=%dx%d data=%p\n",
                 (long long)count, width, height, rgbaData);
        chimera_gfxstream_proxy_log(line);
    }
}

__declspec(dllexport) void gfxstream_backend_set_screen_background(
    int width, int height, const unsigned char* rgbaData) {
    if (g_gfxstream_backend_set_screen_background == NULL) {
        g_gfxstream_backend_set_screen_background = (gfxstream_backend_screen_rgba_fn)
            chimera_gfxstream_resolve_stock_export("gfxstream_backend_set_screen_background");
    }
    if (g_gfxstream_backend_set_screen_background) {
        g_gfxstream_backend_set_screen_background(width, height, rgbaData);
    }
    LONG64 count = 0;
    if (should_log_probe(&g_probe_screen_background_count, &count)) {
        char line[176] = {0};
        snprintf(line, sizeof(line),
                 "probe gfxstream_backend_set_screen_background count=%lld size=%dx%d data=%p\n",
                 (long long)count, width, height, rgbaData);
        chimera_gfxstream_proxy_log(line);
    }
}

__declspec(dllexport) void stream_renderer_flush(uint32_t res_handle) {
    if (g_stream_renderer_flush == NULL) {
        g_stream_renderer_flush =
            (stream_renderer_flush_fn)chimera_gfxstream_resolve_stock_export("stream_renderer_flush");
    }

    if (g_stream_renderer_flush != NULL) {
        g_stream_renderer_flush(res_handle);
    }

    const LONG64 count = InterlockedIncrement64(&g_flush_count);
    if (count <= 3 || (count % 120) == 0) {
        if (g_stream_renderer_resource_get_info == NULL) {
            g_stream_renderer_resource_get_info =
                (stream_renderer_resource_get_info_fn)chimera_gfxstream_resolve_stock_export(
                    "stream_renderer_resource_get_info");
        }

        stream_renderer_resource_info info = {0};
        const int info_result = g_stream_renderer_resource_get_info
            ? g_stream_renderer_resource_get_info((int)res_handle, &info)
            : -1;
        char line[256] = {0};
        snprintf(line, sizeof(line),
                 "stream_renderer_flush count=%lld res=%u info=%d size=%ux%u virgl=%u drm=%d stride=%u flags=%u tex=%u\n",
                 (long long)count, res_handle, info_result, info.width, info.height,
                 info.virgl_format, info.drm_fourcc, info.stride, info.flags, info.tex_id);
        chimera_gfxstream_proxy_log(line);
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)instance;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        chimera_gfxstream_proxy_log("dll_process_attach=libgfxstream_backend_proxy\n");
        /* Install VEH first so we catch crashes in stock initRenderer and anywhere else.
           Priority=1 means this handler runs before SEH on the crashing thread. */
        g_veh_handle = AddVectoredExceptionHandler(1, proxy_crash_veh);
        chimera_gfxstream_proxy_log(g_veh_handle ? "proxy_veh=installed\n" : "proxy_veh=FAILED\n");
    }
    if (reason == DLL_PROCESS_DETACH) {
        /* Log before closing so we know if the DLL is unloaded before heartbeat fires */
        chimera_gfxstream_proxy_log("dll_process_detach=libgfxstream_backend_proxy\n");
        if (g_veh_handle) {
            RemoveVectoredExceptionHandler(g_veh_handle);
            g_veh_handle = NULL;
        }
        close_log_file();
    }
    return TRUE;
}
