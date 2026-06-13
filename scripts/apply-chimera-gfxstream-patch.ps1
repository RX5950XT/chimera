param(
    [Parameter(Mandatory = $true)]
    [string]$SourceDir
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path -LiteralPath $SourceDir
$hostDir = Join-Path $root "host"
$glDir = Join-Path $hostDir "gl"
$vulkanDir = Join-Path $hostDir "vulkan"
$displayGl = Join-Path $glDir "DisplayGl.cpp"
$displayVk = @(
    (Join-Path $vulkanDir "DisplayVk.cpp"),
    (Join-Path $vulkanDir "display_vk.cpp")
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($displayVk)) {
    $displayVk = Join-Path $vulkanDir "DisplayVk.cpp"
}
$frameBuffer = Join-Path $hostDir "FrameBuffer.cpp"
$cmake = Join-Path $hostDir "CMakeLists.txt"
$vulkanCmake = Join-Path $vulkanDir "CMakeLists.txt"
$bridgeHeader = Join-Path $glDir "ChimeraGfxstreamSharedTextureBridge.h"
$bridgeCpp = Join-Path $glDir "ChimeraGfxstreamSharedTextureBridge.cpp"
$vulkanBridgeHeader = Join-Path $vulkanDir "ChimeraGfxstreamVulkanSharedTextureBridge.h"
$vulkanBridgeCpp = Join-Path $vulkanDir "ChimeraGfxstreamVulkanSharedTextureBridge.cpp"
$vulkanBridgeTemplateDir = Join-Path $PSScriptRoot "..\src\host\runtime\gfxstream_bridge_templates\vulkan"
$vulkanBridgeTemplateHeader = Join-Path $vulkanBridgeTemplateDir "ChimeraGfxstreamVulkanSharedTextureBridge.h"
$vulkanBridgeTemplateCpp = Join-Path $vulkanBridgeTemplateDir "ChimeraGfxstreamVulkanSharedTextureBridge.cpp"
$aospRoot = [System.IO.Path]::GetFullPath((Join-Path $root "..\..\.."))

$hasLegacyGlDisplay = (Test-Path -LiteralPath $displayGl -PathType Leaf) -and
    (Test-Path -LiteralPath $frameBuffer -PathType Leaf) -and
    (Test-Path -LiteralPath $cmake -PathType Leaf)
$hasVulkanDisplay = (Test-Path -LiteralPath $displayVk -PathType Leaf) -and
    (Test-Path -LiteralPath $vulkanCmake -PathType Leaf)
$modernVulkanLayout = (Split-Path -Leaf $displayVk) -eq "display_vk.cpp"

if (!$hasLegacyGlDisplay -and !$hasVulkanDisplay) {
    throw "gfxstream source is missing supported display files under: $hostDir"
}

function Write-TextFile([string]$Path, [string]$Text) {
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Text.Replace("`r`n", "`n"), $utf8NoBom)
}

function Copy-TextTemplate([string]$Template, [string]$Destination, [string]$Description) {
    if (!(Test-Path -LiteralPath $Template -PathType Leaf)) {
        throw "Cannot patch $Description; template not found: $Template"
    }
    Write-TextFile $Destination ([System.IO.File]::ReadAllText($Template))
}

function Replace-Once([string]$Path, [string]$Needle, [string]$Replacement, [string]$Description) {
    $text = [System.IO.File]::ReadAllText($Path).Replace("`r`n", "`n")
    $needleLf = $Needle.Replace("`r`n", "`n")
    $replacementLf = $Replacement.Replace("`r`n", "`n")
    if ($text.Contains($replacementLf)) {
        return
    }
    if (!$text.Contains($needleLf)) {
        throw "Cannot patch $Description; expected block not found in $Path"
    }
    $text = $text.Replace($needleLf, $replacementLf)
    Write-TextFile $Path $text
}

function Replace-FirstAvailable([string]$Path, [string[]]$Needles, [string]$Replacement, [string]$Description) {
    $text = [System.IO.File]::ReadAllText($Path).Replace("`r`n", "`n")
    $replacementLf = $Replacement.Replace("`r`n", "`n")
    if ($text.Contains($replacementLf)) {
        return
    }
    foreach ($needle in $Needles) {
        $needleLf = $needle.Replace("`r`n", "`n")
        if ($text.Contains($needleLf)) {
            $text = $text.Replace($needleLf, $replacementLf)
            Write-TextFile $Path $text
            return
        }
    }
    throw "Cannot patch $Description; expected block not found in $Path"
}

function Replace-AllLiteral([string]$Path, [string]$Needle, [string]$Replacement) {
    $text = [System.IO.File]::ReadAllText($Path).Replace("`r`n", "`n")
    $needleLf = $Needle.Replace("`r`n", "`n")
    $replacementLf = $Replacement.Replace("`r`n", "`n")
    if ($text.Contains($needleLf)) {
        $text = $text.Replace($needleLf, $replacementLf)
        Write-TextFile $Path $text
    }
}

function Find-UcrtIncludePath {
    $candidates = @()
    if ($env:UniversalCRTSdkDir -and $env:UCRTVersion) {
        $candidates += (Join-Path $env:UniversalCRTSdkDir "Include\$env:UCRTVersion\ucrt")
    }
    $kitsRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\Include"
    if (Test-Path -LiteralPath $kitsRoot) {
        $candidates += Get-ChildItem -LiteralPath $kitsRoot -Directory |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName "ucrt" }
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate "stdlib.h")) {
            return $candidate.Replace("\", "/")
        }
    }
    return ""
}

function Find-MsvcIncludePath {
    $candidates = @()
    if ($env:VCToolsInstallDir) {
        $candidates += (Join-Path $env:VCToolsInstallDir "include")
    }
    $vcRoot = Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022\Community\VC\Tools\MSVC"
    if (Test-Path -LiteralPath $vcRoot) {
        $candidates += Get-ChildItem -LiteralPath $vcRoot -Directory |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName "include" }
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate "limits.h")) {
            return $candidate.Replace("\", "/")
        }
    }
    return ""
}

function Replace-AemuIncludeNextBlock([string]$Path, [string]$Header, [string]$Replacement, [string]$Description) {
    $text = [System.IO.File]::ReadAllText($Path).Replace("`r`n", "`n")
    $replacementLf = $Replacement.Replace("`r`n", "`n")
    if ($text.Contains($replacementLf)) {
        return
    }

    $blockStart = '#if defined(_MSC_VER) && !defined(__clang__)'
    $start = $text.IndexOf($blockStart, [System.StringComparison]::Ordinal)
    $endNeedle = "#include_next <$Header>`n#endif"
    if ($start -ge 0) {
        $end = $text.IndexOf($endNeedle, $start, [System.StringComparison]::Ordinal)
        if ($end -ge 0) {
            $end += $endNeedle.Length
            $text = $text.Substring(0, $start) + $replacementLf + $text.Substring($end)
            Write-TextFile $Path $text
            return
        }
    }

    Replace-Once $Path "#include_next <$Header>" $replacementLf $Description
}

$header = @'
// Chimera shared D3D11 texture publisher for gfxstream GL display.
#pragma once

#include <EGL/egl.h>

#include <cstdint>

#include "DisplayGl.h"

namespace gfxstream {
namespace gl {

class TextureDraw;

class ChimeraGfxstreamSharedTextureBridge {
  public:
    static ChimeraGfxstreamSharedTextureBridge& get();

    bool isEnabled() const;
    bool shouldSuppressReadbackFallback() const;
    bool publish(const DisplayGl::Post& post,
                 TextureDraw* textureDraw,
                 EGLDisplay display,
                 EGLSurface currentSurface,
                 EGLContext context,
                 int width,
                 int height);

  private:
    ChimeraGfxstreamSharedTextureBridge();
    ~ChimeraGfxstreamSharedTextureBridge();

    ChimeraGfxstreamSharedTextureBridge(const ChimeraGfxstreamSharedTextureBridge&) = delete;
    ChimeraGfxstreamSharedTextureBridge& operator=(const ChimeraGfxstreamSharedTextureBridge&) = delete;

    struct Platform;

    bool ensureInitialized(EGLDisplay display, EGLSurface currentSurface, int width, int height);
    void reset();

    bool mEnabled = false;
    bool mRequireSharedTexture = false;
    bool mHardUnavailable = false;
    int mWidth = 0;
    int mHeight = 0;
    EGLDisplay mDisplay = EGL_NO_DISPLAY;
    Platform* mPlatform = nullptr;
};

}  // namespace gl
}  // namespace gfxstream
'@

$cpp = @'
// Chimera shared D3D11 texture publisher for gfxstream GL display.
#include "ChimeraGfxstreamSharedTextureBridge.h"

#include "OpenGLESDispatch/EGLDispatch.h"
#include "OpenGLESDispatch/DispatchTables.h"
#include "TextureDraw.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#else
#include <strings.h>
#endif

namespace gfxstream {
namespace gl {
namespace {

constexpr uint32_t kD3D11TextureMagic = 0x43485458;  // CHTX
constexpr uint32_t kD3D11TextureVersion = 1;
constexpr uint32_t kD3D11FlagHasAlpha = 0x1;
constexpr uint32_t kD3D11TextureNameChars = 260;

#pragma pack(push, 1)
struct SharedD3D11TextureHeader {
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t headerSize = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t dxgiFormat = 0;
    uint32_t flags = 0;
    uint32_t reserved = 0;
    uint64_t sequence = 0;
    uint16_t textureName[kD3D11TextureNameChars] = {};
};
#pragma pack(pop)

bool envTruthy(const char* name) {
    const char* value = std::getenv(name);
    if (!value || !*value) return false;
#ifdef _WIN32
    return std::strcmp(value, "0") != 0 &&
           _stricmp(value, "false") != 0 &&
           _stricmp(value, "no") != 0 &&
           _stricmp(value, "off") != 0;
#else
    return std::strcmp(value, "0") != 0 &&
           strcasecmp(value, "false") != 0 &&
           strcasecmp(value, "no") != 0 &&
           strcasecmp(value, "off") != 0;
#endif
}

#ifdef _WIN32
static_assert(sizeof(SharedD3D11TextureHeader) == 560,
              "shared D3D11 texture header ABI must match Chimera host");

using EglCreatePbufferFromClientBuffer =
    EGLSurface(EGLAPIENTRY*)(EGLDisplay, EGLenum, EGLClientBuffer, EGLConfig, const EGLint*);

constexpr EGLint EGL_D3D_TEXTURE_2D_SHARE_HANDLE_ANGLE_VALUE = 0x3200;

std::wstring utf8ToWide(const char* text) {
    if (!text || !*text) return std::wstring();
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (needed <= 1) return std::wstring();
    std::wstring value(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, &value[0], needed);
    return value;
}

std::wstring firstEnv(const char* primary, const char* fallback) {
    std::wstring value = utf8ToWide(std::getenv(primary));
    if (!value.empty()) return value;
    return utf8ToWide(std::getenv(fallback));
}

std::wstring defaultTextureName() {
    wchar_t buffer[128] = {};
    swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]),
             L"Local\\ChimeraGfxstreamD3D11Texture_%lu", GetCurrentProcessId());
    return std::wstring(buffer);
}

bool writeTextureName(uint16_t* dst, const std::wstring& name) {
    if (name.empty() || name.size() >= kD3D11TextureNameChars) return false;
    std::memset(dst, 0, sizeof(uint16_t) * kD3D11TextureNameChars);
    for (size_t i = 0; i < name.size(); ++i) {
        dst[i] = static_cast<uint16_t>(name[i]);
    }
    return true;
}

bool findSurfaceConfig(EGLDisplay display, EGLSurface surface, EGLConfig* outConfig) {
    EGLint configId = 0;
    if (!s_egl.eglQuerySurface(display, surface, EGL_CONFIG_ID, &configId)) return false;

    EGLint count = 0;
    if (!s_egl.eglGetConfigs(display, nullptr, 0, &count) || count <= 0) return false;

    std::vector<EGLConfig> configs(static_cast<size_t>(count));
    if (!s_egl.eglGetConfigs(display, configs.data(), count, &count)) return false;

    for (EGLConfig config : configs) {
        EGLint candidateId = 0;
        if (s_egl.eglGetConfigAttrib(display, config, EGL_CONFIG_ID, &candidateId) &&
            candidateId == configId) {
            *outConfig = config;
            return true;
        }
    }
    return false;
}
#endif

}  // namespace

extern "C" const char* ChimeraGfxstreamSharedTextureBridgeMarker() {
    return "ChimeraGfxstreamSharedTextureBridge";
}

#ifdef _WIN32
struct ChimeraGfxstreamSharedTextureBridge::Platform {
    std::wstring metadataName;
    std::wstring textureName;
    std::wstring eventName;
    HANDLE mapping = nullptr;
    HANDLE event = nullptr;
    HANDLE sharedHandle = nullptr;
    SharedD3D11TextureHeader* header = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    EGLSurface surface = EGL_NO_SURFACE;
    EglCreatePbufferFromClientBuffer createPbufferFromClientBuffer = nullptr;
    uint64_t sequence = 0;
};
#else
struct ChimeraGfxstreamSharedTextureBridge::Platform {};
#endif

ChimeraGfxstreamSharedTextureBridge& ChimeraGfxstreamSharedTextureBridge::get() {
    static ChimeraGfxstreamSharedTextureBridge bridge;
    return bridge;
}

ChimeraGfxstreamSharedTextureBridge::ChimeraGfxstreamSharedTextureBridge() {
    mPlatform = new Platform();
#ifdef _WIN32
    mPlatform->metadataName = firstEnv("CHIMERA_GFXSTREAM_D3D11_TEXTURE_METADATA",
                                       "CHIMERA_D3D11_TEXTURE_METADATA");
    mPlatform->textureName = firstEnv("CHIMERA_GFXSTREAM_D3D11_TEXTURE_NAME",
                                      "CHIMERA_D3D11_TEXTURE_NAME");
    if (mPlatform->textureName.empty()) {
        mPlatform->textureName = defaultTextureName();
    }
    mPlatform->eventName = firstEnv("CHIMERA_GFXSTREAM_D3D11_TEXTURE_EVENT",
                                    "CHIMERA_D3D11_TEXTURE_EVENT");
    mEnabled = !mPlatform->metadataName.empty();
    mRequireSharedTexture = envTruthy("CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE");
#endif
}

ChimeraGfxstreamSharedTextureBridge::~ChimeraGfxstreamSharedTextureBridge() {
    reset();
    delete mPlatform;
    mPlatform = nullptr;
}

bool ChimeraGfxstreamSharedTextureBridge::isEnabled() const {
    return mEnabled;
}

bool ChimeraGfxstreamSharedTextureBridge::shouldSuppressReadbackFallback() const {
    return mEnabled && mRequireSharedTexture;
}

bool ChimeraGfxstreamSharedTextureBridge::ensureInitialized(EGLDisplay display,
                                                            EGLSurface currentSurface,
                                                            int width,
                                                            int height) {
#ifndef _WIN32
    (void)display;
    (void)currentSurface;
    (void)width;
    (void)height;
    return false;
#else
    if (!mEnabled || mHardUnavailable) return false;
    if (mPlatform->surface != EGL_NO_SURFACE && width == mWidth && height == mHeight) {
        return true;
    }

    reset();
    mDisplay = display;
    mWidth = width;
    mHeight = height;

    mPlatform->createPbufferFromClientBuffer =
        reinterpret_cast<EglCreatePbufferFromClientBuffer>(
            s_egl.eglGetProcAddress("eglCreatePbufferFromClientBuffer"));
    if (!mPlatform->createPbufferFromClientBuffer) {
        std::fprintf(stderr, "Chimera gfxstream bridge: EGL_ANGLE_d3d_share_handle_client_buffer unavailable\n");
        mHardUnavailable = true;
        return false;
    }

    EGLConfig config = nullptr;
    if (!findSurfaceConfig(display, currentSurface, &config)) {
        std::fprintf(stderr, "Chimera gfxstream bridge: failed to find EGL config for display surface\n");
        mHardUnavailable = true;
        return false;
    }

    mPlatform->mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                            0, sizeof(SharedD3D11TextureHeader),
                                            mPlatform->metadataName.c_str());
    if (!mPlatform->mapping) {
        std::fprintf(stderr, "Chimera gfxstream bridge: CreateFileMappingW failed %lu\n", GetLastError());
        mHardUnavailable = true;
        return false;
    }
    mPlatform->header = static_cast<SharedD3D11TextureHeader*>(
        MapViewOfFile(mPlatform->mapping, FILE_MAP_WRITE, 0, 0,
                      sizeof(SharedD3D11TextureHeader)));
    if (!mPlatform->header) {
        std::fprintf(stderr, "Chimera gfxstream bridge: MapViewOfFile failed %lu\n", GetLastError());
        reset();
        mHardUnavailable = true;
        return false;
    }

    if (!mPlatform->eventName.empty()) {
        mPlatform->event = CreateEventW(nullptr, FALSE, FALSE, mPlatform->eventName.c_str());
        if (!mPlatform->event) {
            std::fprintf(stderr, "Chimera gfxstream bridge: CreateEventW failed %lu\n", GetLastError());
            reset();
            mHardUnavailable = true;
            return false;
        }
    }

    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                   &level, 1, D3D11_SDK_VERSION,
                                   &mPlatform->device, nullptr, &mPlatform->context);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                               &level, 1, D3D11_SDK_VERSION,
                               &mPlatform->device, nullptr, &mPlatform->context);
    }
    if (FAILED(hr)) {
        std::fprintf(stderr, "Chimera gfxstream bridge: D3D11CreateDevice failed 0x%lx\n", hr);
        reset();
        mHardUnavailable = true;
        return false;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
                     D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    hr = mPlatform->device->CreateTexture2D(&desc, nullptr, &mPlatform->texture);
    if (FAILED(hr)) {
        std::fprintf(stderr, "Chimera gfxstream bridge: CreateTexture2D failed 0x%lx\n", hr);
        reset();
        mHardUnavailable = true;
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIResource1> resource;
    hr = mPlatform->texture.As(&resource);
    if (FAILED(hr)) {
        std::fprintf(stderr, "Chimera gfxstream bridge: IDXGIResource1 failed 0x%lx\n", hr);
        reset();
        mHardUnavailable = true;
        return false;
    }
    hr = resource->CreateSharedHandle(nullptr,
                                      DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                      mPlatform->textureName.c_str(),
                                      &mPlatform->sharedHandle);
    if (FAILED(hr)) {
        std::fprintf(stderr, "Chimera gfxstream bridge: CreateSharedHandle failed 0x%lx\n", hr);
        reset();
        mHardUnavailable = true;
        return false;
    }

    const EGLint attrs[] = {
        EGL_WIDTH, width,
        EGL_HEIGHT, height,
        EGL_TEXTURE_FORMAT, EGL_TEXTURE_RGBA,
        EGL_TEXTURE_TARGET, EGL_TEXTURE_2D,
        EGL_NONE
    };
    mPlatform->surface = mPlatform->createPbufferFromClientBuffer(
        display,
        EGL_D3D_TEXTURE_2D_SHARE_HANDLE_ANGLE_VALUE,
        static_cast<EGLClientBuffer>(mPlatform->sharedHandle),
        config,
        attrs);
    if (mPlatform->surface == EGL_NO_SURFACE) {
        std::fprintf(stderr, "Chimera gfxstream bridge: eglCreatePbufferFromClientBuffer failed 0x%x\n",
                     s_egl.eglGetError());
        reset();
        mHardUnavailable = true;
        return false;
    }

    *mPlatform->header = {};
    return true;
#endif
}

bool ChimeraGfxstreamSharedTextureBridge::publish(const DisplayGl::Post& post,
                                                  TextureDraw* textureDraw,
                                                  EGLDisplay display,
                                                  EGLSurface currentSurface,
                                                  EGLContext context,
                                                  int width,
                                                  int height) {
#ifndef _WIN32
    (void)post;
    (void)textureDraw;
    (void)display;
    (void)currentSurface;
    (void)context;
    (void)width;
    (void)height;
    return false;
#else
    if (!mEnabled || !textureDraw || post.layers.empty()) return false;
    if (!ensureInitialized(display, currentSurface, width, height)) return false;

    EGLSurface previousDraw = s_egl.eglGetCurrentSurface(EGL_DRAW);
    EGLSurface previousRead = s_egl.eglGetCurrentSurface(EGL_READ);
    EGLContext previousContext = s_egl.eglGetCurrentContext();
    GLint previousViewport[4] = {0, 0, width, height};
    s_gles2.glGetIntegerv(GL_VIEWPORT, previousViewport);

    if (!s_egl.eglMakeCurrent(display, mPlatform->surface, mPlatform->surface, context)) {
        std::fprintf(stderr, "Chimera gfxstream bridge: eglMakeCurrent(shared) failed 0x%x\n",
                     s_egl.eglGetError());
        return false;
    }

    s_gles2.glViewport(0, 0, width, height);

    bool hasDrawLayer = false;
    bool ok = true;
    for (const DisplayGl::PostLayer& layer : post.layers) {
        if (!layer.colorBuffer) continue;
        if (layer.layerOptions) {
            if (!hasDrawLayer) {
                textureDraw->prepareForDrawLayer();
                hasDrawLayer = true;
            }
            layer.colorBuffer->glOpPostLayer(*layer.layerOptions, post.frameWidth, post.frameHeight);
        } else if (layer.overlayOptions) {
            if (hasDrawLayer) {
                ok = false;
                break;
            }
            layer.colorBuffer->glOpPostViewportScaledWithOverlay(
                layer.overlayOptions->rotation,
                layer.overlayOptions->dx,
                layer.overlayOptions->dy);
        }
    }
    if (hasDrawLayer) {
        textureDraw->cleanupForDrawLayer();
    }
    s_gles2.glFlush();

    s_egl.eglMakeCurrent(display, previousDraw, previousRead, previousContext);
    s_gles2.glViewport(previousViewport[0],
                       previousViewport[1],
                       previousViewport[2],
                       previousViewport[3]);
    if (!ok) return false;

    const uint64_t nextSequence = mPlatform->sequence + 2;
    mPlatform->header->sequence = nextSequence | 1ULL;
    MemoryBarrier();
    mPlatform->header->magic = kD3D11TextureMagic;
    mPlatform->header->version = kD3D11TextureVersion;
    mPlatform->header->headerSize = sizeof(SharedD3D11TextureHeader);
    mPlatform->header->width = static_cast<uint32_t>(width);
    mPlatform->header->height = static_cast<uint32_t>(height);
    mPlatform->header->dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    mPlatform->header->flags = kD3D11FlagHasAlpha;
    if (!writeTextureName(mPlatform->header->textureName, mPlatform->textureName)) {
        return false;
    }
    MemoryBarrier();
    mPlatform->header->sequence = nextSequence;
    mPlatform->sequence = nextSequence;
    if (mPlatform->event) SetEvent(mPlatform->event);
    return true;
#endif
}

void ChimeraGfxstreamSharedTextureBridge::reset() {
#ifdef _WIN32
    if (!mPlatform) return;
    if (mPlatform->surface != EGL_NO_SURFACE) {
        s_egl.eglDestroySurface(mDisplay, mPlatform->surface);
    }
    mPlatform->surface = EGL_NO_SURFACE;
    mPlatform->texture.Reset();
    mPlatform->context.Reset();
    mPlatform->device.Reset();
    if (mPlatform->sharedHandle) {
        CloseHandle(mPlatform->sharedHandle);
        mPlatform->sharedHandle = nullptr;
    }
    if (mPlatform->header) {
        UnmapViewOfFile(mPlatform->header);
        mPlatform->header = nullptr;
    }
    if (mPlatform->event) {
        CloseHandle(mPlatform->event);
        mPlatform->event = nullptr;
    }
    if (mPlatform->mapping) {
        CloseHandle(mPlatform->mapping);
        mPlatform->mapping = nullptr;
    }
    mPlatform->sequence = 0;
#endif
    mDisplay = EGL_NO_DISPLAY;
    mWidth = 0;
    mHeight = 0;
}

}  // namespace gl
}  // namespace gfxstream
'@

if ($hasLegacyGlDisplay) {
Write-TextFile $bridgeHeader $header
Write-TextFile $bridgeCpp $cpp

$displayIncludeNeedle = '#include "DisplayGl.h"'
$displayIncludeReplacement = '#include "DisplayGl.h"' + "`n" + '#include "ChimeraGfxstreamSharedTextureBridge.h"'
Replace-Once $displayGl $displayIncludeNeedle $displayIncludeReplacement "DisplayGl include"

$displayPostNeedle = @'
    s_egl.eglSwapBuffers(surfaceGl->mDisplay, surfaceGl->mSurface);

    return getCompletedFuture();
'@
$displayPostReplacement = @'
    ChimeraGfxstreamSharedTextureBridge::get().publish(
        post,
        mTextureDraw,
        surfaceGl->mDisplay,
        surfaceGl->mSurface,
        surfaceGl->mContext,
        mViewportWidth,
        mViewportHeight);
    s_egl.eglSwapBuffers(surfaceGl->mDisplay, surfaceGl->mSurface);

    return getCompletedFuture();
'@
Replace-Once $displayGl $displayPostNeedle $displayPostReplacement "DisplayGl post hook"

$frameBufferIncludeNeedle = '#include "PostWorkerGl.h"'
$frameBufferIncludeReplacement = '#include "PostWorkerGl.h"' + "`n" + '#include "gl/ChimeraGfxstreamSharedTextureBridge.h"'
Replace-Once $frameBuffer $frameBufferIncludeNeedle $frameBufferIncludeReplacement "FrameBuffer include"

if ($hasVulkanDisplay) {
    Replace-Once $frameBuffer `
        '#include "vulkan/PostWorkerVk.h"' `
        ('#include "vulkan/PostWorkerVk.h"' + "`n" + '#include "vulkan/ChimeraGfxstreamVulkanSharedTextureBridge.h"') `
        "FrameBuffer Vulkan Chimera bridge include"

    $frameBufferHeadlessVkNeedle = @'
    colorBuffer->touch();
    if (m_subWin) {
        Post postCmd;
        postCmd.cmd = PostCmd::Post;
        postCmd.cb = colorBuffer.get();
        postCmd.cbHandle = p_colorbuffer;
        postCmd.completionCallback = std::make_unique<Post::CompletionCallback>(callback);
        sendPostWorkerCmd(std::move(postCmd));
        ret = AsyncResult::OK_AND_CALLBACK_SCHEDULED;
    } else {
        // If there is no sub-window, don't display anything, the client will
        // rely on m_onPost to get the pixels instead.
        ret = AsyncResult::OK_AND_CALLBACK_NOT_SCHEDULED;
    }
'@
    $frameBufferHeadlessVkReplacement = @'
    colorBuffer->touch();
    const bool chimeraHeadlessVkPost =
        !m_subWin && m_displayVk &&
        vk::ChimeraGfxstreamVulkanSharedTextureBridge::get().isEnabled();
    if (m_subWin || chimeraHeadlessVkPost) {
        Post postCmd;
        postCmd.cmd = PostCmd::Post;
        postCmd.cb = colorBuffer.get();
        postCmd.cbHandle = p_colorbuffer;
        postCmd.completionCallback = std::make_unique<Post::CompletionCallback>(callback);
        sendPostWorkerCmd(std::move(postCmd));
        ret = AsyncResult::OK_AND_CALLBACK_SCHEDULED;
    } else {
        // If there is no sub-window, don't display anything, the client will
        // rely on m_onPost to get the pixels instead.
        ret = AsyncResult::OK_AND_CALLBACK_NOT_SCHEDULED;
    }
'@
    Replace-Once $frameBuffer `
        $frameBufferHeadlessVkNeedle `
        $frameBufferHeadlessVkReplacement `
        "FrameBuffer headless Vulkan shared texture post"

    Replace-Once $frameBuffer `
        '    if (!m_subWin) {  // m_subWin is supposed to be false' `
        '    if (!m_subWin && !chimeraHeadlessVkPost) {  // m_subWin is supposed to be false' `
        "FrameBuffer headless Vulkan refcount ownership"
}

$frameBufferOldReadbackNeedle = @'
    if (m_onPost.size() == 0) {
        goto DEC_REFCOUNT_AND_EXIT;
    }
'@
$frameBufferOldReadbackReplacement = @'
    if (m_onPost.size() == 0) {
        goto DEC_REFCOUNT_AND_EXIT;
    }
#if GFXSTREAM_ENABLE_HOST_GLES
    if (gl::ChimeraGfxstreamSharedTextureBridge::get().shouldSuppressReadbackFallback()) {
        goto DEC_REFCOUNT_AND_EXIT;
    }
#endif
'@
$frameBufferText = [System.IO.File]::ReadAllText($frameBuffer).Replace("`r`n", "`n")
if ($frameBufferText.Contains($frameBufferOldReadbackReplacement.Replace("`r`n", "`n"))) {
    # Already patched.
} elseif ($frameBufferText.Contains($frameBufferOldReadbackNeedle.Replace("`r`n", "`n"))) {
    Replace-Once $frameBuffer $frameBufferOldReadbackNeedle $frameBufferOldReadbackReplacement "FrameBuffer onPost readback guard"
} else {
    Replace-Once $frameBuffer `
        '    if (!m_onPost.empty()) {' `
        '    if (!m_onPost.empty() && !gl::ChimeraGfxstreamSharedTextureBridge::get().shouldSuppressReadbackFallback()) {' `
        "FrameBuffer onPost readback guard"
}

$cmakeSourceNeedle = '    PostWorkerGl.cpp' + "`n"
$cmakeSourceReplacement = '    PostWorkerGl.cpp' + "`n" + '    gl/ChimeraGfxstreamSharedTextureBridge.cpp' + "`n"
Replace-Once $cmake $cmakeSourceNeedle $cmakeSourceReplacement "gfxstream source list"

$cmakeD3dNeedle = @'
if (WIN32)
    target_link_libraries(gfxstream_backend_static PRIVATE D3d9.lib)
endif()
'@
$cmakeD3dReplacement = @'
if (WIN32)
    target_link_libraries(gfxstream_backend_static PRIVATE D3d9.lib D3d11.lib Dxgi.lib)
endif()
'@
Replace-Once $cmake $cmakeD3dNeedle $cmakeD3dReplacement "gfxstream D3D11 link libraries"

$cmakeSdkImportBlock = @'

if (WIN32 AND MSVC AND DEFINED CHIMERA_SDK_IMPORT_LIB_DIR)
    target_link_libraries(gfxstream_backend PRIVATE
        "${CHIMERA_SDK_IMPORT_LIB_DIR}/libandroid-emu-agents.lib"
        "${CHIMERA_SDK_IMPORT_LIB_DIR}/libandroid-emu-protos.lib"
        "${CHIMERA_SDK_IMPORT_LIB_DIR}/libandroid-emu-metrics.lib")
    target_link_options(gfxstream_backend PRIVATE
        "/INCLUDE:__imp_?Callback@LineConsumer@emulation@android@@SAHPEAXPEBDH@Z"
        "/INCLUDE:__imp_??0AllRefArDo@android_emulator@@QEAA@XZ"
        "/INCLUDE:__imp_??0AdbAssistantStats@android_studio@@QEAA@XZ")
endif()
'@
$cmakeTextAfterD3d = [System.IO.File]::ReadAllText($cmake).Replace("`r`n", "`n")
if (!$cmakeTextAfterD3d.Contains("CHIMERA_SDK_IMPORT_LIB_DIR")) {
    Write-TextFile $cmake ($cmakeTextAfterD3d.TrimEnd() + $cmakeSdkImportBlock + "`n")
}
} else {
    Write-Host "legacy_gl_patch=skipped"
}

if ($hasVulkanDisplay) {
    Copy-TextTemplate $vulkanBridgeTemplateHeader $vulkanBridgeHeader "Vulkan display-post bridge header"
    Copy-TextTemplate $vulkanBridgeTemplateCpp $vulkanBridgeCpp "Vulkan display-post bridge source"

    if ($modernVulkanLayout) {
        foreach ($path in @($vulkanBridgeHeader, $vulkanBridgeCpp)) {
            Replace-AllLiteral $path '#include "BorrowedImageVk.h"' '#include "borrowed_image_vk.h"'
            Replace-AllLiteral $path "namespace gfxstream {`nnamespace vk {" "namespace gfxstream {`nnamespace host {`nnamespace vk {"
            Replace-AllLiteral $path "}  // namespace vk`n}  // namespace gfxstream" "}  // namespace vk`n}  // namespace host`n}  // namespace gfxstream"
        }
    }

    $displayVkIncludeNeedles = @(
        '#include "host-common/logging.h"',
        '#include "gfxstream/common/logging.h"'
    )
    $displayVkIncludeReplacement = if ($modernVulkanLayout) {
        '#include "gfxstream/common/logging.h"' + "`n" + '#include "vulkan/ChimeraGfxstreamVulkanSharedTextureBridge.h"'
    } else {
        '#include "host-common/logging.h"' + "`n" + '#include "vulkan/ChimeraGfxstreamVulkanSharedTextureBridge.h"'
    }
    Replace-FirstAvailable $displayVk $displayVkIncludeNeedles $displayVkIncludeReplacement "DisplayVk Chimera bridge include"

    $displayVkNoSurfaceNeedle = @'
    const auto* surface = getBoundSurface();
    if (!surface) {
        ERR("Trying to present to non-existing surface!");
        return PostResult{
            .success = true,
            .postCompletedWaitable = completedFuture,
        };
    }

    if (m_needToRecreateSwapChain) {
'@
    $displayVkNoSurfaceReplacement = @'
    const auto* surface = getBoundSurface();
    if (!surface && !ChimeraGfxstreamVulkanSharedTextureBridge::get().isEnabled()) {
        ERR("Trying to present to non-existing surface!");
        return PostResult{
            .success = true,
            .postCompletedWaitable = completedFuture,
        };
    }

    if (surface && m_needToRecreateSwapChain) {
'@
    $displayVkNoSurfaceModernNeedle = @'
    const auto* surface = getBoundSurface();
    if (!surface) {
        GFXSTREAM_ERROR("Trying to present to non-existing surface!");
        return PostResult{
            .success = true,
            .postCompletedWaitable = completedFuture,
        };
    }

    if (m_needToRecreateSwapChain) {
'@
    $displayVkNoSurfaceModernReplacement = @'
    const auto* surface = getBoundSurface();
    if (!surface && !ChimeraGfxstreamVulkanSharedTextureBridge::get().isEnabled()) {
        GFXSTREAM_ERROR("Trying to present to non-existing surface!");
        return PostResult{
            .success = true,
            .postCompletedWaitable = completedFuture,
        };
    }

    if (surface && m_needToRecreateSwapChain) {
'@
    $displayVkText = [System.IO.File]::ReadAllText($displayVk).Replace("`r`n", "`n")
    if ($displayVkText.Contains($displayVkNoSurfaceReplacement.Replace("`r`n", "`n")) -or
        $displayVkText.Contains($displayVkNoSurfaceModernReplacement.Replace("`r`n", "`n"))) {
        # Already patched.
    } elseif ($displayVkText.Contains($displayVkNoSurfaceNeedle.Replace("`r`n", "`n"))) {
        Replace-Once $displayVk $displayVkNoSurfaceNeedle $displayVkNoSurfaceReplacement "DisplayVk headless Chimera post gate"
    } else {
        Replace-Once $displayVk $displayVkNoSurfaceModernNeedle $displayVkNoSurfaceModernReplacement "DisplayVk headless Chimera post gate"
    }

    if ($modernVulkanLayout) {
        Replace-Once $displayVk @'
    if (useBlit) {
        // Use vkCmdBlitImage to post the image (single image optimized path)
'@ @'
    bool chimeraSharedTextureRecorded = false;
    if (useBlit) {
        // Use vkCmdBlitImage to post the image (single image optimized path)
'@ "DisplayVk Chimera Vulkan copy flag"

        $displayVkModernBlitNeedle = @'
        m_vk.vkCmdBlitImage(cmdBuff, sourceImageInfoVk->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            currentSwapchainImage, currentSwapchainLayout, 1, &region, filter);
    } else {
'@
        $displayVkModernBlitReplacement = @'
        m_vk.vkCmdBlitImage(cmdBuff, sourceImageInfoVk->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            currentSwapchainImage, currentSwapchainLayout, 1, &region, filter);
        chimeraSharedTextureRecorded =
            ChimeraGfxstreamVulkanSharedTextureBridge::get().recordCopy(
                m_vk, m_vkPhysicalDevice, m_vkDevice, cmdBuff, *sourceImageInfoVk,
                swapchainImageExtent, filter);
    } else {
'@
        Replace-Once $displayVk $displayVkModernBlitNeedle $displayVkModernBlitReplacement "DisplayVk Vulkan display-post copy hook"
    } else {
    $displayVkHeadlessNeedle = @'
    const auto* surface = getBoundSurface();
    if (!m_swapChainStateVk || !surface) {
        ERR("Cannot post ColorBuffer: No surface bound.");
        return PostResult{true, std::move(completedFuture)};
    }

'@
    $displayVkHeadlessReplacement = @'
    const auto* surface = getBoundSurface();
    if (!m_swapChainStateVk || !surface) {
        bool chimeraHeadlessPublished = false;
        if (ChimeraGfxstreamVulkanSharedTextureBridge::get().isEnabled()) {
            VkCommandBuffer cmdBuff = VK_NULL_HANDLE;
            const VkCommandBufferAllocateInfo allocInfo = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = m_vkCommandPool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            };
            VK_CHECK(m_vk.vkAllocateCommandBuffers(m_vkDevice, &allocInfo, &cmdBuff));

            const VkCommandBufferBeginInfo beginInfo = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            };
            VK_CHECK(m_vk.vkBeginCommandBuffer(cmdBuff, &beginInfo));
            const VkExtent2D chimeraHeadlessExtent = {
                sourceImageInfoVk->imageCreateInfo.extent.width,
                sourceImageInfoVk->imageCreateInfo.extent.height,
            };
            const bool chimeraSharedTextureRecorded =
                ChimeraGfxstreamVulkanSharedTextureBridge::get().recordCopy(
                    m_vk, m_vkPhysicalDevice, m_vkDevice, cmdBuff, *sourceImageInfoVk,
                    chimeraHeadlessExtent, VK_FILTER_NEAREST);
            VK_CHECK(m_vk.vkEndCommandBuffer(cmdBuff));

            if (chimeraSharedTextureRecorded) {
                VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
                VkFence fence = VK_NULL_HANDLE;
                VK_CHECK(m_vk.vkCreateFence(m_vkDevice, &fenceInfo, nullptr, &fence));
                VkSubmitInfo submitInfo = {
                    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                    .commandBufferCount = 1,
                    .pCommandBuffers = &cmdBuff,
                };
                {
                    android::base::AutoLock lock(*m_compositorVkQueueLock);
                    VK_CHECK(m_vk.vkQueueSubmit(m_compositorVkQueue, 1, &submitInfo, fence));
                }
                VkResult res =
                    m_vk.vkWaitForFences(m_vkDevice, 1, &fence, VK_TRUE,
                                         kVkWaitForFencesTimeoutNsecs);
                if (res == VK_TIMEOUT) {
                    res = m_vk.vkWaitForFences(m_vkDevice, 1, &fence, VK_TRUE,
                                               kVkWaitForFencesTimeoutNsecs);
                }
                VK_CHECK(res);
                m_vk.vkDestroyFence(m_vkDevice, fence, nullptr);
                ChimeraGfxstreamVulkanSharedTextureBridge::get().publishFrame(
                    chimeraHeadlessExtent);
                chimeraHeadlessPublished = true;
            }
            m_vk.vkFreeCommandBuffers(m_vkDevice, m_vkCommandPool, 1, &cmdBuff);
        }
        if (chimeraHeadlessPublished) {
            return PostResult{true, std::move(completedFuture)};
        }
        ERR("Cannot post ColorBuffer: No surface bound.");
        return PostResult{true, std::move(completedFuture)};
    }

'@
    $displayVkText = [System.IO.File]::ReadAllText($displayVk).Replace("`r`n", "`n")
    if ($displayVkText.Contains($displayVkHeadlessReplacement.Replace("`r`n", "`n"))) {
        # Already patched.
    } else {
        Replace-Once $displayVk $displayVkHeadlessNeedle $displayVkHeadlessReplacement "DisplayVk headless shared texture copy hook"
    }

    $displayVkBlitNeedle = @'
    m_vk.vkCmdBlitImage(cmdBuff, sourceImageInfoVk->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        m_swapChainStateVk->getVkImages()[imageIndex],
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, filter);

    VkImageMemoryBarrier releaseSwapchainImageBarrier = {
'@
    $displayVkBlitReplacement = @'
    m_vk.vkCmdBlitImage(cmdBuff, sourceImageInfoVk->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        m_swapChainStateVk->getVkImages()[imageIndex],
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, filter);
    const bool chimeraSharedTextureRecorded =
        ChimeraGfxstreamVulkanSharedTextureBridge::get().recordCopy(
        m_vk, m_vkPhysicalDevice, m_vkDevice, cmdBuff, *sourceImageInfoVk, swapchainImageExtent,
        filter);

    VkImageMemoryBarrier releaseSwapchainImageBarrier = {
'@
    $displayVkOldBlitReplacement = @'
    m_vk.vkCmdBlitImage(cmdBuff, sourceImageInfoVk->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        m_swapChainStateVk->getVkImages()[imageIndex],
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, filter);
    ChimeraGfxstreamVulkanSharedTextureBridge::get().recordCopy(
        m_vk, m_vkPhysicalDevice, m_vkDevice, cmdBuff, *sourceImageInfoVk, swapchainImageExtent,
        filter);

    VkImageMemoryBarrier releaseSwapchainImageBarrier = {
'@
    $displayVkText = [System.IO.File]::ReadAllText($displayVk).Replace("`r`n", "`n")
    if ($displayVkText.Contains($displayVkOldBlitReplacement.Replace("`r`n", "`n"))) {
        Replace-AllLiteral $displayVk $displayVkOldBlitReplacement $displayVkBlitReplacement
    } else {
        Replace-Once $displayVk $displayVkBlitNeedle $displayVkBlitReplacement "DisplayVk Vulkan display-post copy hook"
    }
    }

    if ($modernVulkanLayout) {
        $displayVkModernFenceNeedle = @'
    std::shared_future<std::shared_ptr<PostResource>> postResourceFuture =
        std::async(std::launch::deferred, [postCompleteFence, postResource, this,
                                           imResources]() mutable {
            VkResult res = m_vk.vkWaitForFences(m_vkDevice, 1, &postCompleteFence, VK_TRUE,
                                                kVkWaitForFencesTimeoutNsecs);
            if (res == VK_TIMEOUT) {
                // Retry. If device lost, hopefully this returns immediately.
                res = m_vk.vkWaitForFences(m_vkDevice, 1, &postCompleteFence, VK_TRUE,
                                           kVkWaitForFencesTimeoutNsecs);
            }
            VK_CHECK(res);

            // This should always be waited even in failure
            if (imResources) {
                m_compositorVk->releaseImmediateModeResources(imResources);
            }
            return postResource;
        }).share();
'@
        $displayVkModernFenceReplacement = @'
    std::shared_future<std::shared_ptr<PostResource>> postResourceFuture =
        std::async(std::launch::deferred,
                   [postCompleteFence, postResource, this, imResources,
                    chimeraSharedTextureRecorded, swapchainImageExtent]() mutable {
                       VkResult res = m_vk.vkWaitForFences(m_vkDevice, 1, &postCompleteFence,
                                                           VK_TRUE, kVkWaitForFencesTimeoutNsecs);
                       if (res == VK_TIMEOUT) {
                           // Retry. If device lost, hopefully this returns immediately.
                           res = m_vk.vkWaitForFences(m_vkDevice, 1, &postCompleteFence, VK_TRUE,
                                                      kVkWaitForFencesTimeoutNsecs);
                       }
                       VK_CHECK(res);

                       // This should always be waited even in failure
                       if (imResources) {
                           m_compositorVk->releaseImmediateModeResources(imResources);
                       }
                       if (chimeraSharedTextureRecorded) {
                           ChimeraGfxstreamVulkanSharedTextureBridge::get().publishFrame(
                               swapchainImageExtent);
                       }
                       return postResource;
                   })
            .share();
'@
        Replace-Once $displayVk $displayVkModernFenceNeedle $displayVkModernFenceReplacement "DisplayVk publish Chimera frame after fence"
    } else {
    $displayVkFenceNeedle = @'
    std::shared_future<std::shared_ptr<PostResource>> postResourceFuture =
        std::async(std::launch::deferred, [postCompleteFence, postResource, this]() mutable {
            VkResult res = m_vk.vkWaitForFences(m_vkDevice, 1, &postCompleteFence, VK_TRUE,
                                                kVkWaitForFencesTimeoutNsecs);
            if (res == VK_SUCCESS) {
                return postResource;
            }
            if (res == VK_TIMEOUT) {
                // Retry. If device lost, hopefully this returns immediately.
                res = m_vk.vkWaitForFences(m_vkDevice, 1, &postCompleteFence, VK_TRUE,
                                           kVkWaitForFencesTimeoutNsecs);
            }
            VK_CHECK(res);
            return postResource;
        }).share();
'@
    $displayVkFenceReplacement = @'
    std::shared_future<std::shared_ptr<PostResource>> postResourceFuture =
        std::async(std::launch::deferred,
                   [postCompleteFence, postResource, this, chimeraSharedTextureRecorded,
                    swapchainImageExtent]() mutable {
                       VkResult res = m_vk.vkWaitForFences(m_vkDevice, 1, &postCompleteFence,
                                                           VK_TRUE, kVkWaitForFencesTimeoutNsecs);
                       if (res == VK_TIMEOUT) {
                           // Retry. If device lost, hopefully this returns immediately.
                           res = m_vk.vkWaitForFences(m_vkDevice, 1, &postCompleteFence, VK_TRUE,
                                                      kVkWaitForFencesTimeoutNsecs);
                       }
                       VK_CHECK(res);
                       if (chimeraSharedTextureRecorded) {
                           ChimeraGfxstreamVulkanSharedTextureBridge::get().publishFrame(
                               swapchainImageExtent);
                       }
                       return postResource;
                   })
            .share();
'@
    Replace-Once $displayVk $displayVkFenceNeedle $displayVkFenceReplacement "DisplayVk publish Chimera frame after fence"
    }

    $vulkanCmakeText = [System.IO.File]::ReadAllText($vulkanCmake).Replace("`r`n", "`n")
    $vulkanCmakeText = [regex]::Replace(
        $vulkanCmakeText,
        "(?m)^\s*ChimeraGfxstreamVulkanSharedTextureBridge\.cpp\s*\n",
        "")
    $vulkanDisplaySource = if ($modernVulkanLayout) { "display_vk.cpp" } else { "DisplayVk.cpp" }
    $vulkanDisplayNeedle = "            $vulkanDisplaySource`n"
    if (!$vulkanCmakeText.Contains($vulkanDisplayNeedle)) {
        throw "Cannot patch gfxstream Vulkan bridge source list; expected $vulkanDisplaySource in $vulkanCmake"
    }
    $vulkanCmakeText = $vulkanCmakeText.Replace(
        $vulkanDisplayNeedle,
        "            $vulkanDisplaySource`n            ChimeraGfxstreamVulkanSharedTextureBridge.cpp`n")
    Write-TextFile $vulkanCmake $vulkanCmakeText

    $vulkanWin32Needle = @'
if (WIN32)
target_compile_definitions(gfxstream-vulkan-server PRIVATE -DVK_USE_PLATFORM_WIN32_KHR)
'@
    $vulkanWin32Replacement = @'
if (WIN32)
target_compile_definitions(gfxstream-vulkan-server PRIVATE -DVK_USE_PLATFORM_WIN32_KHR)
target_link_libraries(gfxstream-vulkan-server PRIVATE D3d11.lib Dxgi.lib)
'@
    Replace-Once $vulkanCmake $vulkanWin32Needle $vulkanWin32Replacement "gfxstream Vulkan D3D11 link libraries"
}

$gfxstreamRootCmake = Join-Path $root "CMakeLists.txt"
if (Test-Path -LiteralPath $gfxstreamRootCmake) {
    Replace-Once $gfxstreamRootCmake "set(CMAKE_CXX_STANDARD 17)" "set(CMAKE_CXX_STANDARD 20)" "gfxstream C++20 standard"
    Replace-Once $gfxstreamRootCmake `
        "    add_compile_definitions(_CRT_NONSTDC_NO_DEPRECATE)`nendif()" `
        "    add_compile_definitions(_CRT_NONSTDC_NO_DEPRECATE)`n    add_compile_options(/FS)`n    add_compile_definitions(__PRETTY_FUNCTION__=__FUNCSIG__)`nendif()" `
        "gfxstream MSVC shared PDB compile option"
    Replace-Once $gfxstreamRootCmake `
        "    add_compile_options(/FS)`nendif()" `
        "    add_compile_options(/FS)`n    add_compile_definitions(__PRETTY_FUNCTION__=__FUNCSIG__)`nendif()" `
        "gfxstream MSVC pretty function compatibility"
}

$gfxstreamBackendWarningsNeedle = @'
# Suppress some warnings generated by platform/aemu repo
target_compile_options(
    gfxstream_backend_static
    PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wno-missing-field-initializers
    -Wno-unused-parameter
    -Wno-unused-private-field
    -Wno-return-type-c-linkage
    -Wno-extern-c-compat
    -DGFXSTREAM_ENABLE_HOST_GLES=1
    )
'@
$gfxstreamBackendWarningsReplacement = @'
# Suppress some warnings generated by platform/aemu repo
target_compile_options(
    gfxstream_backend_static
    PRIVATE
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-missing-field-initializers>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-unused-parameter>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-unused-private-field>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-return-type-c-linkage>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-extern-c-compat>"
    )
target_compile_definitions(gfxstream_backend_static PRIVATE GFXSTREAM_ENABLE_HOST_GLES=1)
'@
$gfxstreamBackendWarningsOldNeedle = @'
# Suppress some warnings generated by platform/aemu repo
target_compile_options(
    gfxstream_backend_static
    PRIVATE
    -Wno-invalid-offsetof
    -Wno-free-nonheap-object
    -Wno-attributes
    -DGFXSTREAM_ENABLE_HOST_GLES=1
    )
'@
$gfxstreamBackendWarningsOldReplacement = @'
# Suppress some warnings generated by platform/aemu repo
target_compile_options(
    gfxstream_backend_static
    PRIVATE
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-invalid-offsetof>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-free-nonheap-object>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-attributes>"
    )
target_compile_definitions(gfxstream_backend_static PRIVATE GFXSTREAM_ENABLE_HOST_GLES=1)
'@
$gfxstreamBackendWarningsModernNeedle = @'
# Suppress some warnings
target_compile_options(
    gfxstream_backend_static
    PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wno-missing-field-initializers
    -Wno-unused-parameter
    -Wno-unused-private-field
    -Wno-return-type-c-linkage
    -Wno-extern-c-compat
    -DGFXSTREAM_ENABLE_HOST_GLES=1
    )
'@
$gfxstreamBackendWarningsModernReplacement = @'
# Suppress some warnings
target_compile_options(
    gfxstream_backend_static
    PRIVATE
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-missing-field-initializers>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-unused-parameter>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-unused-private-field>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-return-type-c-linkage>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-extern-c-compat>"
    )
target_compile_definitions(gfxstream_backend_static PRIVATE GFXSTREAM_ENABLE_HOST_GLES=1)
'@
$cmakeText = [System.IO.File]::ReadAllText($cmake).Replace("`r`n", "`n")
if ($cmakeText.Contains($gfxstreamBackendWarningsReplacement.Replace("`r`n", "`n")) -or
    $cmakeText.Contains($gfxstreamBackendWarningsOldReplacement.Replace("`r`n", "`n")) -or
    $cmakeText.Contains($gfxstreamBackendWarningsModernReplacement.Replace("`r`n", "`n"))) {
    # Already patched.
} elseif ($cmakeText.Contains($gfxstreamBackendWarningsNeedle.Replace("`r`n", "`n"))) {
    Replace-Once $cmake $gfxstreamBackendWarningsNeedle $gfxstreamBackendWarningsReplacement "gfxstream backend MSVC warning options"
} elseif ($cmakeText.Contains($gfxstreamBackendWarningsModernNeedle.Replace("`r`n", "`n"))) {
    Replace-Once $cmake $gfxstreamBackendWarningsModernNeedle $gfxstreamBackendWarningsModernReplacement "gfxstream backend MSVC warning options"
} else {
    if ($cmakeText.Contains($gfxstreamBackendWarningsOldNeedle.Replace("`r`n", "`n"))) {
        Replace-Once $cmake $gfxstreamBackendWarningsOldNeedle $gfxstreamBackendWarningsOldReplacement "gfxstream backend MSVC warning options"
    } else {
        Write-Host "gfxstream_backend_warning_patch=skipped"
    }
}

$vulkanCmake = Join-Path $hostDir "vulkan\CMakeLists.txt"
if (Test-Path -LiteralPath $vulkanCmake) {
    $vulkanSourceWarningsNeedle = @'
set_source_files_properties(VkDecoder.cpp PROPERTIES COMPILE_FLAGS -Wno-unused-variable)
set_source_files_properties(VkSubDecoder.cpp PROPERTIES COMPILE_FLAGS -Wno-unused-variable)
set_source_files_properties(VkDecoderSnapshot.cpp PROPERTIES COMPILE_FLAGS -Wno-unused-variable)
'@
    $vulkanSourceWarningsReplacement = @'
if (NOT MSVC)
    set_source_files_properties(VkDecoder.cpp PROPERTIES COMPILE_FLAGS -Wno-unused-variable)
    set_source_files_properties(VkSubDecoder.cpp PROPERTIES COMPILE_FLAGS -Wno-unused-variable)
    set_source_files_properties(VkDecoderSnapshot.cpp PROPERTIES COMPILE_FLAGS -Wno-unused-variable)
endif()
'@
    $vulkanSourceWarningsOldNeedle = 'set_source_files_properties(VkDecoder.cpp PROPERTIES COMPILE_FLAGS -Wno-unused-variable)'
    $vulkanSourceWarningsOldReplacement = @'
if (NOT MSVC)
    set_source_files_properties(VkDecoder.cpp PROPERTIES COMPILE_FLAGS -Wno-unused-variable)
endif()
'@
    $vulkanSourceWarningsModernNeedle = @'
set_source_files_properties(vk_decoder.cpp PROPERTIES COMPILE_FLAGS -Wno-unused-variable)
set_source_files_properties(vk_sub_decoder.cpp PROPERTIES COMPILE_FLAGS -Wno-unused-variable)
set_source_files_properties(vk_decoder_snapshot.cpp PROPERTIES COMPILE_FLAGS -Wno-unused-variable)
'@
    $vulkanSourceWarningsModernReplacement = @'
if (NOT MSVC)
    set_source_files_properties(vk_decoder.cpp PROPERTIES COMPILE_FLAGS -Wno-unused-variable)
    set_source_files_properties(vk_sub_decoder.cpp PROPERTIES COMPILE_FLAGS -Wno-unused-variable)
    set_source_files_properties(vk_decoder_snapshot.cpp PROPERTIES COMPILE_FLAGS -Wno-unused-variable)
endif()
'@
    $vulkanCmakeText = [System.IO.File]::ReadAllText($vulkanCmake).Replace("`r`n", "`n")
    if ($vulkanCmakeText.Contains($vulkanSourceWarningsReplacement.Replace("`r`n", "`n")) -or
        $vulkanCmakeText.Contains($vulkanSourceWarningsOldReplacement.Replace("`r`n", "`n")) -or
        $vulkanCmakeText.Contains($vulkanSourceWarningsModernReplacement.Replace("`r`n", "`n"))) {
        # Already patched.
    } elseif ($vulkanCmakeText.Contains($vulkanSourceWarningsNeedle.Replace("`r`n", "`n"))) {
        Replace-Once $vulkanCmake $vulkanSourceWarningsNeedle $vulkanSourceWarningsReplacement "gfxstream vulkan source MSVC warning options"
    } elseif ($vulkanCmakeText.Contains($vulkanSourceWarningsModernNeedle.Replace("`r`n", "`n"))) {
        Replace-Once $vulkanCmake $vulkanSourceWarningsModernNeedle $vulkanSourceWarningsModernReplacement "gfxstream vulkan source MSVC warning options"
    } else {
        if ($vulkanCmakeText.Contains($vulkanSourceWarningsOldNeedle.Replace("`r`n", "`n"))) {
            Replace-Once $vulkanCmake $vulkanSourceWarningsOldNeedle $vulkanSourceWarningsOldReplacement "gfxstream vulkan source MSVC warning options"
        } else {
            Write-Host "gfxstream_vulkan_source_warning_patch=skipped"
        }
    }

    $vulkanTargetWarningsNeedle = @'
target_compile_options(gfxstream-vulkan-server
    PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wno-missing-field-initializers
    -Wno-unused-parameter
    -Wno-unused-private-field
    -Wno-return-type-c-linkage
    -Wno-extern-c-compat
    )
'@
    $vulkanTargetWarningsReplacement = @'
target_compile_options(gfxstream-vulkan-server
    PRIVATE
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-missing-field-initializers>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-unused-parameter>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-unused-private-field>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-return-type-c-linkage>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-extern-c-compat>"
    )
'@
    $vulkanTargetWarningsOldNeedle = 'target_compile_options(gfxstream-vulkan-server PRIVATE -Wno-unused-value -Wno-return-type -Wno-return-type-c-linkage)'
    $vulkanTargetWarningsOldReplacement = 'target_compile_options(gfxstream-vulkan-server PRIVATE "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-unused-value>" "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-return-type>" "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-return-type-c-linkage>")'
    $vulkanCmakeText = [System.IO.File]::ReadAllText($vulkanCmake).Replace("`r`n", "`n")
    if ($vulkanCmakeText.Contains($vulkanTargetWarningsReplacement.Replace("`r`n", "`n")) -or
        $vulkanCmakeText.Contains($vulkanTargetWarningsOldReplacement.Replace("`r`n", "`n"))) {
        # Already patched.
    } elseif ($vulkanCmakeText.Contains($vulkanTargetWarningsNeedle.Replace("`r`n", "`n"))) {
        Replace-Once $vulkanCmake $vulkanTargetWarningsNeedle $vulkanTargetWarningsReplacement "gfxstream vulkan MSVC warning options"
    } else {
        Replace-Once $vulkanCmake $vulkanTargetWarningsOldNeedle $vulkanTargetWarningsOldReplacement "gfxstream vulkan MSVC warning options"
    }

    $vkDecoderGlobalState = @(
        (Join-Path $hostDir "vulkan\VkDecoderGlobalState.cpp"),
        (Join-Path $hostDir "vulkan\vk_decoder_global_state.cpp")
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if (![string]::IsNullOrWhiteSpace($vkDecoderGlobalState) -and (Test-Path -LiteralPath $vkDecoderGlobalState)) {
        Replace-AllLiteral $vkDecoderGlobalState 'importSource.c_str() ?: ""' 'importSource.c_str()'
        Replace-AllLiteral $vkDecoderGlobalState '.c_str() ?: ""' '.c_str()'
        Replace-AllLiteral $vkDecoderGlobalState 'reinterpret_cast<uint32_t>(instanceInfo.contextId)' 'static_cast<uint32_t>(instanceInfo.contextId)'
    }
}

$aemuRootCmake = Join-Path $aospRoot "hardware\google\aemu\CMakeLists.txt"
if (Test-Path -LiteralPath $aemuRootCmake) {
    $aemuGlobalFlagsNeedle = 'set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-extern-c-compat -Wno-return-type-c-linkage -D_FILE_OFFSET_BITS=64")'
    $aemuGlobalFlagsReplacement = @'
if (MSVC)
    add_compile_definitions(_FILE_OFFSET_BITS=64)
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-extern-c-compat -Wno-return-type-c-linkage -D_FILE_OFFSET_BITS=64")
endif()
'@
    Replace-Once $aemuRootCmake $aemuGlobalFlagsNeedle $aemuGlobalFlagsReplacement "AEMU root MSVC global flags"
    Replace-Once $aemuRootCmake "set(CMAKE_CXX_STANDARD 17)" "set(CMAKE_CXX_STANDARD 20)" "AEMU C++20 standard"
}

$aemuCompilerHeader = Join-Path $aospRoot "hardware\google\aemu\base\include\aemu\base\Compiler.h"
if (Test-Path -LiteralPath $aemuCompilerHeader) {
    $compilerCompatNeedle = '#pragma once'
    $compilerCompatReplacement = @'
#pragma once

#if defined(_MSC_VER) && !defined(__clang__)
#ifndef __builtin_expect
#define __builtin_expect(exp, value) (exp)
#endif
#ifndef __typeof__
#define __typeof__(exp) decltype(exp)
#endif
#ifndef __attribute__
#define __attribute__(x)
#endif
#endif
'@
    Replace-Once $aemuCompilerHeader $compilerCompatNeedle $compilerCompatReplacement "AEMU MSVC compiler compatibility shim"
}

$aemuMsvcHeader = Join-Path $aospRoot "hardware\google\aemu\base\include\aemu\base\msvc.h"
if (Test-Path -LiteralPath $aemuMsvcHeader) {
    $msvcCompatNeedle = '#include <windows.h>'
    $msvcCompatReplacement = @'
#include <windows.h>

#ifdef __cplusplus
#include <intrin.h>
#include <type_traits>
#ifndef __ATOMIC_RELAXED
#define __ATOMIC_RELAXED 0
#endif
#ifndef __ATOMIC_ACQUIRE
#define __ATOMIC_ACQUIRE 2
#endif
#ifndef __ATOMIC_RELEASE
#define __ATOMIC_RELEASE 3
#endif
#ifndef __ATOMIC_SEQ_CST
#define __ATOMIC_SEQ_CST 5
#endif
#ifndef __typeof__
#define __typeof__(exp) decltype(exp)
#endif
#ifndef __atomic_load_n
#define __atomic_load_n(ptr, order) (*reinterpret_cast<volatile std::remove_reference_t<decltype(*(ptr))>*>(ptr))
#endif
#ifndef __atomic_store_n
#define __atomic_store_n(ptr, value, order) do { \
    *reinterpret_cast<volatile std::remove_reference_t<decltype(*(ptr))>*>(ptr) = static_cast<std::remove_reference_t<decltype(*(ptr))>>(value); \
    _ReadWriteBarrier(); \
} while (0)
#endif
#ifndef __atomic_load
#define __atomic_load(ptr, ret, order) do { \
    *(ret) = __atomic_load_n((ptr), (order)); \
} while (0)
#endif
#ifndef __atomic_add_fetch
#define __atomic_add_fetch(ptr, value, order) \
    (static_cast<std::remove_reference_t<decltype(*(ptr))>>(_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(ptr), static_cast<long>(value)) + static_cast<long>(value)))
#endif
#ifndef __atomic_sub_fetch
#define __atomic_sub_fetch(ptr, value, order) \
    (static_cast<std::remove_reference_t<decltype(*(ptr))>>(_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(ptr), -static_cast<long>(value)) - static_cast<long>(value)))
#endif
#ifndef __atomic_fetch_add
#define __atomic_fetch_add(ptr, value, order) \
    (static_cast<std::remove_reference_t<decltype(*(ptr))>>(_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(ptr), static_cast<long>(value))))
#endif
#ifndef __atomic_fetch_sub
#define __atomic_fetch_sub(ptr, value, order) \
    (static_cast<std::remove_reference_t<decltype(*(ptr))>>(_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(ptr), -static_cast<long>(value))))
#endif
#ifndef __atomic_compare_exchange_n
#define __atomic_compare_exchange_n(ptr, expected, desired, weak, success_order, failure_order) \
    ([&]() -> bool { \
        const auto previous = static_cast<std::remove_reference_t<decltype(*(ptr))>>(_InterlockedCompareExchange( \
            reinterpret_cast<volatile long*>(ptr), \
            static_cast<long>(desired), \
            static_cast<long>(*(expected)))); \
        if (previous == *(expected)) return true; \
        *(expected) = previous; \
        return false; \
    }())
#endif
#endif
'@
    Replace-Once $aemuMsvcHeader $msvcCompatNeedle $msvcCompatReplacement "AEMU MSVC GNU atomic compatibility shim"
    Replace-AllLiteral $aemuMsvcHeader "#include <intrin.h>`n#ifndef __ATOMIC_RELAXED" "#include <intrin.h>`n#include <type_traits>`n#ifndef __ATOMIC_RELAXED"
    Replace-AllLiteral $aemuMsvcHeader "__typeof__(*(ptr))" "std::remove_reference_t<decltype(*(ptr))>"
}

$ringBufferHeader = Join-Path $aospRoot "hardware\google\aemu\base\include\aemu\base\ring_buffer.h"
if (Test-Path -LiteralPath $ringBufferHeader) {
    Replace-Once $ringBufferHeader `
        '#include "aemu/base/c_header.h"' `
        "#include `"aemu/base/c_header.h`"`n#ifdef _MSC_VER`n#include `"aemu/base/msvc.h`"`n#endif" `
        "AEMU ring buffer MSVC compatibility include"
}

$aemuCLogHeader = Join-Path $aospRoot "hardware\google\aemu\base\include\aemu\base\logging\CLog.h"
if (Test-Path -LiteralPath $aemuCLogHeader) {
    $clogApiNeedle = @'
#ifdef _MSC_VER
#ifdef LOGGING_API_SHARED
#define LOGGING_API __declspec(dllexport)
#else
#define LOGGING_API __declspec(dllimport)
#endif
#else
#define LOGGING_API
#endif
'@
    $clogApiReplacement = @'
#ifdef _MSC_VER
#ifdef LOGGING_API_SHARED
#define LOGGING_API __declspec(dllexport)
#else
#define LOGGING_API
#endif
#else
#define LOGGING_API
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define AEMU_PRINTF_FORMAT(fmt_index, first_arg)
#else
#define AEMU_PRINTF_FORMAT(fmt_index, first_arg) __attribute__((format(printf, fmt_index, first_arg)))
#endif
'@
    Replace-Once $aemuCLogHeader $clogApiNeedle $clogApiReplacement "AEMU CLog MSVC API and format attributes"
    Replace-Once $aemuCLogHeader `
        "LOGGING_API void __emu_log_print(LogSeverity prio, const char* file, int line, const char* fmt, ...)`n    __attribute__((format(printf, 4, 5)));" `
        "LOGGING_API void __emu_log_print(LogSeverity prio, const char* file, int line, const char* fmt, ...)`n    AEMU_PRINTF_FORMAT(4, 5);" `
        "AEMU CLog printf format attribute"
}

$asgTypesHeader = Join-Path $aospRoot "hardware\google\aemu\host-common\include\host-common\address_space_graphics_types.h"
if (Test-Path -LiteralPath $asgTypesHeader) {
    Replace-Once $asgTypesHeader `
        "struct __attribute__((__packed__)) asg_type1_xfer {" `
        "#if defined(_MSC_VER) && !defined(__clang__)`n#pragma pack(push, 1)`nstruct asg_type1_xfer {`n#else`nstruct __attribute__((__packed__)) asg_type1_xfer {`n#endif" `
        "AEMU ASG packed type1 struct"
    Replace-Once $asgTypesHeader `
        "struct __attribute__((__packed__)) asg_type2_xfer {" `
        "struct asg_type2_xfer {" `
        "AEMU ASG packed type2 struct"
    Replace-Once $asgTypesHeader `
        "    uint64_t size;`n};" `
        "    uint64_t size;`n};`n#if defined(_MSC_VER) && !defined(__clang__)`n#pragma pack(pop)`n#endif" `
        "AEMU ASG packed struct pop"
}

$aemuWindowsCmake = Join-Path $aospRoot "hardware\google\aemu\windows\CMakeLists.txt"
if (Test-Path -LiteralPath $aemuWindowsCmake) {
    $aemuWarningNeedle = @'
target_compile_options(
  msvc-posix-compat
  PUBLIC "-Wno-macro-redefined" "-Wno-deprecated-declarations" # A lot of the
                                                               # POSIX names are
                                                               # deprecated..
)
'@
    $aemuWarningReplacement = @'
target_compile_options(
  msvc-posix-compat
  PUBLIC
    "$<$<CXX_COMPILER_ID:MSVC>:/wd4005>"
    "$<$<CXX_COMPILER_ID:MSVC>:/wd4996>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-macro-redefined>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-deprecated-declarations>"
)
'@
    Replace-Once $aemuWindowsCmake $aemuWarningNeedle $aemuWarningReplacement "AEMU MSVC warning options"
}

$aemuIncludeDir = Join-Path $aospRoot "hardware\google\aemu\windows\includes"
if (Test-Path -LiteralPath $aemuIncludeDir) {
    $ucrtInclude = Find-UcrtIncludePath
    if ([string]::IsNullOrWhiteSpace($ucrtInclude)) {
        throw "Cannot locate Windows SDK UCRT include directory for AEMU MSVC include_next patch"
    }
    $msvcInclude = Find-MsvcIncludePath
    if ([string]::IsNullOrWhiteSpace($msvcInclude)) {
        throw "Cannot locate MSVC include directory for AEMU MSVC include_next patch"
    }
    $includeNextReplacements = @(
        @{ Path = "stdlib.h"; Header = "stdlib.h"; IncludeRoot = $ucrtInclude; Extra = "" },
        @{ Path = "time.h"; Header = "time.h"; IncludeRoot = $ucrtInclude; Extra = "" },
        @{ Path = "limits.h"; Header = "limits.h"; IncludeRoot = $msvcInclude; Extra = "" },
        @{ Path = "fcntl.h"; Header = "fcntl.h"; IncludeRoot = $ucrtInclude; Extra = "" },
        @{ Path = "sys\stat.h"; Header = "sys/stat.h"; IncludeRoot = $ucrtInclude; Extra = "" },
        @{ Path = "sys\types.h"; Header = "sys/types.h"; IncludeRoot = $ucrtInclude; Extra = "" }
    )
    foreach ($entry in $includeNextReplacements) {
        $path = Join-Path $aemuIncludeDir $entry.Path
        if (Test-Path -LiteralPath $path) {
            $replacement = "#if defined(_MSC_VER) && !defined(__clang__)`n#include `"$($entry.IncludeRoot)/$($entry.Header)`"`n$($entry.Extra)#else`n#include_next <$($entry.Header)>`n#endif"
            Replace-AemuIncludeNextBlock $path $entry.Header $replacement "AEMU include_next $($entry.Path)"
        }
    }
}

$aemuPread = Join-Path $aospRoot "hardware\google\aemu\windows\src\pread.cpp"
if (Test-Path -LiteralPath $aemuPread) {
    $preadNeedle = @'
    DWORD cRead;
    OVERLAPPED overlapped = {.OffsetHigh = (DWORD)((offset & 0xFFFFFFFF00000000LL) >> 32),
                             .Offset = (DWORD)(offset & 0xFFFFFFFFLL)};
    bool rd = ReadFile(handle, buf, count, &cRead, &overlapped);
'@
    $preadReplacement = @'
    DWORD cRead = 0;
    OVERLAPPED overlapped = {};
    overlapped.OffsetHigh = (DWORD)((offset & 0xFFFFFFFF00000000LL) >> 32);
    overlapped.Offset = (DWORD)(offset & 0xFFFFFFFFLL);
    bool rd = ReadFile(handle, buf, static_cast<DWORD>(count), &cRead, &overlapped);
'@
    Replace-Once $aemuPread $preadNeedle $preadReplacement "AEMU pread C++17 compatibility"
}

$aemuFatalError = Join-Path $aospRoot "hardware\google\aemu\host-common\GfxstreamFatalError.cpp"
if (Test-Path -LiteralPath $aemuFatalError) {
    $fatalNeedle = @'
    CreateMetricsLogger()->logMetricEvent(GfxstreamVkAbort{.file = mFile,
                                                           .function = mFunction,
                                                           .msg = mOss.str().c_str(),
                                                           .line = mLine,
                                                           .abort_reason = mReason.getAbortCode()});
'@
    $fatalReplacement = @'
    GfxstreamVkAbort abortMetric{};
    abortMetric.file = mFile;
    abortMetric.function = mFunction;
    abortMetric.msg = mOss.str().c_str();
    abortMetric.line = mLine;
    abortMetric.abort_reason = mReason.getAbortCode();
    CreateMetricsLogger()->logMetricEvent(abortMetric);
'@
    Replace-Once $aemuFatalError $fatalNeedle $fatalReplacement "AEMU fatal error C++17 metric initializer"
}

$aemuCrashHandler = Join-Path $aospRoot "hardware\google\aemu\host-common\include\host-common\crash-handler.h"
if (Test-Path -LiteralPath $aemuCrashHandler) {
    Replace-Once $aemuCrashHandler `
        "ANDROID_NORETURN void crashhandler_die(const char* message)`n    __attribute__((noinline));" `
        "ANDROID_NORETURN void crashhandler_die(const char* message);" `
        "AEMU crash handler MSVC noinline attribute"
}

$aemuAddressSpaceGraphics = Join-Path $aospRoot "hardware\google\aemu\host-common\address_space_graphics.cpp"
if (Test-Path -LiteralPath $aemuAddressSpaceGraphics) {
    $asgCallbacksNeedle = @'
    : mConsumerCallbacks((ConsumerCallbacks){
          [this] { return onUnavailableRead(); },
          [](uint64_t physAddr) { return (char*)sGlobals()->controlOps()->get_host_ptr(physAddr); },
      }),
'@
    $asgCallbacksReplacement = @'
    : mConsumerCallbacks([this]() {
          ConsumerCallbacks callbacks{};
          callbacks.onUnavailableRead = [this] { return onUnavailableRead(); };
          callbacks.getPtr = [](uint64_t physAddr) {
              return (char*)sGlobals()->controlOps()->get_host_ptr(physAddr);
          };
          return callbacks;
      }()),
'@
    Replace-Once $aemuAddressSpaceGraphics $asgCallbacksNeedle $asgCallbacksReplacement "AEMU address space graphics MSVC callback initialization"
}

$aemuTracing = Join-Path $aospRoot "hardware\google\aemu\base\Tracing.cpp"
if (Test-Path -LiteralPath $aemuTracing) {
    Replace-Once $aemuTracing `
        '#include "aemu/base/Tracing.h"' `
        "#include `"aemu/base/Tracing.h`"`n#include `"aemu/base/Compiler.h`"" `
        "AEMU tracing MSVC compiler compatibility include"
}

$protocolUtils = Join-Path $hostDir "apigen-codec-common\ProtocolUtils.h"
if (Test-Path -LiteralPath $protocolUtils) {
    Replace-Once $protocolUtils `
        "    char __attribute__((__aligned__(Align))) mArray[StackSize];" `
        "    alignas(Align) char mArray[StackSize];" `
        "ProtocolUtils MSVC input buffer alignment"
    Replace-Once $protocolUtils `
        "    unsigned char __attribute__((__aligned__(Align))) mArray[StackSize];" `
        "    alignas(Align) unsigned char mArray[StackSize];" `
        "ProtocolUtils MSVC output buffer alignment"
}

$decoderProtocolUtils = Join-Path $hostDir "decoder_common\include\gfxstream\host\ProtocolUtils.h"
if (Test-Path -LiteralPath $decoderProtocolUtils) {
    Replace-AllLiteral $decoderProtocolUtils `
        "    char __attribute__((__aligned__(Align))) mArray[StackSize];" `
        "    alignas(Align) char mArray[StackSize];"
    Replace-AllLiteral $decoderProtocolUtils `
        "    unsigned char __attribute__((__aligned__(Align))) mArray[StackSize];" `
        "    alignas(Align) unsigned char mArray[StackSize];"
}

$stalePtrRegistry = @(
    (Join-Path $hostDir "StalePtrRegistry.h"),
    (Join-Path $hostDir "stale_ptr_registry.h")
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (![string]::IsNullOrWhiteSpace($stalePtrRegistry) -and (Test-Path -LiteralPath $stalePtrRegistry)) {
    Replace-AllLiteral $stalePtrRegistry `
        "(Entry){ nullptr, Staleness::PrevSnapshot }" `
        "Entry{ nullptr, Staleness::PrevSnapshot }"
}

$virtioGpuResourceHeader = Join-Path $hostDir "VirtioGpuResource.h"
if (Test-Path -LiteralPath $virtioGpuResourceHeader) {
    Replace-AllLiteral $virtioGpuResourceHeader `
        "        VirtioGpuContextId contextId = -1;" `
        "        VirtioGpuContextId contextId = static_cast<VirtioGpuContextId>(-1);"
}

$virtioGpuRenderer = @(
    (Join-Path $hostDir "virtio-gpu-gfxstream-renderer.cpp"),
    (Join-Path $hostDir "virtio_gpu_gfxstream_renderer.cpp")
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (![string]::IsNullOrWhiteSpace($virtioGpuRenderer) -and (Test-Path -LiteralPath $virtioGpuRenderer)) {
    $rendererText = [System.IO.File]::ReadAllText($virtioGpuRenderer).Replace("`r`n", "`n")
    if (!$rendererText.Contains("gfxstream_backend_set_screen_background")) {
        $screenBackgroundNeedle = @'
VG_EXPORT void gfxstream_backend_set_screen_mask(int width, int height,
                                                 const unsigned char* rgbaData) {
    android_setOpenglesScreenMask(width, height, rgbaData);
}
'@
        $screenBackgroundReplacement = @'
VG_EXPORT void gfxstream_backend_set_screen_mask(int width, int height,
                                                 const unsigned char* rgbaData) {
    android_setOpenglesScreenMask(width, height, rgbaData);
}

// SDK Emulator 36.5.11 looks up this optional skin/background ABI even when
// Chimera runs headless. Older emu-36 source trees do not implement it; keep a
// no-op export so the modified backend stays ABI-compatible without adding any
// display-window dependency or CPU readback path.
VG_EXPORT void gfxstream_backend_set_screen_background(int width, int height,
                                                       const unsigned char* rgbaData) {
    (void)width;
    (void)height;
    (void)rgbaData;
}
'@
        Replace-Once $virtioGpuRenderer `
            $screenBackgroundNeedle `
            $screenBackgroundReplacement `
            "gfxstream SDK 36 screen background ABI export"
    }
    Replace-Once $virtioGpuRenderer `
        "static_assert(sizeof(struct stream_renderer_vulkan_info) == 36,`n              `"stream_renderer_vulkan_info must be 36 bytes`");" `
        "using stream_renderer_vulkan_info_struct = struct stream_renderer_vulkan_info;`nstatic_assert(sizeof(stream_renderer_vulkan_info_struct) == 36,`n              `"stream_renderer_vulkan_info must be 36 bytes`");" `
        "virtio gpu renderer MSVC vulkan info alias"
    Replace-AllLiteral $virtioGpuRenderer `
        "offsetof(struct stream_renderer_vulkan_info," `
        "offsetof(stream_renderer_vulkan_info_struct,"
}

$renderApiHeader = @(
    (Join-Path $root "include\render-utils\render_api.h"),
    (Join-Path $hostDir "include\render-utils\render_api.h")
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (Test-Path -LiteralPath $renderApiHeader) {
    Replace-AllLiteral $renderApiHeader `
        "#ifndef USING_ANDROID_BP`nANDROID_BEGIN_HEADER`n#endif" `
        "#if !defined(USING_ANDROID_BP) && !(defined(_MSC_VER) && defined(__cplusplus))`nANDROID_BEGIN_HEADER`n#endif"
    Replace-AllLiteral $renderApiHeader `
        "#ifndef USING_ANDROID_BP`nANDROID_END_HEADER`n#endif" `
        "#if !defined(USING_ANDROID_BP) && !(defined(_MSC_VER) && defined(__cplusplus))`nANDROID_END_HEADER`n#endif"
    Replace-AllLiteral $renderApiHeader `
        "#ifndef USING_ANDROID_BP`n#ifdef __cplusplus`nextern `"C`" {`n#endif`n#endif" `
        "#if !defined(USING_ANDROID_BP) && !(defined(_MSC_VER) && defined(__cplusplus))`n#ifdef __cplusplus`nextern `"C`" {`n#endif`n#endif"
    Replace-AllLiteral $renderApiHeader `
        "#ifndef USING_ANDROID_BP`n#ifdef __cplusplus`n} // extern `"C`"`n#endif`n#endif" `
        "#if !defined(USING_ANDROID_BP) && !(defined(_MSC_VER) && defined(__cplusplus))`n#ifdef __cplusplus`n} // extern `"C`"`n#endif`n#endif"
    $renderApiBeginNeedle = @'
#ifndef USING_ANDROID_BP
#ifdef __cplusplus
extern "C" {
#endif
#endif

namespace gfxstream {
'@
    $renderApiBeginReplacement = @'
#if !defined(USING_ANDROID_BP) && !(defined(_MSC_VER) && defined(__cplusplus))
#ifdef __cplusplus
extern "C" {
#endif
#endif

namespace gfxstream {
'@
    $renderApiEndNeedle = @'
}  // namespace gfxstream

#ifndef USING_ANDROID_BP
#ifdef __cplusplus
} // extern "C"
#endif
#endif
'@
    $renderApiEndReplacement = @'
}  // namespace gfxstream

#if !defined(USING_ANDROID_BP) && !(defined(_MSC_VER) && defined(__cplusplus))
#ifdef __cplusplus
} // extern "C"
#endif
#endif
'@
    Replace-AllLiteral $renderApiHeader $renderApiBeginNeedle $renderApiBeginReplacement
    Replace-AllLiteral $renderApiHeader $renderApiEndNeedle $renderApiEndReplacement
}

$renderApiCpp = Join-Path $hostDir "render_api.cpp"
if (Test-Path -LiteralPath $renderApiCpp) {
    $renderApiText = [System.IO.File]::ReadAllText($renderApiCpp).Replace("`r`n", "`n")
    $renderApiExportPragma = '#pragma comment(linker, "/EXPORT:initLibrary=?initLibrary@gfxstream@@YA?AV?$unique_ptr@VRenderLib@gfxstream@@U?$default_delete@VRenderLib@gfxstream@@@std@@@std@@XZ")'
    if (!$renderApiText.Contains($renderApiExportPragma)) {
        if ($renderApiText.Contains('#include "gfxstream/common/logging.h"')) {
            Replace-Once $renderApiCpp `
                '#include "gfxstream/common/logging.h"' `
                @'
#include "gfxstream/common/logging.h"

#if defined(_MSC_VER)
#pragma comment(linker, "/EXPORT:initLibrary=?initLibrary@gfxstream@@YA?AV?$unique_ptr@VRenderLib@gfxstream@@U?$default_delete@VRenderLib@gfxstream@@@std@@@std@@XZ")
#endif
'@ `
                "render_api MSVC plain initLibrary export alias"
        } elseif ($renderApiText.Contains('#include "host-common/logging.h"')) {
            Replace-Once $renderApiCpp `
                '#include "host-common/logging.h"' `
                @'
#include "host-common/logging.h"

#if defined(_MSC_VER)
#pragma comment(linker, "/EXPORT:initLibrary=?initLibrary@gfxstream@@YA?AV?$unique_ptr@VRenderLib@gfxstream@@U?$default_delete@VRenderLib@gfxstream@@@std@@@std@@XZ")
#endif
'@ `
                "render_api MSVC plain initLibrary export alias"
        } else {
            throw "Cannot patch render_api MSVC plain initLibrary export alias; expected logging include not found in $renderApiCpp"
        }
    }
    $renderApiText = [System.IO.File]::ReadAllText($renderApiCpp).Replace("`r`n", "`n")
    $renderApiAliasNeedle = @'
namespace gfxstream {

RENDER_APICALL RenderLibPtr RENDER_APIENTRY initLibrary() {
    return RenderLibPtr(new RenderLibImpl());
}

}  // namespace gfxstream
'@
    $renderApiAliasReplacement = @'
namespace gfxstream {

RENDER_APICALL RenderLibPtr RENDER_APIENTRY initLibrary() {
    return RenderLibPtr(new RenderLibImpl());
}

}  // namespace gfxstream

// SDK Emulator 36 loads initLibrary by its plain export name. MSVC cannot keep
// the public header's original extern "C" namespace wrapping for a C++ return
// type, so only non-MSVC builds use the C-linkage alias here.
#if !defined(_MSC_VER)
extern "C" RENDER_APICALL gfxstream::RenderLibPtr RENDER_APIENTRY initLibrary() {
    return gfxstream::RenderLibPtr(new gfxstream::RenderLibImpl());
}
#endif
'@
    $renderApiModernNeedle = @'
namespace gfxstream {

RENDER_API_EXPORT RenderLibPtr initLibrary() {
    return RenderLibPtr(new gfxstream::host::RenderLibImpl());
}

}  // namespace gfxstream
'@
    $renderApiModernReplacement = @'
namespace gfxstream {

RENDER_API_EXPORT RenderLibPtr initLibrary() {
    return RenderLibPtr(new gfxstream::host::RenderLibImpl());
}

}  // namespace gfxstream

// SDK Emulator 36 loads initLibrary by its plain export name. MSVC cannot keep
// the public header's original extern "C" namespace wrapping for a C++ return
// type, so only non-MSVC builds use the C-linkage alias here.
#if !defined(_MSC_VER)
extern "C" RENDER_API_EXPORT gfxstream::RenderLibPtr initLibrary() {
    return gfxstream::RenderLibPtr(new gfxstream::host::RenderLibImpl());
}
#endif
'@
    if ($renderApiText.Contains('extern "C" RENDER_API_EXPORT gfxstream::RenderLibPtr initLibrary()') -or
        $renderApiText.Contains('extern "C" RENDER_APICALL gfxstream::RenderLibPtr RENDER_APIENTRY initLibrary()')) {
        # Already patched.
    } elseif ($renderApiText.Contains($renderApiModernNeedle.Replace("`r`n", "`n"))) {
        Replace-Once $renderApiCpp `
            $renderApiModernNeedle `
            $renderApiModernReplacement `
            "render_api SDK plain initLibrary export"
    } else {
        Replace-Once $renderApiCpp `
            $renderApiAliasNeedle `
            $renderApiAliasReplacement `
            "render_api SDK plain initLibrary export"
    }
    Replace-AllLiteral $renderApiCpp `
        "// SDK Emulator 36 loads initLibrary by its plain C export name. MSVC cannot`n// keep the public header's original extern `"C`" namespace wrapping, so provide`n// the stock ABI alias explicitly while preserving the C++ namespaced API.`nextern `"C`" RENDER_API_EXPORT gfxstream::RenderLibPtr initLibrary() {`n    return gfxstream::RenderLibPtr(new gfxstream::host::RenderLibImpl());`n}" `
        "// SDK Emulator 36 loads initLibrary by its plain export name. MSVC cannot keep`n// the public header's original extern `"C`" namespace wrapping for a C++ return`n// type, so only non-MSVC builds use the C-linkage alias here.`n#if !defined(_MSC_VER)`nextern `"C`" RENDER_API_EXPORT gfxstream::RenderLibPtr initLibrary() {`n    return gfxstream::RenderLibPtr(new gfxstream::host::RenderLibImpl());`n}`n#endif"
    Replace-AllLiteral $renderApiCpp `
        "// SDK Emulator 36 loads initLibrary by its plain C export name. MSVC cannot`n// keep the public header's original extern `"C`" namespace wrapping, so provide`n// the stock ABI alias explicitly while preserving the C++ namespaced API.`nextern `"C`" RENDER_APICALL gfxstream::RenderLibPtr RENDER_APIENTRY initLibrary() {`n    return gfxstream::RenderLibPtr(new gfxstream::RenderLibImpl());`n}" `
        "// SDK Emulator 36 loads initLibrary by its plain export name. MSVC cannot keep`n// the public header's original extern `"C`" namespace wrapping for a C++ return`n// type, so only non-MSVC builds use the C-linkage alias here.`n#if !defined(_MSC_VER)`nextern `"C`" RENDER_APICALL gfxstream::RenderLibPtr RENDER_APIENTRY initLibrary() {`n    return gfxstream::RenderLibPtr(new gfxstream::RenderLibImpl());`n}`n#endif"
}

$magmaDefs = Join-Path $root "third-party\fuchsia\magma\include\lib\magma\magma_common_defs.h"
if (Test-Path -LiteralPath $magmaDefs) {
    Replace-AllLiteral $magmaDefs "} __attribute__((__aligned__(8))) magma_exec_command_buffer_t;" "} magma_exec_command_buffer_t;"
    Replace-AllLiteral $magmaDefs "} __attribute__((__aligned__(8))) magma_command_descriptor_t;" "} magma_command_descriptor_t;"
}

$baseWindowsCmake = Join-Path $root "common\base\windows\CMakeLists.txt"
if (Test-Path -LiteralPath $baseWindowsCmake) {
    $baseWindowsWarningsNeedle = @'
# Msvc redefines macro's to inject compatibility.
target_compile_options(
    gfxstream_common_base_windows_compat
    PUBLIC
    "-Wno-macro-redefined"
    "-Wno-deprecated-declarations" # A lot of the POSIX names are deprecated...
    )
'@
    $baseWindowsWarningsReplacement = @'
# Msvc redefines macro's to inject compatibility.
target_compile_options(
    gfxstream_common_base_windows_compat
    PUBLIC
    "$<$<C_COMPILER_ID:MSVC>:/wd4005>"
    "$<$<CXX_COMPILER_ID:MSVC>:/wd4005>"
    "$<$<C_COMPILER_ID:MSVC>:/wd4996>"
    "$<$<CXX_COMPILER_ID:MSVC>:/wd4996>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-macro-redefined>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-deprecated-declarations>"
    )
'@
    $baseWindowsText = [System.IO.File]::ReadAllText($baseWindowsCmake).Replace("`r`n", "`n")
    if ($baseWindowsText.Contains($baseWindowsWarningsNeedle.Replace("`r`n", "`n")) -or
        $baseWindowsText.Contains($baseWindowsWarningsReplacement.Replace("`r`n", "`n"))) {
        Replace-Once $baseWindowsCmake $baseWindowsWarningsNeedle $baseWindowsWarningsReplacement "common base windows MSVC warning options"
    }
}

$gfxstreamBaseWindowsIncludeDir = Join-Path $root "common\base\windows\includes"
if (Test-Path -LiteralPath $gfxstreamBaseWindowsIncludeDir) {
    $ucrtInclude = Find-UcrtIncludePath
    if ([string]::IsNullOrWhiteSpace($ucrtInclude)) {
        throw "Cannot locate Windows SDK UCRT include directory for gfxstream common base include_next patch"
    }
    $msvcInclude = Find-MsvcIncludePath
    if ([string]::IsNullOrWhiteSpace($msvcInclude)) {
        throw "Cannot locate MSVC include directory for gfxstream common base include_next patch"
    }
    $baseIncludeNextReplacements = @(
        @{ Path = "stdlib.h"; Header = "stdlib.h"; IncludeRoot = $ucrtInclude; Extra = "" },
        @{ Path = "time.h"; Header = "time.h"; IncludeRoot = $ucrtInclude; Extra = "" },
        @{ Path = "limits.h"; Header = "limits.h"; IncludeRoot = $msvcInclude; Extra = "#include <Windows.h>`n" },
        @{ Path = "fcntl.h"; Header = "fcntl.h"; IncludeRoot = $ucrtInclude; Extra = "" },
        @{ Path = "sys\stat.h"; Header = "sys/stat.h"; IncludeRoot = $ucrtInclude; Extra = "" },
        @{ Path = "sys\types.h"; Header = "sys/types.h"; IncludeRoot = $ucrtInclude; Extra = "" }
    )
    foreach ($entry in $baseIncludeNextReplacements) {
        $path = Join-Path $gfxstreamBaseWindowsIncludeDir $entry.Path
        if (Test-Path -LiteralPath $path) {
            $replacement = "#if defined(_MSC_VER) && !defined(__clang__)`n#include `"$($entry.IncludeRoot)/$($entry.Header)`"`n$($entry.Extra)#else`n#include_next <$($entry.Header)>`n#endif"
            Replace-AemuIncludeNextBlock $path $entry.Header $replacement "gfxstream common base include_next $($entry.Path)"
            if ($entry.Path -eq "limits.h") {
                Replace-AllLiteral $path "#include <Windows.h>`n#else" "#else"
            }
        }
    }
}

$gfxstreamBaseTimeCpp = Join-Path $root "common\base\windows\src\time.cpp"
if (Test-Path -LiteralPath $gfxstreamBaseTimeCpp) {
    Replace-Once $gfxstreamBaseTimeCpp @'
#include <Windows.h>
#include <sys/time.h>
'@ @'
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <Windows.h>
#include <sys/time.h>
'@ "gfxstream common base time WinSock2 include order"
}

$gfxstreamCompilerHeader = Join-Path $root "common\base\include\gfxstream\Compiler.h"
if (Test-Path -LiteralPath $gfxstreamCompilerHeader) {
    Replace-Once $gfxstreamCompilerHeader '#pragma once' @'
#pragma once

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#include <type_traits>
#ifndef __builtin_expect
#define __builtin_expect(exp, value) (exp)
#endif
#ifndef __attribute__
#define __attribute__(x)
#endif
#ifndef __ATOMIC_RELAXED
#define __ATOMIC_RELAXED 0
#endif
#ifndef __ATOMIC_ACQUIRE
#define __ATOMIC_ACQUIRE 2
#endif
#ifndef __ATOMIC_RELEASE
#define __ATOMIC_RELEASE 3
#endif
#ifndef __ATOMIC_SEQ_CST
#define __ATOMIC_SEQ_CST 5
#endif
#ifndef __atomic_load_n
#define __atomic_load_n(ptr, order) (*reinterpret_cast<volatile std::remove_reference_t<decltype(*(ptr))>*>(ptr))
#endif
#ifndef __atomic_store_n
#define __atomic_store_n(ptr, value, order) do { \
    *reinterpret_cast<volatile std::remove_reference_t<decltype(*(ptr))>*>(ptr) = static_cast<std::remove_reference_t<decltype(*(ptr))>>(value); \
    _ReadWriteBarrier(); \
} while (0)
#endif
#ifndef __atomic_load
#define __atomic_load(ptr, ret, order) do { \
    *(ret) = __atomic_load_n((ptr), (order)); \
} while (0)
#endif
#ifndef __atomic_add_fetch
#define __atomic_add_fetch(ptr, value, order) \
    (static_cast<std::remove_reference_t<decltype(*(ptr))>>(_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(ptr), static_cast<long>(value)) + static_cast<long>(value)))
#endif
#ifndef __atomic_sub_fetch
#define __atomic_sub_fetch(ptr, value, order) \
    (static_cast<std::remove_reference_t<decltype(*(ptr))>>(_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(ptr), -static_cast<long>(value)) - static_cast<long>(value)))
#endif
#ifndef __atomic_fetch_add
#define __atomic_fetch_add(ptr, value, order) \
    (static_cast<std::remove_reference_t<decltype(*(ptr))>>(_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(ptr), static_cast<long>(value))))
#endif
#ifndef __atomic_fetch_sub
#define __atomic_fetch_sub(ptr, value, order) \
    (static_cast<std::remove_reference_t<decltype(*(ptr))>>(_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(ptr), -static_cast<long>(value))))
#endif
#ifndef __atomic_compare_exchange_n
#define __atomic_compare_exchange_n(ptr, expected, desired, weak, success_order, failure_order) \
    ([&]() -> bool { \
        auto previous = __atomic_load_n((ptr), (failure_order)); \
        if (previous == *(expected)) { \
            __atomic_store_n((ptr), (desired), (success_order)); \
            return true; \
        } \
        *(expected) = previous; \
        return false; \
    }())
#endif
#endif
'@ "gfxstream common compiler MSVC compatibility shim"
}

$gfxstreamMsvcHeader = Join-Path $root "common\base\include\gfxstream\msvc.h"
if (Test-Path -LiteralPath $gfxstreamMsvcHeader) {
    Replace-Once $gfxstreamMsvcHeader '#pragma once' "#pragma once`n`n#include `"gfxstream/Compiler.h`"" "gfxstream msvc compiler compatibility include"
}

$gfxstreamTracingCpp = Join-Path $root "common\base\Tracing.cpp"
if (Test-Path -LiteralPath $gfxstreamTracingCpp) {
    Replace-Once $gfxstreamTracingCpp '#include "gfxstream/Tracing.h"' @'
#include "gfxstream/Tracing.h"
#include "gfxstream/Compiler.h"
'@ "gfxstream tracing compiler compatibility include"
}

$ringStreamCpp = Join-Path $hostDir "ring_stream.cpp"
if (Test-Path -LiteralPath $ringStreamCpp) {
    Replace-Once $ringStreamCpp `
        '#include "ring_stream.h"' `
        "#include `"ring_stream.h`"`n#include `"gfxstream/Compiler.h`"" `
        "ring_stream MSVC atomic compatibility include"
}

$gfxstreamMacrosHeader = Join-Path $root "common\base\include\gfxstream\Macros.h"
if (Test-Path -LiteralPath $gfxstreamMacrosHeader) {
    Replace-AllLiteral $gfxstreamMacrosHeader `
        "#define ALIGN(x, a) ALIGN_MASK(x, (__typeof__(x))(a) - 1)" `
        "#if defined(_MSC_VER) && !defined(__clang__)`n#define ALIGN(x, a) ALIGN_MASK(x, (decltype(x))(a) - 1)`n#else`n#define ALIGN(x, a) ALIGN_MASK(x, (__typeof__(x))(a) - 1)`n#endif"
}

$gfxstreamAddressSpaceTypes = Join-Path $root "host\address_space\include\gfxstream\host\address_space_graphics_types.h"
if (Test-Path -LiteralPath $gfxstreamAddressSpaceTypes) {
    Replace-Once $gfxstreamAddressSpaceTypes `
        "struct __attribute__((__packed__)) asg_type1_xfer {" `
        "#if defined(_MSC_VER) && !defined(__clang__)`n#pragma pack(push, 1)`nstruct asg_type1_xfer {`n#else`nstruct __attribute__((__packed__)) asg_type1_xfer {`n#endif" `
        "gfxstream address space packed type1 struct"
    Replace-Once $gfxstreamAddressSpaceTypes `
        "struct __attribute__((__packed__)) asg_type2_xfer {" `
        "struct asg_type2_xfer {" `
        "gfxstream address space packed type2 struct"
    Replace-Once $gfxstreamAddressSpaceTypes `
        "    uint64_t size;`n};" `
        "    uint64_t size;`n};`n#if defined(_MSC_VER) && !defined(__clang__)`n#pragma pack(pop)`n#endif" `
        "gfxstream address space packed struct pop"
}

$frameBufferHeader = @(
    (Join-Path $hostDir "FrameBuffer.h"),
    (Join-Path $hostDir "frame_buffer.h")
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (![string]::IsNullOrWhiteSpace($frameBufferHeader) -and (Test-Path -LiteralPath $frameBufferHeader)) {
    $frameBufferHeaderText = [System.IO.File]::ReadAllText($frameBufferHeader).Replace("`r`n", "`n")
    if ($frameBufferHeaderText.Contains("using gfxstream::base::EventNotificationSupport<FrameBufferChangeEvent>::fireEvent;") -or
        $frameBufferHeaderText.Contains("using android::base::EventNotificationSupport<FrameBufferChangeEvent>::fireEvent;")) {
        # Already patched.
    } elseif ($frameBufferHeaderText.Contains("class FrameBuffer : public gfxstream::base::EventNotificationSupport<FrameBufferChangeEvent> {`n   public:")) {
        Replace-Once $frameBufferHeader `
            "class FrameBuffer : public gfxstream::base::EventNotificationSupport<FrameBufferChangeEvent> {`n   public:" `
            "class FrameBuffer : public gfxstream::base::EventNotificationSupport<FrameBufferChangeEvent> {`n   public:`n    using gfxstream::base::EventNotificationSupport<FrameBufferChangeEvent>::fireEvent;" `
            "FrameBuffer public fireEvent forwarding"
    } else {
        Replace-Once $frameBufferHeader `
            "class FrameBuffer : public android::base::EventNotificationSupport<FrameBufferChangeEvent> {`n   public:" `
            "class FrameBuffer : public android::base::EventNotificationSupport<FrameBufferChangeEvent> {`n   public:`n    using android::base::EventNotificationSupport<FrameBufferChangeEvent>::fireEvent;" `
        "FrameBuffer public fireEvent forwarding"
    }
}

$gfxstreamAddressSpaceGraphics = Join-Path $root "host\address_space\address_space_graphics.cpp"
if (Test-Path -LiteralPath $gfxstreamAddressSpaceGraphics) {
    $asgConsumerCreateInfoNeedle = @'
        const AsgConsumerCreateInfo& consumerCreateInfo = {
            .version = mVersion,
            .ring_storage = mRingAllocation.buffer,
            .buffer = mBufferAllocation.buffer,
            .buffer_size = kAsgWriteBufferSize,
            .buffer_flush_interval = kAsgWriteStepSize,
            .callbacks = mConsumerCallbacks,
            .virtioGpuContextId = mVirtioGpuInfo ?
                std::optional<uint32_t>(mVirtioGpuInfo->contextId) :
                std::nullopt,
            .virtioGpuContextName = mVirtioGpuInfo ?
                std::optional<std::string>(mVirtioGpuInfo->name) :
                std::nullopt,
            .virtioGpuCapsetId = mVirtioGpuInfo ?
                std::optional<uint32_t>(mVirtioGpuInfo->capsetId) :
                std::nullopt,
        };
'@
    $asgConsumerCreateInfoReplacement = @'
        AsgConsumerCreateInfo consumerCreateInfo{};
        consumerCreateInfo.version = mVersion;
        consumerCreateInfo.ring_storage = mRingAllocation.buffer;
        consumerCreateInfo.buffer = mBufferAllocation.buffer;
        consumerCreateInfo.buffer_size = kAsgWriteBufferSize;
        consumerCreateInfo.buffer_flush_interval = kAsgWriteStepSize;
        consumerCreateInfo.callbacks = mConsumerCallbacks;
        consumerCreateInfo.virtioGpuContextId = mVirtioGpuInfo ?
            std::optional<uint32_t>(mVirtioGpuInfo->contextId) :
            std::nullopt;
        consumerCreateInfo.virtioGpuContextName = mVirtioGpuInfo ?
            std::optional<std::string>(mVirtioGpuInfo->name) :
            std::nullopt;
        consumerCreateInfo.virtioGpuCapsetId = mVirtioGpuInfo ?
            std::optional<uint32_t>(mVirtioGpuInfo->capsetId) :
            std::nullopt;
'@
    Replace-AllLiteral $gfxstreamAddressSpaceGraphics $asgConsumerCreateInfoNeedle $asgConsumerCreateInfoReplacement
}

$checksumHeader = Join-Path $root "host\decoder_common\include\gfxstream\host\ChecksumCalculator.h"
if (Test-Path -LiteralPath $checksumHeader) {
    Replace-Once $checksumHeader `
        "#define LOG_CHECKSUMHELPER(x...) fprintf(stderr, x)" `
        "#define LOG_CHECKSUMHELPER(...) fprintf(stderr, __VA_ARGS__)" `
        "checksum helper MSVC variadic macro"
    Replace-AllLiteral $checksumHeader `
        "#else`n#define LOG_CHECKSUMHELPER(x...)`n#endif" `
        "#else`n#define LOG_CHECKSUMHELPER(...)`n#endif"
}

$glesV2Cmake = @(
    (Join-Path $glDir "glestranslator\GLES_V2\CMakeLists.txt"),
    (Join-Path $glDir "glestranslator\gles_v2\CMakeLists.txt")
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (![string]::IsNullOrWhiteSpace($glesV2Cmake) -and (Test-Path -LiteralPath $glesV2Cmake)) {
    $glesV2WarningReplacement = '# Chimera: removed clang-only -Wno-macro-redefined for MSVC generator compatibility'
    $glesV2Text = [System.IO.File]::ReadAllText($glesV2Cmake).Replace("`r`n", "`n")
    $glesV2Needle = "target_compile_options(GLES_V2_translator_static PRIVATE -Wno-macro-redefined)"
    if ($glesV2Text.Contains($glesV2Needle) -or $glesV2Text.Contains($glesV2WarningReplacement.Replace("`r`n", "`n"))) {
        Replace-Once $glesV2Cmake $glesV2Needle $glesV2WarningReplacement "GLES_V2 MSVC warning options"
    } else {
        Write-Host "gles_v2_warning_patch=skipped"
    }
    Replace-AllLiteral $glesV2Cmake `
        "target_compile_options(`n    GLES_V2_translator_static`n    PRIVATE`n    -Wno-macro-redefined)" `
        $glesV2WarningReplacement
    Replace-AllLiteral $glesV2Cmake `
        "target_compile_options(GLES_V2_translator_static PRIVATE`n    `"$<$<CXX_COMPILER_ID:MSVC>:/wd4005>`"`n    `"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-macro-redefined>`")" `
        $glesV2WarningReplacement
}

$glesCmCmake = @(
    (Join-Path $glDir "glestranslator\GLES_CM\CMakeLists.txt"),
    (Join-Path $glDir "glestranslator\gles_cm\CMakeLists.txt")
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (![string]::IsNullOrWhiteSpace($glesCmCmake) -and (Test-Path -LiteralPath $glesCmCmake)) {
    $glesCmWarningReplacement = '# Chimera: removed clang-only -Wno-macro-redefined for MSVC generator compatibility'
    $glesCmText = [System.IO.File]::ReadAllText($glesCmCmake).Replace("`r`n", "`n")
    $glesCmNeedle = "target_compile_options(GLES_CM_translator_static PRIVATE -Wno-macro-redefined)"
    if ($glesCmText.Contains($glesCmNeedle) -or $glesCmText.Contains($glesCmWarningReplacement.Replace("`r`n", "`n"))) {
        Replace-Once $glesCmCmake $glesCmNeedle $glesCmWarningReplacement "GLES_CM MSVC warning options"
    } else {
        Write-Host "gles_cm_warning_patch=skipped"
    }
    Replace-AllLiteral $glesCmCmake `
        "target_compile_options(`n    GLES_CM_translator_static`n    PRIVATE`n    -Wno-macro-redefined)" `
        $glesCmWarningReplacement
    Replace-AllLiteral $glesCmCmake `
        "target_compile_options(GLES_CM_translator_static PRIVATE`n    `"$<$<CXX_COMPILER_ID:MSVC>:/wd4005>`"`n    `"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-macro-redefined>`")" `
        $glesCmWarningReplacement
}

$vkFormatUtils = Join-Path $vulkanDir "vk_format_utils.h"
if (Test-Path -LiteralPath $vkFormatUtils) {
    $vkFormatMacroNeedle = @'
#define TO_VK_FORMAT_OR_DIE(GFXSTREAM_FORMAT)                             \
    ({                                                                    \
        auto formatOptForVkFormatOrDie = ToVkFormat(GFXSTREAM_FORMAT);    \
        if (!formatOptForVkFormatOrDie) {                                 \
            const std::string formatString = ToString(GFXSTREAM_FORMAT);  \
            GFXSTREAM_FATAL("Unhandled format %s, formatString.c_str()"); \
        };                                                                \
        *formatOptForVkFormatOrDie;                                       \
    })
'@
    $vkFormatMacroReplacement = @'
inline VkFormat ToVkFormatOrDie(GfxstreamFormat format) {
    auto formatOptForVkFormatOrDie = ToVkFormat(format);
    if (!formatOptForVkFormatOrDie) {
        const std::string formatString = ToString(format);
        GFXSTREAM_FATAL("Unhandled format %s", formatString.c_str());
    }
    return *formatOptForVkFormatOrDie;
}

#define TO_VK_FORMAT_OR_DIE(GFXSTREAM_FORMAT) ToVkFormatOrDie(GFXSTREAM_FORMAT)
'@
    Replace-Once $vkFormatUtils $vkFormatMacroNeedle $vkFormatMacroReplacement "vk format MSVC statement expression"
}

$eglCmake = @(
    (Join-Path $glDir "glestranslator\EGL\CMakeLists.txt"),
    (Join-Path $glDir "glestranslator\egl\CMakeLists.txt")
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (![string]::IsNullOrWhiteSpace($eglCmake) -and (Test-Path -LiteralPath $eglCmake)) {
    $eglWarningNeedle = @'
target_compile_options(
    EGL_translator_static
    PRIVATE -Wno-inconsistent-missing-override -Wno-macro-redefined)
'@
    $eglWarningReplacement = @'
target_compile_options(
    EGL_translator_static
    PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/wd4005>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-inconsistent-missing-override>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-macro-redefined>")
'@
    $eglText = [System.IO.File]::ReadAllText($eglCmake).Replace("`r`n", "`n")
    if ($eglText.Contains($eglWarningNeedle.Replace("`r`n", "`n")) -or
        $eglText.Contains($eglWarningReplacement.Replace("`r`n", "`n"))) {
        Replace-Once $eglCmake $eglWarningNeedle $eglWarningReplacement "EGL MSVC warning options"
    } else {
        Write-Host "egl_warning_patch=skipped"
    }
    $eglWarningGenexBlock = @'
target_compile_options(
    EGL_translator_static
    PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/wd4005>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-inconsistent-missing-override>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-macro-redefined>")
'@
    $eglWarningIfBlock = @'
if (MSVC)
    target_compile_options(EGL_translator_static PRIVATE /wd4005)
endif()
'@
    Replace-AllLiteral $eglCmake $eglWarningGenexBlock $eglWarningIfBlock
    Replace-AllLiteral $eglCmake `
        "if (MSVC)`n    target_compile_options(EGL_translator_static PRIVATE /wd4005)`nelse()`n    target_compile_options(EGL_translator_static PRIVATE -Wno-inconsistent-missing-override -Wno-macro-redefined)`nendif()" `
        "if (MSVC)`n    target_compile_options(EGL_translator_static PRIVATE /wd4005)`nendif()"
    Replace-AllLiteral $eglCmake `
        "target_compile_options(EGL_translator_static PRIVATE -Wno-deprecated-declarations)" `
        'target_compile_options(EGL_translator_static PRIVATE "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-deprecated-declarations>")'
    Replace-AllLiteral $eglCmake `
        'target_compile_options(EGL_translator_static PRIVATE "-Wno-deprecated-declarations")' `
        'target_compile_options(EGL_translator_static PRIVATE "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-deprecated-declarations>")'
}

$glCommonCmake = @(
    (Join-Path $glDir "glestranslator\GLcommon\CMakeLists.txt"),
    (Join-Path $glDir "glestranslator\common\CMakeLists.txt")
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (![string]::IsNullOrWhiteSpace($glCommonCmake) -and (Test-Path -LiteralPath $glCommonCmake)) {
    Replace-AllLiteral $glCommonCmake `
        "target_compile_options(GLcommon PUBLIC -Wno-inconsistent-missing-override)" `
        "# Chimera: removed clang-only -Wno-inconsistent-missing-override for MSVC generator compatibility"
    Replace-AllLiteral $glCommonCmake `
        "target_compile_options(GLcommon PUBLIC`n    `"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-inconsistent-missing-override>`")" `
        "# Chimera: removed clang-only -Wno-inconsistent-missing-override for MSVC generator compatibility"
}

$glDispatch = Join-Path $glDir "glestranslator\GLcommon\GLDispatch.cpp"
if (Test-Path -LiteralPath $glDispatch) {
    Replace-AllLiteral $glDispatch "__typeof__(" "decltype("
}
$glDispatchModern = Join-Path $glDir "glestranslator\common\gl_dispatch.cpp"
if (Test-Path -LiteralPath $glDispatchModern) {
    Replace-AllLiteral $glDispatchModern "__typeof__(" "decltype("
}

$nativeGpuInfoWindows = Join-Path $glDir "gl-host-common\opengl\NativeGpuInfo_windows.cpp"
if (Test-Path -LiteralPath $nativeGpuInfoWindows) {
    Replace-Once $nativeGpuInfoWindows `
        "#include <tuple>" `
        "#include <tuple>`n`n#if defined(_MSC_VER) && !defined(__clang__)`nextern `"C`" const char* emuglConfig_get_vulkan_runtime_full_path() {`n    return `"vulkan-1.dll`";`n}`n#endif" `
        "NativeGpuInfo Windows MSVC vulkan runtime fallback"
}

$eglThreadInfo = @(
    (Join-Path $glDir "glestranslator\EGL\EglThreadInfo.h"),
    (Join-Path $glDir "glestranslator\egl\egl_thread_info.h")
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (![string]::IsNullOrWhiteSpace($eglThreadInfo) -and (Test-Path -LiteralPath $eglThreadInfo)) {
    Replace-Once $eglThreadInfo `
        "    static EglThreadInfo* get(void) __attribute__((const));" `
        "    static EglThreadInfo* get(void);" `
        "EglThreadInfo MSVC const attribute"
}

$vkDecoderGlobalState = @(
    (Join-Path $hostDir "vulkan\VkDecoderGlobalState.cpp"),
    (Join-Path $hostDir "vulkan\vk_decoder_global_state.cpp")
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (![string]::IsNullOrWhiteSpace($vkDecoderGlobalState) -and (Test-Path -LiteralPath $vkDecoderGlobalState)) {
    Replace-AllLiteral $vkDecoderGlobalState ".c_str()?:`"`"" ".c_str()"
    Replace-AllLiteral $vkDecoderGlobalState ".c_str() ?: `"`"" ".c_str()"
    Replace-AllLiteral $vkDecoderGlobalState "reinterpret_cast<uint32_t>(instanceInfo.contextId)" "static_cast<uint32_t>(instanceInfo.contextId)"
}

$vulkanCppFiles = Get-ChildItem -LiteralPath $vulkanDir -Filter "*.cpp" -File -ErrorAction SilentlyContinue
foreach ($vulkanCpp in $vulkanCppFiles) {
    Replace-AllLiteral $vulkanCpp.FullName "__typeof__(" "decltype("
}

$glesCmContext = Join-Path $glDir "glestranslator\GLES_CM\GLEScmContext.cpp"
if (Test-Path -LiteralPath $glesCmContext) {
    Replace-FirstAvailable $glesCmContext `
        @("#include <GLES_CM/GLEScmContext.h>`n`n#include <array>`n#include <vector>", "#include <GLES_CM/GLEScmContext.h>", "#include `"GLEScmContext.h`"") `
        "#include `"GLEScmContext.h`"`n`n#include <array>`n#include <vector>" `
        "GLEScmContext vector headers"
    Replace-Once $glesCmContext `
        "        GLfloat texels[getMaxTexUnits()][4*2];`n        memset((void*)texels, 0, getMaxTexUnits()*4*2*sizeof(GLfloat));" `
        "        std::vector<std::array<GLfloat, 8>> texels(static_cast<size_t>(getMaxTexUnits()));" `
        "GLEScmContext MSVC texel storage"
    Replace-Once $glesCmContext `
        "                    gl.glTexCoordPointer(2,GL_FLOAT,0,texels[i]);" `
        "                    gl.glTexCoordPointer(2,GL_FLOAT,0,texels[i].data());" `
        "GLEScmContext MSVC texel pointer"
}

$glUtils = Join-Path $glDir "glestranslator\GLcommon\GLutils.cpp"
if (Test-Path -LiteralPath $glUtils) {
    Replace-Once $glUtils `
        "#include <GLcommon/GLutils.h>" `
        "#include <GLcommon/GLutils.h>`n#ifdef _MSC_VER`n#include `"aemu/base/msvc.h`"`n#endif" `
        "GLutils MSVC GNU atomic compatibility include"
}

$glesExtHeader = Join-Path $root "third_party\opengl\include\GLES\glext.h"
if (Test-Path -LiteralPath $glesExtHeader) {
    Replace-AllLiteral $glesExtHeader `
        "GL_API void GL_APIENTRY *glMapBufferRangeEXT (GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);" `
        "GL_API void * GL_APIENTRY glMapBufferRangeEXT (GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);"
}

$checksumCalculatorHeader = Join-Path $hostDir "apigen-codec-common\ChecksumCalculator.h"
if (Test-Path -LiteralPath $checksumCalculatorHeader) {
    Replace-Once $checksumCalculatorHeader `
        "#if TRACE_CHECKSUMHELPER`n#define LOG_CHECKSUMHELPER(x...) fprintf(stderr, x)`n#else`n#define LOG_CHECKSUMHELPER(x...)`n#endif" `
        "#if TRACE_CHECKSUMHELPER`n#define LOG_CHECKSUMHELPER(...) fprintf(stderr, __VA_ARGS__)`n#else`n#define LOG_CHECKSUMHELPER(...)`n#endif" `
        "ChecksumCalculator MSVC variadic macro"
}

Get-ChildItem -LiteralPath $hostDir -Recurse -File -Include "*.cpp" | ForEach-Object {
    Replace-AllLiteral $_.FullName " __attribute__((unused))" ""
}

Write-Host "Applied Chimera gfxstream shared texture patch to $root"
