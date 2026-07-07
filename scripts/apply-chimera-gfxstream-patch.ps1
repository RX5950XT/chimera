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

function Replace-Text([string]$Path, [string]$Needle, [string]$Replacement, [string]$Description) {
    $text = [System.IO.File]::ReadAllText($Path).Replace("`r`n", "`n")
    $needleLf = $Needle.Replace("`r`n", "`n")
    $replacementLf = $Replacement.Replace("`r`n", "`n")
    if ($text.Contains($replacementLf)) {
        return
    }
    if (!$text.Contains($needleLf)) {
        Write-Host "$Description=skipped"
        return
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

$frameBufferCompositionNeedle = @'
    fb->m_useVulkanComposition = fb->m_features.GuestVulkanOnly.enabled ||
                                 fb->m_features.VulkanNativeSwapchain.enabled;
'@
$frameBufferCompositionReplacement = @'
    fb->m_useVulkanComposition = fb->m_features.GuestVulkanOnly.enabled ||
                                 fb->m_features.VulkanNativeSwapchain.enabled;
    if (android::base::getEnvironmentVariable("CHIMERA_GFXSTREAM_FORCE_VK_COMPOSITION") == "1") {
        fb->m_useVulkanComposition = true;
    }
'@
Replace-Once $frameBuffer `
    $frameBufferCompositionNeedle `
    $frameBufferCompositionReplacement `
    "FrameBuffer Vulkan composition override"

$frameBufferFeatureNeedle = @'
        .useVulkanComposition = fb->m_useVulkanComposition,
        .useVulkanNativeSwapchain = fb->m_features.VulkanNativeSwapchain.enabled,
'@
$frameBufferFeatureReplacement = @'
        .useVulkanComposition = fb->m_useVulkanComposition,
        .useVulkanNativeSwapchain =
            fb->m_features.VulkanNativeSwapchain.enabled ||
            android::base::getEnvironmentVariable("CHIMERA_GFXSTREAM_FORCE_VK_COMPOSITION") == "1",
'@
Replace-Once $frameBuffer `
    $frameBufferFeatureNeedle `
    $frameBufferFeatureReplacement `
    "FrameBuffer Vulkan feature override"

if ($hasVulkanDisplay) {
    Replace-Once $frameBuffer `
        '#include "vulkan/PostWorkerVk.h"' `
        ('#include "vulkan/PostWorkerVk.h"' + "`n" + '#include "vulkan/ChimeraGfxstreamVulkanSharedTextureBridge.h"') `
        "FrameBuffer Vulkan Chimera bridge include"

    $frameBufferD3D11CpuProducerNeedle = 'bool FrameBuffer::Impl::postImplSync(HandleType p_colorbuffer, bool needLockAndBind, bool repaint) {'
    $frameBufferD3D11CpuProducerReplacement = @'
static void chimeraPublishFrameToD3D11Texture(ColorBuffer* cb, int fbW, int fbH) {
    const uint32_t width = std::max(1920u, static_cast<uint32_t>(fbW));
    const uint32_t height = std::max(1080u, static_cast<uint32_t>(fbH));
    static std::vector<uint8_t> s_pixels;
    const size_t bytes = static_cast<size_t>(width) * height * 4u;
    if (s_pixels.size() != bytes) {
        s_pixels.assign(bytes, 0);
    }

    LARGE_INTEGER t0, t1, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    // Prefer a plain readback when the ColorBuffer already matches the shared
    // texture extent: readToBytesScaled() always runs a full-screen resize/rotate
    // blit (m_resizer->update + a second FBO) even at 1:1, adding ~5ms of pure
    // overhead per frame. readToBytes() reads the ColorBuffer's own FBO directly.
    if (cb->getWidth() == width && cb->getHeight() == height) {
        cb->readToBytes(0, 0, static_cast<int>(width), static_cast<int>(height),
                        GfxstreamFormat::R8G8B8A8_UNORM, s_pixels.data(), bytes);
    } else {
        Rect fullRect = {{0, 0}, {static_cast<int>(width), static_cast<int>(height)}};
        cb->readToBytesScaled(static_cast<int>(width), static_cast<int>(height), /*rotation=*/0,
                              fullRect, GfxstreamFormat::R8G8B8A8_UNORM,
                              s_pixels.data(), /*colorTransform=*/{});
    }
    vk::ChimeraGfxstreamVulkanSharedTextureBridge::get().postFrameCpu(
        s_pixels.data(), width, height, width * 4u);

    QueryPerformanceCounter(&t1);
    {
        static int s_logFrameCount = 0;
        static double s_logTotalMs = 0.0;
        const double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart;
        s_logTotalMs += ms;
        if (++s_logFrameCount >= 30) {
            GFXSTREAM_INFO("[chimera] readToD3D11Texture avg=%.1f ms over 30 frames",
                           s_logTotalMs / 30.0);
            s_logFrameCount = 0;
            s_logTotalMs = 0.0;
        }
    }
}

bool FrameBuffer::Impl::postImplSync(HandleType p_colorbuffer, bool needLockAndBind, bool repaint) {
'@
    if ([System.IO.File]::ReadAllText($frameBuffer).Replace("`r`n", "`n") -notmatch 'chimeraPublishFrameToD3D11Texture') {
        $frameBufferText = [System.IO.File]::ReadAllText($frameBuffer).Replace("`r`n", "`n")
        if ($frameBufferText.Contains($frameBufferD3D11CpuProducerNeedle)) {
            Replace-Once $frameBuffer $frameBufferD3D11CpuProducerNeedle $frameBufferD3D11CpuProducerReplacement "FrameBuffer D3D11 CPU producer helper"
        } else {
            $frameBufferD3D11CpuProducerLegacyNeedle = 'bool FrameBuffer::postImplSync(HandleType p_colorbuffer, bool needLockAndBind, bool repaint) {'
            $frameBufferD3D11CpuProducerLegacyReplacement = $frameBufferD3D11CpuProducerReplacement.Replace(
                'bool FrameBuffer::Impl::postImplSync(HandleType p_colorbuffer, bool needLockAndBind, bool repaint) {',
                $frameBufferD3D11CpuProducerLegacyNeedle)
            Replace-Once $frameBuffer $frameBufferD3D11CpuProducerLegacyNeedle $frameBufferD3D11CpuProducerLegacyReplacement "FrameBuffer D3D11 CPU producer helper"
        }
    }

    # Needle variants: (1) original unpatched source, (2) Session 77-82 intermediate (shmem-only)
    $frameBufferHeadlessVkNeedleOrig = @'
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
    $frameBufferHeadlessVkNeedleIntermediate = @'
    colorBuffer->touch();
    if (m_subWin) {
        Post postCmd;
        postCmd.cmd = PostCmd::Post;
        postCmd.cb = colorBuffer.get();
        postCmd.cbHandle = p_colorbuffer;
        postCmd.colorTransform = GetColorTransform();
        postCmd.completionCallback = std::make_unique<Post::CompletionCallback>(callback);
        sendPostWorkerCmd(std::move(postCmd));
        ret = AsyncResult::OK_AND_CALLBACK_SCHEDULED;
    } else {
        // Headless: no sub-window; choose transport based on what's configured
        ret = AsyncResult::OK_AND_CALLBACK_NOT_SCHEDULED;
        chimeraPublishFrameToShmem(colorBuffer.get(), m_framebufferWidth, m_framebufferHeight);
    }
'@
    # Old VkComp variant (sendPostWorkerCmd path) -- intermediate state before MSVCP140 fix
    $frameBufferHeadlessVkNeedleVkCompOld = @'
    colorBuffer->touch();
    const bool chimeraHeadlessVkPost =
        !m_subWin && m_displayVk &&
        vk::ChimeraGfxstreamVulkanSharedTextureBridge::get().isEnabled();
    if (m_subWin || chimeraHeadlessVkPost) {
        Post postCmd;
        postCmd.cmd = PostCmd::Post;
        postCmd.cb = colorBuffer.get();
        postCmd.cbHandle = p_colorbuffer;
        postCmd.colorTransform = GetColorTransform();
        postCmd.completionCallback = std::make_unique<Post::CompletionCallback>(callback);
        sendPostWorkerCmd(std::move(postCmd));
        ret = AsyncResult::OK_AND_CALLBACK_SCHEDULED;
    } else {
        // Headless: no sub-window; choose transport based on what's configured
        ret = AsyncResult::OK_AND_CALLBACK_NOT_SCHEDULED;
        chimeraPublishFrameToShmem(colorBuffer.get(), m_framebufferWidth, m_framebufferHeight);
    }
'@
    # Legacy GLES FrameBuffer.cpp VkComp variant (no colorTransform, no shmem else)
    $frameBufferHeadlessVkNeedleLegacyVkComp = @'
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
    # Old design with m_displayVk->postHeadlessBridge (superseded: m_displayVk is always null headless)
    $frameBufferHeadlessVkNeedleDisplayVkOld = @'
    colorBuffer->touch();
    if (m_subWin) {
        Post postCmd;
        postCmd.cmd = PostCmd::Post;
        postCmd.cb = colorBuffer.get();
        postCmd.cbHandle = p_colorbuffer;
        postCmd.colorTransform = GetColorTransform();
        postCmd.completionCallback = std::make_unique<Post::CompletionCallback>(callback);
        sendPostWorkerCmd(std::move(postCmd));
        ret = AsyncResult::OK_AND_CALLBACK_SCHEDULED;
    } else if (m_displayVk && vk::ChimeraGfxstreamVulkanSharedTextureBridge::get().isEnabled()) {
        // Direct GPU D3D11 bridge path: bypass sendPostWorkerCmd/PostWorkerVk to avoid MSVCP140 ABI crash.
        // Call colorBuffer->borrowForDisplay directly (no FrameBuffer lock re-entry via invalidateColorBufferForVk).
        auto borrowedImg = colorBuffer->borrowForDisplay(ColorBuffer::UsedApi::kVk);
        if (borrowedImg) {
            vk::DisplayVk::PostLayer layer;
            layer.info = borrowedImg.get();
            layer.rotationDegrees = 0.0f;
            layer.displayFrame = {0, 0, (int32_t)m_framebufferWidth, (int32_t)m_framebufferHeight};
            vk::DisplayVk::Post postCmd;
            postCmd.frameWidth = m_framebufferWidth;
            postCmd.frameHeight = m_framebufferHeight;
            postCmd.layers.push_back(std::move(layer));
            m_displayVk->postHeadlessBridge(postCmd);
        }
        ret = AsyncResult::OK_AND_CALLBACK_NOT_SCHEDULED;
    } else {
        // Headless: no sub-window; choose transport based on what's configured
        ret = AsyncResult::OK_AND_CALLBACK_NOT_SCHEDULED;
        chimeraPublishFrameToShmem(colorBuffer.get(), m_framebufferWidth, m_framebufferHeight);
    }
'@
    # Previous replacement (pre GL->VK content sync); kept as a needle so an
    # already-patched tree upgrades in place.
    $frameBufferHeadlessVkNeedlePreGlSync = @'
    colorBuffer->touch();
    if (m_subWin) {
        Post postCmd;
        postCmd.cmd = PostCmd::Post;
        postCmd.cb = colorBuffer.get();
        postCmd.cbHandle = p_colorbuffer;
        postCmd.colorTransform = GetColorTransform();
        postCmd.completionCallback = std::make_unique<Post::CompletionCallback>(callback);
        sendPostWorkerCmd(std::move(postCmd));
        ret = AsyncResult::OK_AND_CALLBACK_SCHEDULED;
    } else {
        // Headless: no sub-window. Try Vulkan GPU path (requires VulkanNativeSwapchain feature
        // so ColorBuffers are Vulkan-backed); fall back to synchronous GL readback otherwise.
        ret = AsyncResult::OK_AND_CALLBACK_NOT_SCHEDULED;
        auto& bridge = vk::ChimeraGfxstreamVulkanSharedTextureBridge::get();
        if (bridge.isEnabled() && bridge.isDirectVkReady()) {
            auto borrowedImg = colorBuffer->borrowForDisplay(ColorBuffer::UsedApi::kVk);
            if (borrowedImg) {
                const auto* vkInfo = static_cast<const vk::BorrowedImageInfoVk*>(borrowedImg.get());
                const uint32_t srcW = vkInfo->imageCreateInfo.extent.width;
                const uint32_t srcH = vkInfo->imageCreateInfo.extent.height;
                const VkExtent2D extent = {std::max(1920u, srcW), std::max(1080u, srcH)};
                bridge.postFrameDirectGpu(*vkInfo, extent);
            } else {
                // kVk borrow failed (GLES-backed): fall back to synchronous GL readback.
                chimeraPublishFrameToD3D11Texture(colorBuffer.get(), m_framebufferWidth, m_framebufferHeight);
            }
        } else {
            chimeraPublishFrameToD3D11Texture(colorBuffer.get(), m_framebufferWidth, m_framebufferHeight);
        }
    }
'@
    # Current design: direct GPU bridge via bridge.postFrameDirectGpu (no DisplayVk/CompositorVk/MSVCP140).
    # CRITICAL: GLES-composited frames (SwiftShader-ES SurfaceFlinger) live in the GL
    # backing; the kVk sibling image is stale until invalidateForVk() syncs GL->VK.
    # Without the sync the blit copies a never-written VK image and the shared
    # texture publishes all-zero frames (host window permanently black while all
    # sequence/FPS counters look healthy). invalidateForVk() is a no-op when the
    # ColorBuffer is VK-backed (mGlTexDirty false) so real Vulkan content stays zero-copy.
    $frameBufferHeadlessVkReplacement = @'
    colorBuffer->touch();
    if (m_subWin) {
        Post postCmd;
        postCmd.cmd = PostCmd::Post;
        postCmd.cb = colorBuffer.get();
        postCmd.cbHandle = p_colorbuffer;
        postCmd.colorTransform = GetColorTransform();
        postCmd.completionCallback = std::make_unique<Post::CompletionCallback>(callback);
        sendPostWorkerCmd(std::move(postCmd));
        ret = AsyncResult::OK_AND_CALLBACK_SCHEDULED;
    } else {
        // Headless: no sub-window. Try Vulkan GPU path (requires VulkanNativeSwapchain feature
        // so ColorBuffers are Vulkan-backed); fall back to synchronous GL readback otherwise.
        ret = AsyncResult::OK_AND_CALLBACK_NOT_SCHEDULED;
        auto& bridge = vk::ChimeraGfxstreamVulkanSharedTextureBridge::get();
        if (bridge.isEnabled() && bridge.isDirectVkReady()) {
            // GLES-composited content lives in the GL backing; sync it into the kVk
            // sibling before borrowing or the blit publishes a never-written image.
            // No-op for VK-backed ColorBuffers (keeps the zero-copy path).
            colorBuffer->invalidateForVk();
            auto borrowedImg = colorBuffer->borrowForDisplay(ColorBuffer::UsedApi::kVk);
            if (borrowedImg) {
                const auto* vkInfo = static_cast<const vk::BorrowedImageInfoVk*>(borrowedImg.get());
                const uint32_t srcW = vkInfo->imageCreateInfo.extent.width;
                const uint32_t srcH = vkInfo->imageCreateInfo.extent.height;
                const VkExtent2D extent = {std::max(1920u, srcW), std::max(1080u, srcH)};
                bridge.postFrameDirectGpu(*vkInfo, extent);
            } else {
                // kVk borrow failed (GLES-backed): fall back to synchronous GL readback.
                chimeraPublishFrameToD3D11Texture(colorBuffer.get(), m_framebufferWidth, m_framebufferHeight);
            }
        } else {
            chimeraPublishFrameToD3D11Texture(colorBuffer.get(), m_framebufferWidth, m_framebufferHeight);
        }
    }
'@
    Replace-FirstAvailable $frameBuffer `
        @($frameBufferHeadlessVkNeedlePreGlSync,
          $frameBufferHeadlessVkNeedleOrig, $frameBufferHeadlessVkNeedleIntermediate,
          $frameBufferHeadlessVkNeedleVkCompOld, $frameBufferHeadlessVkNeedleLegacyVkComp,
          $frameBufferHeadlessVkNeedleDisplayVkOld) `
        $frameBufferHeadlessVkReplacement `
        "FrameBuffer headless Vulkan shared texture post"


    # Throttle stale ColorBuffer miss logs. In long GPU-direct runs gfxstream can repeatedly
    # invalidate an already-destroyed transient ColorBuffer; logging every miss can produce
    # tens of thousands of stderr lines and disturb the 60 FPS producer. Keep first/low-rate
    # diagnostics, but avoid per-frame log I/O.
    Replace-Text $frameBuffer @'
bool FrameBuffer::Impl::invalidateColorBufferForVk(HandleType colorBufferHandle) {
    // It reads contents from GL, which requires a context lock.
    // Also we should not do this in PostWorkerGl, otherwise it will deadlock.
    //
    // b/283524158
    // b/273986739
    AutoLock mutex(m_lock);
    auto colorBuffer = findColorBuffer(colorBufferHandle);
    if (!colorBuffer) {
        GFXSTREAM_ERROR("Failed to find ColorBuffer: %d", colorBufferHandle);
        return false;
    }
    return colorBuffer->invalidateForVk();
}
'@ @'
bool FrameBuffer::Impl::invalidateColorBufferForVk(HandleType colorBufferHandle) {
    // It reads contents from GL, which requires a context lock.
    // Also we should not do this in PostWorkerGl, otherwise it will deadlock.
    //
    // b/283524158
    // b/273986739
    AutoLock mutex(m_lock);
    auto colorBuffer = findColorBuffer(colorBufferHandle);
    if (!colorBuffer) {
        static std::atomic<uint64_t> sMissingInvalidateVkLogs{0};
        const uint64_t logCount = ++sMissingInvalidateVkLogs;
        if (logCount == 1 || logCount == 60 || (logCount % 600) == 0) {
            GFXSTREAM_ERROR("Failed to find ColorBuffer: %d (invalidateVk throttled count=%llu)",
                            colorBufferHandle, (unsigned long long)logCount);
        }
        return false;
    }
    return colorBuffer->invalidateForVk();
}
'@ "FrameBuffer throttle invalidateColorBufferForVk stale ColorBuffer logs"

    Replace-Text $frameBuffer @'
bool FrameBuffer::Impl::invalidateColorBufferForGl(HandleType colorBufferHandle) {
    auto colorBuffer = findColorBuffer(colorBufferHandle);
    if (!colorBuffer) {
        GFXSTREAM_ERROR("Failed to find ColorBuffer: %d", colorBufferHandle);
        return false;
    }
    return colorBuffer->invalidateForGl();
}
'@ @'
bool FrameBuffer::Impl::invalidateColorBufferForGl(HandleType colorBufferHandle) {
    auto colorBuffer = findColorBuffer(colorBufferHandle);
    if (!colorBuffer) {
        static std::atomic<uint64_t> sMissingInvalidateGlLogs{0};
        const uint64_t logCount = ++sMissingInvalidateGlLogs;
        if (logCount == 1 || logCount == 60 || (logCount % 600) == 0) {
            GFXSTREAM_ERROR("Failed to find ColorBuffer: %d (invalidateGl throttled count=%llu)",
                            colorBufferHandle, (unsigned long long)logCount);
        }
        return false;
    }
    return colorBuffer->invalidateForGl();
}
'@ "FrameBuffer throttle invalidateColorBufferForGl stale ColorBuffer logs"

    Replace-Text $frameBuffer @'
            GFXSTREAM_ERROR("bad color buffer handle %d", p_colorbuffer);
            // bad colorbuffer handle
            return false;
'@ @'
            static std::atomic<uint64_t> sBadColorBufferHandleLogs{0};
            const uint64_t logCount = ++sBadColorBufferHandleLogs;
            if (logCount == 1 || logCount == 60 || (logCount % 600) == 0) {
                GFXSTREAM_ERROR("bad color buffer handle %d (throttled count=%llu)",
                                p_colorbuffer, (unsigned long long)logCount);
            }
            // bad colorbuffer handle
            return false;
'@ "FrameBuffer throttle bad color buffer handle logs"

    # Fix refcount guard: remove chimeraHeadlessVkPost variable from condition if it was removed above.
    Replace-FirstAvailable $frameBuffer `
        @(
            '    if (!m_subWin && !chimeraHeadlessVkPost) {  // m_subWin is supposed to be false',
            '    if (!m_subWin) {  // m_subWin is supposed to be false'
        ) `
        '    if (!m_subWin) {  // m_subWin is supposed to be false' `
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
        "/INCLUDE:__imp_??0AdbAssistantStats@android_studio@@QEAA@XZ"
        "/DELAYLOAD:libandroid-emu-agents.dll"
        "/DELAYLOAD:libandroid-emu-protos.dll"
        "/DELAYLOAD:libandroid-emu-metrics.dll")
    target_link_libraries(gfxstream_backend PRIVATE delayimp.lib)
endif()
'@
$cmakeTextAfterD3d = [System.IO.File]::ReadAllText($cmake).Replace("`r`n", "`n")
if (!$cmakeTextAfterD3d.Contains("CHIMERA_SDK_IMPORT_LIB_DIR")) {
    Write-TextFile $cmake ($cmakeTextAfterD3d.TrimEnd() + $cmakeSdkImportBlock + "`n")
}
} else {
    Write-Host "legacy_gl_patch=skipped"
    # Still need SDK import link block for vulkan-only path
    $cmakeSdkImportBlock = @'

if (WIN32 AND MSVC AND DEFINED CHIMERA_SDK_IMPORT_LIB_DIR)
    target_link_libraries(gfxstream_backend PRIVATE
        "${CHIMERA_SDK_IMPORT_LIB_DIR}/libandroid-emu-agents.lib"
        "${CHIMERA_SDK_IMPORT_LIB_DIR}/libandroid-emu-protos.lib"
        "${CHIMERA_SDK_IMPORT_LIB_DIR}/libandroid-emu-metrics.lib")
    target_link_options(gfxstream_backend PRIVATE
        "/INCLUDE:__imp_?Callback@LineConsumer@emulation@android@@SAHPEAXPEBDH@Z"
        "/INCLUDE:__imp_??0AllRefArDo@android_emulator@@QEAA@XZ"
        "/INCLUDE:__imp_??0AdbAssistantStats@android_studio@@QEAA@XZ"
        "/DELAYLOAD:libandroid-emu-agents.dll"
        "/DELAYLOAD:libandroid-emu-protos.dll"
        "/DELAYLOAD:libandroid-emu-metrics.dll")
    target_link_libraries(gfxstream_backend PRIVATE delayimp.lib)
endif()
'@
    $cmakeText = [System.IO.File]::ReadAllText($cmake).Replace("`r`n", "`n")
    if (!$cmakeText.Contains("CHIMERA_SDK_IMPORT_LIB_DIR")) {
        Write-TextFile $cmake ($cmakeText.TrimEnd() + $cmakeSdkImportBlock + "`n")
    }
}

if ($hasVulkanDisplay) {
    Copy-TextTemplate $vulkanBridgeTemplateHeader $vulkanBridgeHeader "Vulkan display-post bridge header"
    Copy-TextTemplate $vulkanBridgeTemplateCpp $vulkanBridgeCpp "Vulkan display-post bridge source"

    if ($modernVulkanLayout) {
        foreach ($path in @($vulkanBridgeHeader, $vulkanBridgeCpp)) {
            Replace-AllLiteral $path '#include "BorrowedImageVk.h"' '#include "borrowed_image_vk.h"'
            Replace-AllLiteral $path "namespace gfxstream {`nnamespace vk {" "namespace gfxstream::host::vk {"
            Replace-AllLiteral $path "}  // namespace vk`n}  // namespace gfxstream" "}  // namespace gfxstream::host::vk"
            # aosp-github uses gfxstream::base::Lock (not android::base::Lock)
            Replace-AllLiteral $path '#include "aemu/base/synchronization/Lock.h"' '#include "gfxstream/synchronization/Lock.h"'
            Replace-AllLiteral $path 'android::base::Lock' 'gfxstream::base::Lock'
            Replace-AllLiteral $path 'android::base::AutoLock' 'gfxstream::base::AutoLock'
        }
    }

    # Patch vk_common_operations.h/.cpp with direct GPU bridge accessors (no DisplayVk needed).
    if ($modernVulkanLayout) {
        $vkCommonOpsH   = Join-Path $vulkanDir "vk_common_operations.h"
        $vkCommonOpsCpp = Join-Path $vulkanDir "vk_common_operations.cpp"
        if ((Test-Path -LiteralPath $vkCommonOpsH -PathType Leaf) -and
            (Test-Path -LiteralPath $vkCommonOpsCpp -PathType Leaf)) {

            # Header: add 6 accessors after getDisplay()
            $vkEmulationAccessorHNeedle = '    DisplayVk* getDisplay();'
            $vkEmulationAccessorHReplacement = @'
    DisplayVk* getDisplay();

    // Chimera: headless GPU bridge accessors (no DisplayVk/CompositorVk needed)
    const VulkanDispatch* getVkInstanceDispatch() const;
    VkPhysicalDevice getVkPhysicalDevice() const;
    VkDevice getVkDevice() const;
    VkQueue getVkQueue() const;
    std::shared_ptr<gfxstream::base::Lock> getVkQueueLock() const;
    uint32_t getVkQueueFamilyIndex() const;
'@
            Replace-Once $vkCommonOpsH $vkEmulationAccessorHNeedle $vkEmulationAccessorHReplacement "VkEmulation bridge accessor declarations"

            # Impl: add 6 accessor implementations after getDisplay()
            $vkEmulationAccessorCppNeedle = 'DisplayVk* VkEmulation::getDisplay() { return mDisplayVk.get(); }'
            $vkEmulationAccessorCppReplacement = @'
DisplayVk* VkEmulation::getDisplay() { return mDisplayVk.get(); }

const VulkanDispatch* VkEmulation::getVkInstanceDispatch() const { return mIvk; }
VkPhysicalDevice VkEmulation::getVkPhysicalDevice() const { return mPhysicalDevice; }
VkDevice VkEmulation::getVkDevice() const { return mDevice; }
VkQueue VkEmulation::getVkQueue() const { return mQueue; }
std::shared_ptr<gfxstream::base::Lock> VkEmulation::getVkQueueLock() const { return mQueueLock; }
uint32_t VkEmulation::getVkQueueFamilyIndex() const { return mQueueFamilyIndex; }
'@
            Replace-Once $vkCommonOpsCpp $vkEmulationAccessorCppNeedle $vkEmulationAccessorCppReplacement "VkEmulation bridge accessor implementations"
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

    # Modern frame_buffer.cpp: add Vulkan bridge include + direct GPU bridge init after VkDecoderGlobalState.
    if ($modernVulkanLayout) {
        $modernFrameBuffer = Join-Path $hostDir "frame_buffer.cpp"
        if (Test-Path -LiteralPath $modernFrameBuffer -PathType Leaf) {
            $fbVkBridgeIncludeNeedle = '#include "vulkan/post_worker_vk.h"'
            $fbVkBridgeIncludeReplacement = '#include "vulkan/post_worker_vk.h"' + "`n" + '#include "vulkan/ChimeraGfxstreamVulkanSharedTextureBridge.h"'
            Replace-Once $modernFrameBuffer $fbVkBridgeIncludeNeedle $fbVkBridgeIncludeReplacement "frame_buffer Vulkan bridge include"

            # Inject bridge init block before SyncThread::initialize (handles both comment variants)
            $fbBridgeInitNeedles = @(
                '    SyncThread::initialize(/* hasGL */ impl->m_emulationGl != nullptr);',
                '    SyncThread::initialize(impl->m_emulationGl != nullptr);'
            )
            $fbBridgeInitReplacementWithComment = @'
    // Chimera: init direct GPU bridge resources from VkEmulation (no DisplayVk/CompositorVk)
    if (impl->m_vulkanEnabled && impl->m_emulationVk) {
        auto& bridge = vk::ChimeraGfxstreamVulkanSharedTextureBridge::get();
        if (bridge.isEnabled()) {
            const bool bridgeOk = bridge.initDirectVkResources(
                impl->m_emulationVk->getVkInstanceDispatch(),
                impl->m_emulationVk->getVkPhysicalDevice(),
                impl->m_emulationVk->getVkDevice(),
                impl->m_emulationVk->getVkQueueFamilyIndex(),
                impl->m_emulationVk->getVkQueue(),
                impl->m_emulationVk->getVkQueueLock());
            std::fprintf(stderr, "[chimera-gfxstream] Direct GPU bridge init: %s\n",
                         bridgeOk ? "OK" : "FAILED"); std::fflush(stderr);
        }
    }
    SyncThread::initialize(/* hasGL */ impl->m_emulationGl != nullptr);
'@
            $fbBridgeInitReplacementNoComment = @'
    // Chimera: init direct GPU bridge resources from VkEmulation (no DisplayVk/CompositorVk)
    if (impl->m_vulkanEnabled && impl->m_emulationVk) {
        auto& bridge = vk::ChimeraGfxstreamVulkanSharedTextureBridge::get();
        if (bridge.isEnabled()) {
            const bool bridgeOk = bridge.initDirectVkResources(
                impl->m_emulationVk->getVkInstanceDispatch(),
                impl->m_emulationVk->getVkPhysicalDevice(),
                impl->m_emulationVk->getVkDevice(),
                impl->m_emulationVk->getVkQueueFamilyIndex(),
                impl->m_emulationVk->getVkQueue(),
                impl->m_emulationVk->getVkQueueLock());
            std::fprintf(stderr, "[chimera-gfxstream] Direct GPU bridge init: %s\n",
                         bridgeOk ? "OK" : "FAILED"); std::fflush(stderr);
        }
    }
    SyncThread::initialize(impl->m_emulationGl != nullptr);
'@
            Replace-FirstAvailable $modernFrameBuffer $fbBridgeInitNeedles $fbBridgeInitReplacementWithComment "frame_buffer direct GPU bridge init"
        }
    }

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
        $displayVkText.Contains($displayVkNoSurfaceModernReplacement.Replace("`r`n", "`n")) -or
        $displayVkText.Contains("return postImplHeadlessBridge(postCmd);")) {
        # Already patched (direct headless bridge path or old surface gate replacement).
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
# April 2026 variant: -Werror commented out with "# TODO: renable"
$gfxstreamBackendWarningsApril2026Needle = @'
# Suppress some warnings
target_compile_options(
    gfxstream_backend_static
    PRIVATE
    -Wall
    -Wextra
    # TODO: renable
    # -Werror
    -Wno-missing-field-initializers
    -Wno-unused-parameter
    -Wno-unused-private-field
    -Wno-return-type-c-linkage
    -Wno-extern-c-compat
    -DGFXSTREAM_ENABLE_HOST_GLES=1
    )
'@
$gfxstreamBackendWarningsApril2026Replacement = @'
# Suppress some warnings
target_compile_options(
    gfxstream_backend_static
    PRIVATE
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>"
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
    $cmakeText.Contains($gfxstreamBackendWarningsModernReplacement.Replace("`r`n", "`n")) -or
    $cmakeText.Contains($gfxstreamBackendWarningsApril2026Replacement.Replace("`r`n", "`n"))) {
    # Already patched.
} elseif ($cmakeText.Contains($gfxstreamBackendWarningsApril2026Needle.Replace("`r`n", "`n"))) {
    Replace-Once $cmake $gfxstreamBackendWarningsApril2026Needle $gfxstreamBackendWarningsApril2026Replacement "gfxstream backend MSVC warning options"
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
    # April 2026 variant: -Werror is commented out with "# TODO: renable" above it
    $vulkanTargetWarningsApril2026Needle = @'
target_compile_options(gfxstream-vulkan-server
    PRIVATE
    -Wall
    -Wextra
    # TODO: renable
    #-Werror
    -Wno-missing-field-initializers
    -Wno-unused-parameter
    -Wno-unused-private-field
    -Wno-return-type-c-linkage
    -Wno-extern-c-compat
    )
'@
    $vulkanTargetWarningsApril2026Replacement = @'
target_compile_options(gfxstream-vulkan-server
    PRIVATE
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-missing-field-initializers>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-unused-parameter>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-unused-private-field>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-return-type-c-linkage>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-extern-c-compat>"
    )
'@
    $vulkanCmakeText = [System.IO.File]::ReadAllText($vulkanCmake).Replace("`r`n", "`n")
    if ($vulkanCmakeText.Contains($vulkanTargetWarningsReplacement.Replace("`r`n", "`n")) -or
        $vulkanCmakeText.Contains($vulkanTargetWarningsOldReplacement.Replace("`r`n", "`n")) -or
        $vulkanCmakeText.Contains($vulkanTargetWarningsApril2026Replacement.Replace("`r`n", "`n"))) {
        # Already patched.
    } elseif ($vulkanCmakeText.Contains($vulkanTargetWarningsApril2026Needle.Replace("`r`n", "`n"))) {
        Replace-Once $vulkanCmake $vulkanTargetWarningsApril2026Needle $vulkanTargetWarningsApril2026Replacement "gfxstream vulkan MSVC warning options"
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

# Chimera: make the custom gfxstream runtime usable for normal Android UI in
# headless mode. The prebuilt emulator reports GLES mode "host" for -gpu host;
# in headless mode that resolves to bundled SwiftShader, but renderer HOST makes
# the translator emit desktop core-profile shaders ("#version 330 core"), which
# SwiftShader's ES compiler rejects and SurfaceFlinger composes black. This gate
# disables core-profile shader emission while preserving the renderer identity
# and the direct-VK shared-texture path used by continuously-rendering content.
$glesVersionDetector = Join-Path $hostDir "gl\gles_version_detector.cpp"
if (Test-Path -LiteralPath $glesVersionDetector) {
    Replace-Once $glesVersionDetector `
        @'
bool shouldEnableCoreProfile() {
    int dispatchMaj, dispatchMin;

    get_gfxstream_gles_version(&dispatchMaj, &dispatchMin);
    return get_gfxstream_renderer() == SELECTED_RENDERER_HOST &&
           dispatchMaj > 2;
}
'@ `
        @'
bool shouldEnableCoreProfile() {
    int dispatchMaj, dispatchMin;

    get_gfxstream_gles_version(&dispatchMaj, &dispatchMin);
    return get_gfxstream_renderer() == SELECTED_RENDERER_HOST &&
           dispatchMaj > 2 &&
           gfxstream::base::getEnvironmentVariable("CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES") != "1";
}
'@ `
        "GLES version detector headless SwiftShader ES shader gate"
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

# --- Session 87/88: headless host-GLES ANGLE routing (R&D only; not production) ---
# Root cause of the custom-runtime black normal UI (log-confirmed): in headless
# mode emuglConfig_init() falls host GLES to SwiftShader (software); the renderer
# enum stays SELECTED_RENDERER_HOST, so the translator emits desktop
# "#version 330 core" compositor shaders that SwiftShader's ES compiler rejects
# ("'core' : invalid version directive") -> SurfaceFlinger composition empty ->
# black. gl60 60fps survives because postFrameDirectGpu bypasses that compositor.
#
# This patch redirects the headless fallback to ANGLE (gpu_mode="angle_indirect")
# INSIDE the DLL, bypassing the prebuilt emulator's CLI-level angle_indirect
# rejection. Keep it as an opt-in R&D probe only: later Session 88 evidence showed
# ANGLE/D3D11 can initialize and remove the shader-version error, but
# SurfaceFlinger draw then crashes inside ANGLE libGLESv2.dll (glDrawArrays,
# program 28/31; newer ANGLE reproduced it too). DO NOT enable for production.
# Production normal UI is fixed by CHIMERA_GFXSTREAM_HEADLESS_SWIFTSHADER_ES=1,
# which disables HOST/core-profile shader emission while preserving the
# direct-VK shared-texture path. See verify-hardware-ui.ps1 and CONTEXT.md.
$emuglConfig = Join-Path $glDir "gl-host-common\opengl\emugl_config.cpp"
if (Test-Path -LiteralPath $emuglConfig) {
    Replace-Once $emuglConfig @'
            if (stringVectorContains(sBackendList->names(), "swiftshader")) {
                D("%s: Headless mode or blacklisted GPU driver, "
                  "using Swiftshader backend\n",
                  __FUNCTION__);
                gpu_mode = "swiftshader_indirect";
            } else if (!has_guest_renderer) {
'@ @'
            if (android::base::getEnvironmentVariable("CHIMERA_GFXSTREAM_HEADLESS_ANGLE") == "1" &&
                stringVectorContains(sBackendList->names(), "angle")) {
                // Chimera (Session 87): headless host GLES on ANGLE (GLES-on-D3D11,
                // hardware on NVIDIA) instead of SwiftShader. Setting gpu_mode here
                // bypasses the prebuilt emulator's CLI-level angle_indirect
                // rejection; renderer becomes non-HOST so the translator emits
                // "#version 300 es" shaders (compile) and host Vulkan stays on the
                // real GPU (60fps direct-VK path preserved).
                D("%s: Headless mode, Chimera ANGLE host GLES backend\n", __FUNCTION__);
                gpu_mode = "angle_indirect";
            } else if (stringVectorContains(sBackendList->names(), "swiftshader")) {
                D("%s: Headless mode or blacklisted GPU driver, "
                  "using Swiftshader backend\n",
                  __FUNCTION__);
                gpu_mode = "swiftshader_indirect";
            } else if (!has_guest_renderer) {
'@ "emugl_config headless ANGLE host GLES routing"
}

# Session 91: std::promise/std::future/std::packaged_task crash on this host (two
# incompatible MSVCP140.dll versions disagree on _Associated_state layout -- same root
# cause Session 76 fixed in frame_buffer.cpp). HWUI Vulkan's heavy per-submit fence
# traffic drove these paths and crashed. Replace with header-only atomics / Lock+CV.
$deviceOpTrackerH = Join-Path $vulkanDir "device_op_tracker.h"
if (Test-Path -LiteralPath $deviceOpTrackerH) {
    Replace-FirstAvailable $deviceOpTrackerH @(@'
#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <optional>
#include <variant>
'@) @'
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <variant>
'@ "device_op_tracker.h atomic/memory includes"

    Replace-FirstAvailable $deviceOpTrackerH @(@'
using DeviceOpWaitable = std::shared_future<void>;

inline bool IsDone(const DeviceOpWaitable& waitable) {
    return waitable.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
}
'@) @'
// Chimera fix: std::promise<void>/std::shared_future<void> crash on this host because
// two incompatible MSVCP140.dll versions disagree on _Associated_state layout (same
// root cause fixed in frame_buffer.cpp, Session 76). Under HWUI Vulkan the per-submit
// fence traffic drives this path hard and crashes in promise->set_value() /
// wait_for(). Use a header-only atomic done-flag so no _Associated_state is touched.
// A null waitable counts as done (nothing to wait on).
using DeviceOpWaitable = std::shared_ptr<std::atomic<bool>>;

inline bool IsDone(const DeviceOpWaitable& waitable) {
    return !waitable || waitable->load(std::memory_order_acquire);
}
'@ "device_op_tracker.h DeviceOpWaitable atomic flag"
}

$deviceOpTrackerCpp = Join-Path $vulkanDir "device_op_tracker.cpp"
if (Test-Path -LiteralPath $deviceOpTrackerCpp) {
    Replace-FirstAvailable $deviceOpTrackerCpp @(@'
    std::shared_ptr<std::promise<void>> promise = std::make_shared<std::promise<void>>();
    DeviceOpWaitable future = promise->get_future().share();

    mTracker.AddPendingDeviceOp([device = mTracker.mDevice,
                                 deviceDispatch = mTracker.mDeviceDispatch, fence,
                                 promise = std::move(promise), destroyFenceOnCompletion] {
        if (fence == VK_NULL_HANDLE) {
            return DeviceOpStatus::kDone;
        }

        VkResult result = deviceDispatch->vkGetFenceStatus(device, fence);
        if (result == VK_NOT_READY) {
            return DeviceOpStatus::kPending;
        }

        if (destroyFenceOnCompletion) {
            deviceDispatch->vkDestroyFence(device, fence, nullptr);
        }
        promise->set_value();

        return result == VK_SUCCESS ? DeviceOpStatus::kDone : DeviceOpStatus::kFailure;
    });

    return future;
'@) @'
    // Chimera fix: replace std::promise<void>/std::shared_future<void> with a header-only
    // atomic done-flag. The promise machinery crashes here under HWUI Vulkan's heavy
    // per-submit fence traffic (incompatible MSVCP140.dll _Associated_state). The flag is
    // stored when the op completes; IsDone() reads it. See device_op_tracker.h.
    std::shared_ptr<std::atomic<bool>> done = std::make_shared<std::atomic<bool>>(false);
    DeviceOpWaitable future = done;

    mTracker.AddPendingDeviceOp([device = mTracker.mDevice,
                                 deviceDispatch = mTracker.mDeviceDispatch, fence,
                                 done, destroyFenceOnCompletion] {
        if (fence == VK_NULL_HANDLE) {
            done->store(true, std::memory_order_release);
            return DeviceOpStatus::kDone;
        }

        VkResult result = deviceDispatch->vkGetFenceStatus(device, fence);
        if (result == VK_NOT_READY) {
            return DeviceOpStatus::kPending;
        }

        if (destroyFenceOnCompletion) {
            deviceDispatch->vkDestroyFence(device, fence, nullptr);
        }
        done->store(true, std::memory_order_release);

        return result == VK_SUCCESS ? DeviceOpStatus::kDone : DeviceOpStatus::kFailure;
    });

    return future;
'@ "device_op_tracker.cpp promise->atomic"
}

$syncThreadH = Join-Path $hostDir "sync_thread.h"
if (Test-Path -LiteralPath $syncThreadH) {
    Replace-FirstAvailable $syncThreadH @(@'
    struct Command {
        std::packaged_task<int(WorkerId)> mTask;
        std::string mDescription;
    };
'@) @'
    struct Command {
        // Chimera: was std::packaged_task<int(WorkerId)>. packaged_task/std::future use
        // MSVCP140 _Associated_state, which crashes on this host (two incompatible
        // MSVCP140.dll versions). HWUI Vulkan drives sync commands hard and crashed in
        // doSyncThreadCmd's task invocation. Use a plain callable; sendAndWaitForResult
        // carries the result back via Lock+ConditionVariable. Same root cause as Session 76.
        std::function<int(WorkerId)> mTask;
        std::string mDescription;
    };
'@ "sync_thread.h Command callable"
}

$syncThreadCpp = Join-Path $hostDir "sync_thread.cpp"
if (Test-Path -LiteralPath $syncThreadCpp) {
    Replace-FirstAvailable $syncThreadCpp @(@'
#include "gfxstream/threads/Thread.h"
'@) @'
#include "gfxstream/threads/Thread.h"
#include "gfxstream/synchronization/ConditionVariable.h"  // Chimera: Lock+CV replaces std::future
'@ "sync_thread.cpp ConditionVariable include"

    $syncText = [System.IO.File]::ReadAllText($syncThreadCpp).Replace("`r`n", "`n")
    if ($syncText -notmatch 'pResult = &result') {
        Replace-FirstAvailable $syncThreadCpp @(@'
    std::packaged_task<int(WorkerId)> task(std::move(job));
    std::future<int> resFuture = task.get_future();
    Command command = {
        .mTask = std::move(task),
        .mDescription = std::move(description),
    };

    mWorkerThreadPool.enqueue(std::move(command));
    auto res = resFuture.get();
    DPRINT("exit");
    return res;
'@) @'
    // Chimera: replace std::packaged_task<int>/std::future<int> (crashes in MSVCP140
    // _Associated_state on this host) with Lock+ConditionVariable to carry the result.
    // The caller blocks until the worker signals, so Result lives on the stack and is
    // captured by pointer -- no heap allocation (lighter than the original future, which
    // heap-allocates its shared state).
    struct Result {
        gfxstream::base::Lock lock;
        gfxstream::base::ConditionVariable cv;
        bool done = false;
        int value = 0;
    } result;
    Command command = {
        .mTask = [job = std::move(job), pResult = &result](WorkerId workerId) -> int {
            int v = job(workerId);
            {
                gfxstream::base::AutoLock lock(pResult->lock);
                pResult->value = v;
                pResult->done = true;
            }
            pResult->cv.signal();
            return v;
        },
        .mDescription = std::move(description),
    };

    mWorkerThreadPool.enqueue(std::move(command));
    int res;
    {
        gfxstream::base::AutoLock lock(result.lock);
        result.cv.wait(&lock, [&] { return result.done; });
        res = result.value;
    }
    DPRINT("exit");
    return res;
'@ "sync_thread.cpp sendAndWaitForResult Lock+CV"
    }

    $syncText = [System.IO.File]::ReadAllText($syncThreadCpp).Replace("`r`n", "`n")
    if ($syncText -notmatch 'plain callable instead of std::packaged_task') {
        Replace-FirstAvailable $syncThreadCpp @(@'
        .mTask =
            std::packaged_task<int(WorkerId)>([job = std::move(job)](WorkerId workerId) mutable {
                job(workerId);
                return 0;
            }),
'@) @'
        // Chimera: plain callable instead of std::packaged_task (MSVCP140 crash).
        .mTask = [job = std::move(job)](WorkerId workerId) mutable -> int {
            job(workerId);
            return 0;
        },
'@ "sync_thread.cpp sendAsync callable"
    }
}

$workerThreadH = Join-Path $root "common\base\include\gfxstream\threads\WorkerThread.h"
if (Test-Path -LiteralPath $workerThreadH) {
    Replace-FirstAvailable $workerThreadH @(@'
#include <functional>
#include <future>
#include <optional>
#include <thread>
#include <utility>
#include <vector>
'@, @'
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>
'@) @'
#include <algorithm>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>
'@ "WorkerThread standard includes"

    Replace-FirstAvailable $workerThreadH @(@'
#include "gfxstream/ThreadAnnotations.h"
#include "gfxstream/synchronization/ConditionVariable.h"
#include "gfxstream/synchronization/Lock.h"
'@, @'
#include "gfxstream/ThreadAnnotations.h"
'@) @'
#include "gfxstream/ThreadAnnotations.h"
#include "gfxstream/synchronization/ConditionVariable.h"
#include "gfxstream/synchronization/Lock.h"
#include "gfxstream/system/System.h"
'@ "WorkerThread gfxstream synchronization includes"

    $workerText = [System.IO.File]::ReadAllText($workerThreadH).Replace("`r`n", "`n")
    if ($workerText -notmatch 'class WorkerWaitable') {
        Replace-FirstAvailable $workerThreadH @(@'
// Return values for a worker thread's processing function.
enum class WorkerProcessingResult { Continue, Stop };
'@) @'
// Return values for a worker thread's processing function.
enum class WorkerProcessingResult { Continue, Stop };

// Chimera: per-item completion signal replacing std::promise<void>/std::future<void>.
// std::promise::set_value() crashes on this host (two incompatible MSVCP140.dll versions
// disagree on _Associated_state layout -- same root cause Session 76 fixed in
// frame_buffer.cpp). HWUI Vulkan's heavy enqueue traffic drove ThreadLoop's per-item
// set_value() into the crash. This Lock+ConditionVariable signal touches no MSVCP140
// shared state. enqueue() returns WorkerWaitable; callers can use the old future-style
// .wait() / .wait_for(...) API while ThreadLoop signals via ->signal().
class WorkerCompletion {
  public:
    void signal() {
        gfxstream::base::AutoLock lock(mLock);
        mDone = true;
        mCv.signal();
    }
    void wait() {
        gfxstream::base::AutoLock lock(mLock);
        mCv.wait(&lock, [this] { return mDone; });
    }
    template <class Rep, class Period>
    std::future_status wait_for(const std::chrono::duration<Rep, Period>& timeout) {
        gfxstream::base::AutoLock lock(mLock);
        if (mDone) {
            return std::future_status::ready;
        }
        const auto timeoutUs = std::chrono::duration_cast<std::chrono::microseconds>(timeout).count();
        const auto waitUntilUs = gfxstream::base::getUnixTimeUs() +
                                 static_cast<uint64_t>(std::max<int64_t>(0, timeoutUs));
        while (!mDone) {
            if (!mCv.timedWait(&mLock, waitUntilUs)) {
                return mDone ? std::future_status::ready : std::future_status::timeout;
            }
        }
        return std::future_status::ready;
    }

  private:
    gfxstream::base::Lock mLock;
    gfxstream::base::ConditionVariable mCv;
    bool mDone = false;
};
class WorkerWaitable {
  public:
    WorkerWaitable() : mCompletion(std::make_shared<WorkerCompletion>()) {}
    void wait() const { mCompletion->wait(); }
    template <class Rep, class Period>
    std::future_status wait_for(const std::chrono::duration<Rep, Period>& timeout) const {
        return mCompletion->wait_for(timeout);
    }
    WorkerCompletion* operator->() const { return mCompletion.get(); }

  private:
    std::shared_ptr<WorkerCompletion> mCompletion;
};
'@ "WorkerThread WorkerCompletion"
    }

    Replace-FirstAvailable $workerThreadH @(@'
    // Waits for all enqueue()'d items to finish or the worker stops.
    void waitQueuedItems() {
        // Enqueue an empty sync command.
        std::future<void> completeFuture = enqueueImpl(Command());
        completeFuture.wait();
    }

    // Moves the |item| into internal queue for processing. If the command is enqueued after the
    // stop command is enqueued or before start() returns, the returned future will also be ready
    // without processing the command.
    std::future<void> enqueue(Item&& item) {
        return enqueueImpl(Command(std::move(item)));
    }
'@) @'
    // Waits for all enqueue()'d items to finish or the worker stops.
    void waitQueuedItems() {
        // Enqueue an empty sync command.
        WorkerWaitable completeWaitable = enqueueImpl(Command());
        completeWaitable->wait();
    }

    // Moves the |item| into internal queue for processing. If the command is enqueued after the
    // stop command is enqueued or before start() returns, the returned waitable will also be ready
    // without processing the command. Chimera: returns shared_ptr<WorkerCompletion> instead of
    // std::future<void> (MSVCP140 crash); callers that block call ->wait(), others discard.
    WorkerWaitable enqueue(Item&& item) {
        return enqueueImpl(Command(std::move(item)));
    }
'@ "WorkerThread enqueue waitable"

    Replace-FirstAvailable $workerThreadH @(@'
    struct Command {
        Command() : mWorkItem(std::nullopt) {}
        Command(Item&& it) : mWorkItem(std::move(it)) {}
        Command(Command&& other)
            : mCompletedPromise(std::move(other.mCompletedPromise)),
              mWorkItem(std::move(other.mWorkItem)) {}

        std::promise<void> mCompletedPromise;
        std::optional<Item> mWorkItem;
    };

    std::future<void> enqueueImpl(Command command) {
        gfxstream::base::AutoLock lock(mMutex);

        // Do not enqueue any new items if exiting.
        if (mExiting) {
            command.mCompletedPromise.set_value();
            return command.mCompletedPromise.get_future();
        }

        std::future<void> res = command.mCompletedPromise.get_future();
        mQueue.emplace_back(std::move(command));
        // signal() does not require holding the lock but is safe while holding it.
        mCv.signal();
        return res;
    }
'@) @'
    struct Command {
        Command() : mCompleted(), mWorkItem(std::nullopt) {}
        Command(Item&& it) : mCompleted(), mWorkItem(std::move(it)) {}
        Command(Command&& other)
            : mCompleted(std::move(other.mCompleted)),
              mWorkItem(std::move(other.mWorkItem)) {}

        // Chimera: shared completion signal instead of std::promise<void> (MSVCP140 crash).
        WorkerWaitable mCompleted;
        std::optional<Item> mWorkItem;
    };

    WorkerWaitable enqueueImpl(Command command) {
        gfxstream::base::AutoLock lock(mMutex);

        // Do not enqueue any new items if exiting.
        if (mExiting) {
            command.mCompleted->signal();
            return command.mCompleted;
        }

        WorkerWaitable res = command.mCompleted;
        mQueue.emplace_back(std::move(command));
        // signal() does not require holding the lock but is safe while holding it.
        mCv.signal();
        return res;
    }
'@ "WorkerThread Command completion"

    Replace-FirstAvailable $workerThreadH @(@'
            bool shouldStop = false;
            for (Command& item : todo) {
                if (!shouldStop && item.mWorkItem) {
                    shouldStop = mProcessor(std::move(item.mWorkItem.value())) == Result::Stop;
                }
                item.mCompletedPromise.set_value();
            }

            if (shouldStop) {
                gfxstream::base::AutoLock lock(mMutex);

                mExiting = true;

                // Signal pending tasks as if they are completed.
                for (Command& item : mQueue) {
                    item.mCompletedPromise.set_value();
                }

                return;
            }
'@) @'
            bool shouldStop = false;
            for (Command& item : todo) {
                if (!shouldStop && item.mWorkItem) {
                    shouldStop = mProcessor(std::move(item.mWorkItem.value())) == Result::Stop;
                }
                item.mCompleted->signal();
            }

            if (shouldStop) {
                gfxstream::base::AutoLock lock(mMutex);

                mExiting = true;

                // Signal pending tasks as if they are completed.
                for (Command& item : mQueue) {
                    item.mCompleted->signal();
                }

                return;
            }
'@ "WorkerThread ThreadLoop completion"
}

$frameBufferFiles = @(
    (Join-Path $hostDir "frame_buffer.cpp"),
    (Join-Path $hostDir "FrameBuffer.cpp")
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf }
foreach ($frameBufferPath in $frameBufferFiles) {
    $frameBufferPostModernNeedle = @'
        std::future<void> completeFuture =
            m_postThread.enqueue(Post(std::move(post)));
        if (!shouldPostOnlyOnMainThread ||
            (PostCmd::Screenshot == post.cmd &&
             !get_gfxstream_window_operations().is_current_thread_ui_thread())) {
            res = std::move(completeFuture);
        }
'@
    $frameBufferPostModernReplacement = @'
        // Chimera: enqueue() now returns WorkerWaitable (MSVCP140 promise crash). Bridge
        // back to the std::future<void> return via deferred async; resolved inline on
        // .wait(), so std::promise::set_value() is never invoked.
        auto completeWaitable = m_postThread.enqueue(Post(std::move(post)));
        if (!shouldPostOnlyOnMainThread ||
            (PostCmd::Screenshot == post.cmd &&
             !get_gfxstream_window_operations().is_current_thread_ui_thread())) {
            res = std::async(std::launch::deferred, [completeWaitable] {
                completeWaitable.wait();
            });
        }
'@
    $frameBufferPostLegacyNeedle = @'
        std::future<void> completeFuture =
            m_postThread.enqueue(Post(std::move(post)));
        if (!shouldPostOnlyOnMainThread ||
            (PostCmd::Screenshot == post.cmd &&
             !emugl::get_emugl_window_operations().isRunningInUiThread())) {
            res = std::move(completeFuture);
        }
'@
    $frameBufferPostLegacyReplacement = @'
        // Chimera: enqueue() now returns WorkerWaitable (MSVCP140 promise crash). Bridge
        // back to the std::future<void> return via deferred async; resolved inline on
        // .wait(), so std::promise::set_value() is never invoked.
        auto completeWaitable = m_postThread.enqueue(Post(std::move(post)));
        if (!shouldPostOnlyOnMainThread ||
            (PostCmd::Screenshot == post.cmd &&
             !emugl::get_emugl_window_operations().isRunningInUiThread())) {
            res = std::async(std::launch::deferred, [completeWaitable] {
                completeWaitable.wait();
            });
        }
'@
    $frameBufferPostText = [System.IO.File]::ReadAllText($frameBufferPath).Replace("`r`n", "`n")
    if ($frameBufferPostText.Contains($frameBufferPostModernReplacement.Replace("`r`n", "`n")) -or
        $frameBufferPostText.Contains($frameBufferPostLegacyReplacement.Replace("`r`n", "`n"))) {
        # already patched
    } elseif ($frameBufferPostText.Contains($frameBufferPostModernNeedle.Replace("`r`n", "`n"))) {
        Replace-Once $frameBufferPath $frameBufferPostModernNeedle $frameBufferPostModernReplacement "frame_buffer sendPostWorkerCmd waitable bridge"
    } else {
        Replace-Once $frameBufferPath $frameBufferPostLegacyNeedle $frameBufferPostLegacyReplacement "frame_buffer sendPostWorkerCmd waitable bridge"
    }

    Replace-FirstAvailable $frameBufferPath @(@'
        std::future<void> completeFuture = m_readbackThread.enqueue(
            {ReadbackCmd::AddRecordDisplay, displayId, nullptr, 0, w, h});
        completeFuture.wait();
    } else {
        std::future<void> completeFuture = m_readbackThread.enqueue(
            {ReadbackCmd::DelRecordDisplay, displayId});
        completeFuture.wait();
        m_onPost.erase(displayId);
    }
'@) @'
        auto completeWaitable = m_readbackThread.enqueue(
            {ReadbackCmd::AddRecordDisplay, displayId, nullptr, 0, w, h});
        completeWaitable->wait();
    } else {
        auto completeWaitable = m_readbackThread.enqueue(
            {ReadbackCmd::DelRecordDisplay, displayId});
        completeWaitable->wait();
        m_onPost.erase(displayId);
    }
'@ "frame_buffer record-display waitable"

    Replace-FirstAvailable $frameBufferPath @(@'
    std::future<void> completeFuture =
        m_readbackThread.enqueue({ReadbackCmd::GetPixels, displayId, pixels, bytes});
    completeFuture.wait();
'@) @'
    auto completeWaitable =
        m_readbackThread.enqueue({ReadbackCmd::GetPixels, displayId, pixels, bytes});
    completeWaitable->wait();
'@ "frame_buffer getDisplayPixels waitable"
}

$frameBufferBlockFiles = @(
    (Join-Path $hostDir "frame_buffer.cpp"),
    (Join-Path $hostDir "FrameBuffer.cpp")
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf }
foreach ($frameBufferPath in $frameBufferBlockFiles) {
    # GL->VK content sync before the kVk borrow in the headless direct-GPU post.
    # GLES-composited frames live in the GL backing; without invalidateForVk()
    # the blit publishes a never-written VK image (all-zero shared texture, host
    # window black, healthy-looking counters). No-op for VK-backed ColorBuffers.
    Replace-Text $frameBufferPath @'
        if (bridge.isEnabled() && bridge.isDirectVkReady()) {
            static std::atomic<int> sBorrowTimingFrame{0};
'@ @'
        if (bridge.isEnabled() && bridge.isDirectVkReady()) {
            // GLES-composited content (SwiftShader-ES SurfaceFlinger) lives in the
            // GL backing; the kVk sibling image is stale until this GL->VK sync.
            // Without it the blit publishes a never-written image: the shared
            // texture stays all-zero (host window black) while every sequence/FPS
            // counter looks healthy. flushFromGl() is needed first because the
            // HWC compose path writes the target through host GL (CompositorGl)
            // without ever marking mGlTexDirty -- invalidateForVk() alone exits
            // "clean" and syncs nothing. Both are no-ops when GL/VK share memory.
            colorBuffer->flushFromGl();
            colorBuffer->invalidateForVk();
            static std::atomic<int> sBorrowTimingFrame{0};
'@ "frame_buffer headless kVk GL->VK content sync"

    Replace-FirstAvailable $frameBufferPath @(@'
    std::future<void> blockPostWorker(std::future<void> continueSignal);
'@) @'
    gfxstream::base::WorkerWaitable blockPostWorker(gfxstream::base::WorkerWaitable continueSignal);
'@ "frame_buffer blockPostWorker WorkerWaitable declaration"

    Replace-FirstAvailable $frameBufferPath @(@'
    class ScopedPromise {
       public:
        ~ScopedPromise() { mPromise.set_value(); }
        std::future<void> getFuture() { return mPromise.get_future(); }
        DISALLOW_COPY_ASSIGN_AND_MOVE(ScopedPromise);
        static std::tuple<std::unique_ptr<ScopedPromise>, std::future<void>> create() {
            auto scopedPromise = std::unique_ptr<ScopedPromise>(new ScopedPromise());
            auto future = scopedPromise->mPromise.get_future();
            return std::make_tuple(std::move(scopedPromise), std::move(future));
        }

       private:
        ScopedPromise() = default;
        std::promise<void> mPromise;
    };
    std::unique_ptr<ScopedPromise> postWorkerContinueSignal;
    std::future<void> postWorkerContinueSignalFuture;
    std::tie(postWorkerContinueSignal, postWorkerContinueSignalFuture) = ScopedPromise::create();
    {
        blockPostWorker(std::move(postWorkerContinueSignalFuture)).wait();
    }
'@) @'
    class ScopedWorkerSignal {
       public:
        ScopedWorkerSignal() = default;
        ~ScopedWorkerSignal() { reset(); }
        gfxstream::base::WorkerWaitable waitable() const { return mSignal; }
        void reset() {
            if (!mSignaled) {
                mSignal->signal();
                mSignaled = true;
            }
        }
        DISALLOW_COPY_ASSIGN_AND_MOVE(ScopedWorkerSignal);

       private:
        gfxstream::base::WorkerWaitable mSignal;
        bool mSignaled = false;
    };
    ScopedWorkerSignal postWorkerContinueSignal;
    gfxstream::base::WorkerWaitable postWorkerContinueSignalScheduled =
        blockPostWorker(postWorkerContinueSignal.waitable());
    postWorkerContinueSignalScheduled.wait();
'@ "frame_buffer setupSubWindow WorkerWaitable block signal"

    Replace-FirstAvailable $frameBufferPath @(@'
std::future<void> FrameBuffer::Impl::blockPostWorker(std::future<void> continueSignal) {
    std::promise<void> scheduled;
    std::future<void> scheduledFuture = scheduled.get_future();
    Post postCmd = {
        .cmd = PostCmd::Block,
        .block = std::make_unique<Post::Block>(Post::Block{
            .scheduledSignal = std::move(scheduled),
            .continueSignal = std::move(continueSignal),
        }),
    };
    sendPostWorkerCmd(std::move(postCmd));
    return scheduledFuture;
}
'@) @'
gfxstream::base::WorkerWaitable FrameBuffer::Impl::blockPostWorker(
    gfxstream::base::WorkerWaitable continueSignal) {
    gfxstream::base::WorkerWaitable scheduledSignal;
    Post postCmd = {
        .cmd = PostCmd::Block,
        .block = std::make_unique<Post::Block>(Post::Block{
            .scheduledSignal = scheduledSignal,
            .continueSignal = std::move(continueSignal),
        }),
    };
    sendPostWorkerCmd(std::move(postCmd));
    return scheduledSignal;
}
'@ "frame_buffer blockPostWorker WorkerWaitable implementation"
}

$postCommandsH = Join-Path $hostDir "post_commands.h"
if (Test-Path -LiteralPath $postCommandsH) {
    $postCommandsText = [System.IO.File]::ReadAllText($postCommandsH).Replace("`r`n", "`n")
    if ($postCommandsText -notmatch 'WorkerThread.h') {
        Replace-FirstAvailable $postCommandsH @(@'
#include "gfxstream/host/display_operations.h"
#include "gfxstream/host/gfxstream_format.h"
#include "handle.h"
#include "render-utils/Renderer.h"
'@) @'
#include "gfxstream/host/display_operations.h"
#include "gfxstream/host/gfxstream_format.h"
#include "handle.h"
#include "render-utils/Renderer.h"
#include "gfxstream/threads/WorkerThread.h"
'@ "post_commands WorkerWaitable include"
    }

    $postCommandsText = [System.IO.File]::ReadAllText($postCommandsH).Replace("`r`n", "`n")
    if ($postCommandsText -notmatch 'WorkerWaitable scheduledSignal') {
        Replace-FirstAvailable $postCommandsH @(@'
    struct Block {
        // schduledSignal will be set when the block task is scheduled.
        std::promise<void> scheduledSignal;
        // The block task won't stop until continueSignal is ready.
        std::future<void> continueSignal;
    };
'@) @'
    struct Block {
        // scheduledSignal is signaled when the block task is scheduled.
        gfxstream::base::WorkerWaitable scheduledSignal;
        // The block task won't stop until continueSignal is ready.
        gfxstream::base::WorkerWaitable continueSignal;
    };
'@ "post_commands Block WorkerWaitable"
    }
}

$postWorkerH = Join-Path $hostDir "post_worker.h"
if (Test-Path -LiteralPath $postWorkerH) {
    $postWorkerHText = [System.IO.File]::ReadAllText($postWorkerH).Replace("`r`n", "`n")
    if ($postWorkerHText -notmatch 'WorkerWaitable scheduledSignal') {
        Replace-FirstAvailable $postWorkerH @(@'
    // The block task will set the scheduledSignal promise when the task is scheduled, and wait
    // until continueSignal is ready before completes.
    void block(std::promise<void> scheduledSignal, std::future<void> continueSignal);
'@) @'
    // The block task signals scheduledSignal when scheduled, then waits until continueSignal is ready.
    void block(gfxstream::base::WorkerWaitable scheduledSignal,
               gfxstream::base::WorkerWaitable continueSignal);
'@ "post_worker block WorkerWaitable signature"
    }
}

$postWorkerCpp = Join-Path $hostDir "post_worker.cpp"
if (Test-Path -LiteralPath $postWorkerCpp) {
    $postWorkerCppText = [System.IO.File]::ReadAllText($postWorkerCpp).Replace("`r`n", "`n")
    if ($postWorkerCppText -notmatch 'PostWorker::block\(gfxstream::base::WorkerWaitable') {
        Replace-FirstAvailable $postWorkerCpp @(@'
void PostWorker::block(std::promise<void> scheduledSignal, std::future<void> continueSignal) {
    // Do not block mainthread.
    if (m_mainThreadPostingOnly) {
        return;
    }
    // MSVC STL doesn't support not copyable std::packaged_task. As a workaround, we use the
    // copyable std::shared_ptr here.
    auto block = std::make_shared<Post::Block>(Post::Block{
        .scheduledSignal = std::move(scheduledSignal),
        .continueSignal = std::move(continueSignal),
    });
    runTask(std::packaged_task<void()>([block] {
        block->scheduledSignal.set_value();
        block->continueSignal.wait();
    }));
}
'@) @'
void PostWorker::block(gfxstream::base::WorkerWaitable scheduledSignal,
                       gfxstream::base::WorkerWaitable continueSignal) {
    // Do not block mainthread.
    if (m_mainThreadPostingOnly) {
        // Chimera: the original std::promise was destroyed on this early return, which woke the
        // waiter via broken_promise. WorkerWaitable has no auto-complete, so signal explicitly or
        // blockPostWorker()'s scheduledSignal.wait() hangs forever (main-thread-posting/macOS path).
        scheduledSignal->signal();
        return;
    }
    auto block = std::make_shared<Post::Block>(Post::Block{
        .scheduledSignal = std::move(scheduledSignal),
        .continueSignal = std::move(continueSignal),
    });
    runTask(std::packaged_task<void()>([block] {
        block->scheduledSignal->signal();
        block->continueSignal.wait();
    }));
}
'@ "post_worker block WorkerWaitable implementation"
    }
}

# --- color_buffer.cpp: invalidateForVk exit-path diagnostics -------------------
# Low-frequency log of which gate short-circuits the GL->VK content sync that
# feeds the headless direct-GPU post. This is the sync whose silent no-op made
# the shared texture publish all-zero frames for 15 sessions.
$colorBufferPath = Join-Path $hostDir "color_buffer.cpp"
if (Test-Path -LiteralPath $colorBufferPath -PathType Leaf) {
    Replace-Text $colorBufferPath @'
#include "color_buffer.h"
'@ @'
#include "color_buffer.h"

#include <atomic>
#include <chrono>
#include <cstdio>
'@ "color_buffer diag includes"

    Replace-Text $colorBufferPath @'
bool ColorBuffer::Impl::invalidateForVk() {
    if (!(mColorBufferGl && mColorBufferVk)) {
        return true;
    }

    if (mGlAndVkAreSharingExternalMemory) {
        return true;
    }

    if (!mGlTexDirty) {
        return true;
    }

'@ @'
bool ColorBuffer::Impl::invalidateForVk() {
    // chimera-diag: low-frequency exit-path log (which gate short-circuits the
    // GL->VK content sync feeding the headless direct-GPU post).
    static std::atomic<int> sChimeraInvalidateLog{0};
    const bool chimeraLog = (sChimeraInvalidateLog.fetch_add(1) % 600 == 0);
    if (!(mColorBufferGl && mColorBufferVk)) {
        if (chimeraLog) {
            std::fprintf(stderr, "[chimera-diag] invalidateForVk cb=%u exit=no-pair gl=%d vk=%d\n",
                         mHandle, mColorBufferGl ? 1 : 0, mColorBufferVk ? 1 : 0);
            std::fflush(stderr);
        }
        return true;
    }

    if (mGlAndVkAreSharingExternalMemory) {
        if (chimeraLog) {
            std::fprintf(stderr, "[chimera-diag] invalidateForVk cb=%u exit=shared-extmem\n", mHandle);
            std::fflush(stderr);
        }
        return true;
    }

    if (!mGlTexDirty) {
        if (chimeraLog) {
            std::fprintf(stderr, "[chimera-diag] invalidateForVk cb=%u exit=clean\n", mHandle);
            std::fflush(stderr);
        }
        return true;
    }

    if (chimeraLog) {
        std::fprintf(stderr, "[chimera-diag] invalidateForVk cb=%u syncing GL->VK\n", mHandle);
        std::fflush(stderr);
    }

'@ "color_buffer invalidateForVk exit diagnostics"

    Replace-Text $colorBufferPath @'
    bool mGlAndVkAreSharingExternalMemory = false;
    bool mGlTexDirty = false;
};
'@ @'
    bool mGlAndVkAreSharingExternalMemory = false;
    bool mGlTexDirty = false;
    // Chimera: reused GL readback scratch for the headless GL->VK content sync;
    // a fresh zero-initialized vector costs an extra 8MB memset per 1080p frame
    // on the guest-blocking post path.
    std::vector<uint8_t> mChimeraGlReadbackBuffer;
};
'@ "color_buffer reused readback buffer member"

    Replace-Text $colorBufferPath @'
#if GFXSTREAM_ENABLE_HOST_GLES
    std::size_t contentsSize = 0;
    if (!mColorBufferGl->readContents(&contentsSize, nullptr)) {
        GFXSTREAM_ERROR("Failed to get GL contents size for ColorBuffer:%d", mHandle);
        return false;
    }

    std::vector<uint8_t> contents(contentsSize, 0);

    if (!mColorBufferGl->readContents(&contentsSize, contents.data())) {
        GFXSTREAM_ERROR("Failed to get GL contents for ColorBuffer:%d", mHandle);
        return false;
    }

    if (!mColorBufferVk->updateFromBytes(contents)) {
        GFXSTREAM_ERROR("Failed to set VK contents for ColorBuffer:%d", mHandle);
        return false;
    }
#endif
    mGlTexDirty = false;
    return true;
}
'@ @'
#if GFXSTREAM_ENABLE_HOST_GLES
    const auto tSync0 = std::chrono::steady_clock::now();
    std::size_t contentsSize = 0;
    if (!mColorBufferGl->readContents(&contentsSize, nullptr)) {
        GFXSTREAM_ERROR("Failed to get GL contents size for ColorBuffer:%d", mHandle);
        return false;
    }

    if (mChimeraGlReadbackBuffer.size() != contentsSize) {
        mChimeraGlReadbackBuffer.resize(contentsSize);
    }

    if (!mColorBufferGl->readContents(&contentsSize, mChimeraGlReadbackBuffer.data())) {
        GFXSTREAM_ERROR("Failed to get GL contents for ColorBuffer:%d", mHandle);
        return false;
    }
    const auto tRead = std::chrono::steady_clock::now();

    if (!mColorBufferVk->updateFromBytes(mChimeraGlReadbackBuffer)) {
        GFXSTREAM_ERROR("Failed to set VK contents for ColorBuffer:%d", mHandle);
        return false;
    }

    static std::atomic<int> sChimeraSyncTiming{0};
    if (sChimeraSyncTiming.fetch_add(1) % 120 == 0) {
        const auto tUpload = std::chrono::steady_clock::now();
        const auto ms = [](auto a, auto b) {
            return std::chrono::duration<double, std::milli>(b - a).count();
        };
        std::fprintf(stderr,
                     "[chimera-timing] glToVkSync cb=%u read=%.1fms upload=%.1fms total=%.1fms\n",
                     mHandle, ms(tSync0, tRead), ms(tRead, tUpload), ms(tSync0, tUpload));
        std::fflush(stderr);
    }
#endif
    mGlTexDirty = false;
    return true;
}
'@ "color_buffer invalidateForVk reused buffer + timing"
}

# --- frame_buffer.cpp (modern): guest-blocking compose/post wall-time timing ---
# The guest HWC present waits for compose (PostWorker) then runs the headless
# publish inline; this split shows which side eats the frame budget.
$modernFrameBufferTiming = Join-Path $hostDir "frame_buffer.cpp"
if (Test-Path -LiteralPath $modernFrameBufferTiming -PathType Leaf) {
    Replace-Text $modernFrameBufferTiming @'
bool FrameBuffer::Impl::compose(uint32_t bufferSize, void* buffer, bool needPost) {
    bool done = false;
'@ @'
bool FrameBuffer::Impl::compose(uint32_t bufferSize, void* buffer, bool needPost) {
    const auto tComposeStart = std::chrono::steady_clock::now();
    bool done = false;
'@ "frame_buffer compose timing start"

    Replace-Text $modernFrameBufferTiming @'
    if (composeRes.CallbackScheduledOrFired()) {
        gfxstream::base::AutoLock lk(doneLock);
        doneCv.wait(&lk, [&] { return done; });
    }

#ifdef CONFIG_AEMU
'@ @'
    if (composeRes.CallbackScheduledOrFired()) {
        gfxstream::base::AutoLock lk(doneLock);
        doneCv.wait(&lk, [&] { return done; });
    }
    const auto tComposeDone = std::chrono::steady_clock::now();

#ifdef CONFIG_AEMU
'@ "frame_buffer compose timing mid"

    Replace-Text $modernFrameBufferTiming @'
            default: {
                return false;
            }
        }
    }
#endif

    return true;
}

AsyncResult FrameBuffer::Impl::composeWithCallback(uint32_t bufferSize, void* buffer,
'@ @'
            default: {
                return false;
            }
        }
    }
#endif

    static std::atomic<int> sChimeraComposeTiming{0};
    if (sChimeraComposeTiming.fetch_add(1) % 120 == 0) {
        const auto tEnd = std::chrono::steady_clock::now();
        const auto ms = [](auto a, auto b) {
            return std::chrono::duration<double, std::milli>(b - a).count();
        };
        std::fprintf(stderr,
                     "[chimera-timing] guestCompose: compose=%.1fms post=%.1fms total=%.1fms\n",
                     ms(tComposeStart, tComposeDone), ms(tComposeDone, tEnd),
                     ms(tComposeStart, tEnd));
        std::fflush(stderr);
    }
    return true;
}

AsyncResult FrameBuffer::Impl::composeWithCallback(uint32_t bufferSize, void* buffer,
'@ "frame_buffer compose timing log"
}

# --- Vulkan cereal: VK_EXT_mesh_shader extension struct support -----------------
# Chimera: gfxstream's extension size table knows VK_EXT_mesh_shader, but the
# generated marshal/unmarshal/transform/deepcopy handlers in this snapshot do not.
# Apps such as GravityMark query these pNext structs and hit default abort paths.
$vkCerealCommonDir = Join-Path $vulkanDir "cereal\common"
$reservedMarshalingPath = Join-Path $vkCerealCommonDir "goldfish_vk_reserved_marshaling.cpp"
$marshalingPath = Join-Path $vkCerealCommonDir "goldfish_vk_marshaling.cpp"
$transformPath = Join-Path $vkCerealCommonDir "goldfish_vk_transform.cpp"
$deepcopyPath = Join-Path $vkCerealCommonDir "goldfish_vk_deepcopy.cpp"

if (Test-Path -LiteralPath $reservedMarshalingPath -PathType Leaf) {
    Replace-Once $reservedMarshalingPath @'
#endif
#ifdef VK_EXT_color_write_enable
void reservedunmarshal_VkPhysicalDeviceColorWriteEnableFeaturesEXT(
'@ @'
#endif
#ifdef VK_EXT_mesh_shader
void reservedunmarshal_VkPhysicalDeviceMeshShaderFeaturesEXT(
    VulkanStream* vkStream, VkStructureType rootType,
    VkPhysicalDeviceMeshShaderFeaturesEXT* forUnmarshaling, uint8_t** ptr) {
    memcpy((VkStructureType*)&forUnmarshaling->sType, *ptr, sizeof(VkStructureType));
    *ptr += sizeof(VkStructureType);
    if (rootType == VK_STRUCTURE_TYPE_MAX_ENUM) {
        rootType = forUnmarshaling->sType;
    }
    uint32_t pNext_size;
    memcpy((uint32_t*)&pNext_size, *ptr, sizeof(uint32_t));
    gfxstream::Stream::fromBe32((uint8_t*)&pNext_size);
    *ptr += sizeof(uint32_t);
    forUnmarshaling->pNext = nullptr;
    if (pNext_size) {
        vkStream->alloc((void**)&forUnmarshaling->pNext, sizeof(VkStructureType));
        memcpy((void*)forUnmarshaling->pNext, *ptr, sizeof(VkStructureType));
        *ptr += sizeof(VkStructureType);
        VkStructureType extType = *(VkStructureType*)(forUnmarshaling->pNext);
        vkStream->alloc((void**)&forUnmarshaling->pNext,
                        goldfish_vk_extension_struct_size_with_stream_features(
                            vkStream->getFeatureBits(), rootType, forUnmarshaling->pNext));
        *(VkStructureType*)forUnmarshaling->pNext = extType;
        reservedunmarshal_extension_struct(vkStream, rootType, (void*)(forUnmarshaling->pNext),
                                           ptr);
    }
    memcpy((VkBool32*)&forUnmarshaling->taskShader, *ptr, sizeof(VkBool32));
    *ptr += sizeof(VkBool32);
    memcpy((VkBool32*)&forUnmarshaling->meshShader, *ptr, sizeof(VkBool32));
    *ptr += sizeof(VkBool32);
    memcpy((VkBool32*)&forUnmarshaling->multiviewMeshShader, *ptr, sizeof(VkBool32));
    *ptr += sizeof(VkBool32);
    memcpy((VkBool32*)&forUnmarshaling->primitiveFragmentShadingRateMeshShader, *ptr,
           sizeof(VkBool32));
    *ptr += sizeof(VkBool32);
    memcpy((VkBool32*)&forUnmarshaling->meshShaderQueries, *ptr, sizeof(VkBool32));
    *ptr += sizeof(VkBool32);
}

void reservedunmarshal_VkPhysicalDeviceMeshShaderPropertiesEXT(
    VulkanStream* vkStream, VkStructureType rootType,
    VkPhysicalDeviceMeshShaderPropertiesEXT* forUnmarshaling, uint8_t** ptr) {
    memcpy((VkStructureType*)&forUnmarshaling->sType, *ptr, sizeof(VkStructureType));
    *ptr += sizeof(VkStructureType);
    if (rootType == VK_STRUCTURE_TYPE_MAX_ENUM) {
        rootType = forUnmarshaling->sType;
    }
    uint32_t pNext_size;
    memcpy((uint32_t*)&pNext_size, *ptr, sizeof(uint32_t));
    gfxstream::Stream::fromBe32((uint8_t*)&pNext_size);
    *ptr += sizeof(uint32_t);
    forUnmarshaling->pNext = nullptr;
    if (pNext_size) {
        vkStream->alloc((void**)&forUnmarshaling->pNext, sizeof(VkStructureType));
        memcpy((void*)forUnmarshaling->pNext, *ptr, sizeof(VkStructureType));
        *ptr += sizeof(VkStructureType);
        VkStructureType extType = *(VkStructureType*)(forUnmarshaling->pNext);
        vkStream->alloc((void**)&forUnmarshaling->pNext,
                        goldfish_vk_extension_struct_size_with_stream_features(
                            vkStream->getFeatureBits(), rootType, forUnmarshaling->pNext));
        *(VkStructureType*)forUnmarshaling->pNext = extType;
        reservedunmarshal_extension_struct(vkStream, rootType, (void*)(forUnmarshaling->pNext),
                                           ptr);
    }
    memcpy((uint32_t*)&forUnmarshaling->maxTaskWorkGroupTotalCount, *ptr, sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    memcpy((uint32_t*)forUnmarshaling->maxTaskWorkGroupCount, *ptr, 3 * sizeof(uint32_t));
    *ptr += 3 * sizeof(uint32_t);
    memcpy((uint32_t*)&forUnmarshaling->maxTaskWorkGroupInvocations, *ptr, sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    memcpy((uint32_t*)forUnmarshaling->maxTaskWorkGroupSize, *ptr, 3 * sizeof(uint32_t));
    *ptr += 3 * sizeof(uint32_t);
    memcpy((uint32_t*)&forUnmarshaling->maxTaskPayloadSize, *ptr, sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    memcpy((uint32_t*)&forUnmarshaling->maxTaskSharedMemorySize, *ptr, sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    memcpy((uint32_t*)&forUnmarshaling->maxTaskPayloadAndSharedMemorySize, *ptr,
           sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    memcpy((uint32_t*)&forUnmarshaling->maxMeshWorkGroupTotalCount, *ptr, sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    memcpy((uint32_t*)forUnmarshaling->maxMeshWorkGroupCount, *ptr, 3 * sizeof(uint32_t));
    *ptr += 3 * sizeof(uint32_t);
    memcpy((uint32_t*)&forUnmarshaling->maxMeshWorkGroupInvocations, *ptr, sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    memcpy((uint32_t*)forUnmarshaling->maxMeshWorkGroupSize, *ptr, 3 * sizeof(uint32_t));
    *ptr += 3 * sizeof(uint32_t);
    memcpy((uint32_t*)&forUnmarshaling->maxMeshSharedMemorySize, *ptr, sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    memcpy((uint32_t*)&forUnmarshaling->maxMeshPayloadAndSharedMemorySize, *ptr,
           sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    memcpy((uint32_t*)&forUnmarshaling->maxMeshOutputMemorySize, *ptr, sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    memcpy((uint32_t*)&forUnmarshaling->maxMeshPayloadAndOutputMemorySize, *ptr,
           sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    memcpy((uint32_t*)&forUnmarshaling->maxMeshOutputComponents, *ptr, sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    memcpy((uint32_t*)&forUnmarshaling->maxMeshOutputVertices, *ptr, sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    memcpy((uint32_t*)&forUnmarshaling->maxMeshOutputPrimitives, *ptr, sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    memcpy((uint32_t*)&forUnmarshaling->maxMeshOutputLayers, *ptr, sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    memcpy((uint32_t*)&forUnmarshaling->maxMeshMultiviewViewCount, *ptr, sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    memcpy((uint32_t*)&forUnmarshaling->meshOutputPerVertexGranularity, *ptr,
           sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    memcpy((uint32_t*)&forUnmarshaling->meshOutputPerPrimitiveGranularity, *ptr,
           sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    memcpy((uint32_t*)&forUnmarshaling->maxPreferredTaskWorkGroupInvocations, *ptr,
           sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    memcpy((uint32_t*)&forUnmarshaling->maxPreferredMeshWorkGroupInvocations, *ptr,
           sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    memcpy((VkBool32*)&forUnmarshaling->prefersLocalInvocationVertexOutput, *ptr,
           sizeof(VkBool32));
    *ptr += sizeof(VkBool32);
    memcpy((VkBool32*)&forUnmarshaling->prefersLocalInvocationPrimitiveOutput, *ptr,
           sizeof(VkBool32));
    *ptr += sizeof(VkBool32);
    memcpy((VkBool32*)&forUnmarshaling->prefersCompactVertexOutput, *ptr, sizeof(VkBool32));
    *ptr += sizeof(VkBool32);
    memcpy((VkBool32*)&forUnmarshaling->prefersCompactPrimitiveOutput, *ptr, sizeof(VkBool32));
    *ptr += sizeof(VkBool32);
}

#endif
#ifdef VK_EXT_color_write_enable
void reservedunmarshal_VkPhysicalDeviceColorWriteEnableFeaturesEXT(
'@ "reserved VK_EXT_mesh_shader struct unmarshaling"

    Replace-Once $reservedMarshalingPath @'
#endif
#ifdef VK_EXT_color_write_enable
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT: {
'@ @'
#endif
#ifdef VK_EXT_mesh_shader
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT: {
            reservedunmarshal_VkPhysicalDeviceMeshShaderFeaturesEXT(
                vkStream, rootType,
                reinterpret_cast<VkPhysicalDeviceMeshShaderFeaturesEXT*>(structExtension_out), ptr);
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT: {
            reservedunmarshal_VkPhysicalDeviceMeshShaderPropertiesEXT(
                vkStream, rootType,
                reinterpret_cast<VkPhysicalDeviceMeshShaderPropertiesEXT*>(structExtension_out),
                ptr);
            break;
        }
#endif
#ifdef VK_EXT_color_write_enable
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT: {
'@ "reserved VK_EXT_mesh_shader extension switch"
}

if (Test-Path -LiteralPath $marshalingPath -PathType Leaf) {
    Replace-Once $marshalingPath @'
#endif
#ifdef VK_EXT_color_write_enable
void marshal_VkPhysicalDeviceColorWriteEnableFeaturesEXT(
'@ @'
#endif
#ifdef VK_EXT_mesh_shader
void marshal_VkPhysicalDeviceMeshShaderFeaturesEXT(
    VulkanStream* vkStream, VkStructureType rootType,
    const VkPhysicalDeviceMeshShaderFeaturesEXT* forMarshaling) {
    (void)rootType;
    vkStream->write((VkStructureType*)&forMarshaling->sType, sizeof(VkStructureType));
    if (rootType == VK_STRUCTURE_TYPE_MAX_ENUM) {
        rootType = forMarshaling->sType;
    }
    marshal_extension_struct(vkStream, rootType, forMarshaling->pNext);
    vkStream->write((VkBool32*)&forMarshaling->taskShader, sizeof(VkBool32));
    vkStream->write((VkBool32*)&forMarshaling->meshShader, sizeof(VkBool32));
    vkStream->write((VkBool32*)&forMarshaling->multiviewMeshShader, sizeof(VkBool32));
    vkStream->write((VkBool32*)&forMarshaling->primitiveFragmentShadingRateMeshShader,
                    sizeof(VkBool32));
    vkStream->write((VkBool32*)&forMarshaling->meshShaderQueries, sizeof(VkBool32));
}

void unmarshal_VkPhysicalDeviceMeshShaderFeaturesEXT(
    VulkanStream* vkStream, VkStructureType rootType,
    VkPhysicalDeviceMeshShaderFeaturesEXT* forUnmarshaling) {
    (void)rootType;
    vkStream->read((VkStructureType*)&forUnmarshaling->sType, sizeof(VkStructureType));
    if (rootType == VK_STRUCTURE_TYPE_MAX_ENUM) {
        rootType = forUnmarshaling->sType;
    }
    size_t pNext_size;
    pNext_size = vkStream->getBe32();
    forUnmarshaling->pNext = nullptr;
    if (pNext_size) {
        vkStream->alloc((void**)&forUnmarshaling->pNext, sizeof(VkStructureType));
        vkStream->read((void*)forUnmarshaling->pNext, sizeof(VkStructureType));
        VkStructureType extType = *(VkStructureType*)(forUnmarshaling->pNext);
        vkStream->alloc((void**)&forUnmarshaling->pNext,
                        goldfish_vk_extension_struct_size_with_stream_features(
                            vkStream->getFeatureBits(), rootType, forUnmarshaling->pNext));
        *(VkStructureType*)forUnmarshaling->pNext = extType;
        unmarshal_extension_struct(vkStream, rootType, (void*)(forUnmarshaling->pNext));
    }
    vkStream->read((VkBool32*)&forUnmarshaling->taskShader, sizeof(VkBool32));
    vkStream->read((VkBool32*)&forUnmarshaling->meshShader, sizeof(VkBool32));
    vkStream->read((VkBool32*)&forUnmarshaling->multiviewMeshShader, sizeof(VkBool32));
    vkStream->read((VkBool32*)&forUnmarshaling->primitiveFragmentShadingRateMeshShader,
                   sizeof(VkBool32));
    vkStream->read((VkBool32*)&forUnmarshaling->meshShaderQueries, sizeof(VkBool32));
}

void marshal_VkPhysicalDeviceMeshShaderPropertiesEXT(
    VulkanStream* vkStream, VkStructureType rootType,
    const VkPhysicalDeviceMeshShaderPropertiesEXT* forMarshaling) {
    (void)rootType;
    vkStream->write((VkStructureType*)&forMarshaling->sType, sizeof(VkStructureType));
    if (rootType == VK_STRUCTURE_TYPE_MAX_ENUM) {
        rootType = forMarshaling->sType;
    }
    marshal_extension_struct(vkStream, rootType, forMarshaling->pNext);
    vkStream->write((uint32_t*)&forMarshaling->maxTaskWorkGroupTotalCount, sizeof(uint32_t));
    vkStream->write((uint32_t*)forMarshaling->maxTaskWorkGroupCount, 3 * sizeof(uint32_t));
    vkStream->write((uint32_t*)&forMarshaling->maxTaskWorkGroupInvocations, sizeof(uint32_t));
    vkStream->write((uint32_t*)forMarshaling->maxTaskWorkGroupSize, 3 * sizeof(uint32_t));
    vkStream->write((uint32_t*)&forMarshaling->maxTaskPayloadSize, sizeof(uint32_t));
    vkStream->write((uint32_t*)&forMarshaling->maxTaskSharedMemorySize, sizeof(uint32_t));
    vkStream->write((uint32_t*)&forMarshaling->maxTaskPayloadAndSharedMemorySize,
                    sizeof(uint32_t));
    vkStream->write((uint32_t*)&forMarshaling->maxMeshWorkGroupTotalCount, sizeof(uint32_t));
    vkStream->write((uint32_t*)forMarshaling->maxMeshWorkGroupCount, 3 * sizeof(uint32_t));
    vkStream->write((uint32_t*)&forMarshaling->maxMeshWorkGroupInvocations, sizeof(uint32_t));
    vkStream->write((uint32_t*)forMarshaling->maxMeshWorkGroupSize, 3 * sizeof(uint32_t));
    vkStream->write((uint32_t*)&forMarshaling->maxMeshSharedMemorySize, sizeof(uint32_t));
    vkStream->write((uint32_t*)&forMarshaling->maxMeshPayloadAndSharedMemorySize,
                    sizeof(uint32_t));
    vkStream->write((uint32_t*)&forMarshaling->maxMeshOutputMemorySize, sizeof(uint32_t));
    vkStream->write((uint32_t*)&forMarshaling->maxMeshPayloadAndOutputMemorySize,
                    sizeof(uint32_t));
    vkStream->write((uint32_t*)&forMarshaling->maxMeshOutputComponents, sizeof(uint32_t));
    vkStream->write((uint32_t*)&forMarshaling->maxMeshOutputVertices, sizeof(uint32_t));
    vkStream->write((uint32_t*)&forMarshaling->maxMeshOutputPrimitives, sizeof(uint32_t));
    vkStream->write((uint32_t*)&forMarshaling->maxMeshOutputLayers, sizeof(uint32_t));
    vkStream->write((uint32_t*)&forMarshaling->maxMeshMultiviewViewCount, sizeof(uint32_t));
    vkStream->write((uint32_t*)&forMarshaling->meshOutputPerVertexGranularity,
                    sizeof(uint32_t));
    vkStream->write((uint32_t*)&forMarshaling->meshOutputPerPrimitiveGranularity,
                    sizeof(uint32_t));
    vkStream->write((uint32_t*)&forMarshaling->maxPreferredTaskWorkGroupInvocations,
                    sizeof(uint32_t));
    vkStream->write((uint32_t*)&forMarshaling->maxPreferredMeshWorkGroupInvocations,
                    sizeof(uint32_t));
    vkStream->write((VkBool32*)&forMarshaling->prefersLocalInvocationVertexOutput,
                    sizeof(VkBool32));
    vkStream->write((VkBool32*)&forMarshaling->prefersLocalInvocationPrimitiveOutput,
                    sizeof(VkBool32));
    vkStream->write((VkBool32*)&forMarshaling->prefersCompactVertexOutput, sizeof(VkBool32));
    vkStream->write((VkBool32*)&forMarshaling->prefersCompactPrimitiveOutput, sizeof(VkBool32));
}

void unmarshal_VkPhysicalDeviceMeshShaderPropertiesEXT(
    VulkanStream* vkStream, VkStructureType rootType,
    VkPhysicalDeviceMeshShaderPropertiesEXT* forUnmarshaling) {
    (void)rootType;
    vkStream->read((VkStructureType*)&forUnmarshaling->sType, sizeof(VkStructureType));
    if (rootType == VK_STRUCTURE_TYPE_MAX_ENUM) {
        rootType = forUnmarshaling->sType;
    }
    size_t pNext_size;
    pNext_size = vkStream->getBe32();
    forUnmarshaling->pNext = nullptr;
    if (pNext_size) {
        vkStream->alloc((void**)&forUnmarshaling->pNext, sizeof(VkStructureType));
        vkStream->read((void*)forUnmarshaling->pNext, sizeof(VkStructureType));
        VkStructureType extType = *(VkStructureType*)(forUnmarshaling->pNext);
        vkStream->alloc((void**)&forUnmarshaling->pNext,
                        goldfish_vk_extension_struct_size_with_stream_features(
                            vkStream->getFeatureBits(), rootType, forUnmarshaling->pNext));
        *(VkStructureType*)forUnmarshaling->pNext = extType;
        unmarshal_extension_struct(vkStream, rootType, (void*)(forUnmarshaling->pNext));
    }
    vkStream->read((uint32_t*)&forUnmarshaling->maxTaskWorkGroupTotalCount, sizeof(uint32_t));
    vkStream->read((uint32_t*)forUnmarshaling->maxTaskWorkGroupCount, 3 * sizeof(uint32_t));
    vkStream->read((uint32_t*)&forUnmarshaling->maxTaskWorkGroupInvocations, sizeof(uint32_t));
    vkStream->read((uint32_t*)forUnmarshaling->maxTaskWorkGroupSize, 3 * sizeof(uint32_t));
    vkStream->read((uint32_t*)&forUnmarshaling->maxTaskPayloadSize, sizeof(uint32_t));
    vkStream->read((uint32_t*)&forUnmarshaling->maxTaskSharedMemorySize, sizeof(uint32_t));
    vkStream->read((uint32_t*)&forUnmarshaling->maxTaskPayloadAndSharedMemorySize,
                   sizeof(uint32_t));
    vkStream->read((uint32_t*)&forUnmarshaling->maxMeshWorkGroupTotalCount, sizeof(uint32_t));
    vkStream->read((uint32_t*)forUnmarshaling->maxMeshWorkGroupCount, 3 * sizeof(uint32_t));
    vkStream->read((uint32_t*)&forUnmarshaling->maxMeshWorkGroupInvocations, sizeof(uint32_t));
    vkStream->read((uint32_t*)forUnmarshaling->maxMeshWorkGroupSize, 3 * sizeof(uint32_t));
    vkStream->read((uint32_t*)&forUnmarshaling->maxMeshSharedMemorySize, sizeof(uint32_t));
    vkStream->read((uint32_t*)&forUnmarshaling->maxMeshPayloadAndSharedMemorySize,
                   sizeof(uint32_t));
    vkStream->read((uint32_t*)&forUnmarshaling->maxMeshOutputMemorySize, sizeof(uint32_t));
    vkStream->read((uint32_t*)&forUnmarshaling->maxMeshPayloadAndOutputMemorySize,
                   sizeof(uint32_t));
    vkStream->read((uint32_t*)&forUnmarshaling->maxMeshOutputComponents, sizeof(uint32_t));
    vkStream->read((uint32_t*)&forUnmarshaling->maxMeshOutputVertices, sizeof(uint32_t));
    vkStream->read((uint32_t*)&forUnmarshaling->maxMeshOutputPrimitives, sizeof(uint32_t));
    vkStream->read((uint32_t*)&forUnmarshaling->maxMeshOutputLayers, sizeof(uint32_t));
    vkStream->read((uint32_t*)&forUnmarshaling->maxMeshMultiviewViewCount, sizeof(uint32_t));
    vkStream->read((uint32_t*)&forUnmarshaling->meshOutputPerVertexGranularity,
                   sizeof(uint32_t));
    vkStream->read((uint32_t*)&forUnmarshaling->meshOutputPerPrimitiveGranularity,
                   sizeof(uint32_t));
    vkStream->read((uint32_t*)&forUnmarshaling->maxPreferredTaskWorkGroupInvocations,
                   sizeof(uint32_t));
    vkStream->read((uint32_t*)&forUnmarshaling->maxPreferredMeshWorkGroupInvocations,
                   sizeof(uint32_t));
    vkStream->read((VkBool32*)&forUnmarshaling->prefersLocalInvocationVertexOutput,
                   sizeof(VkBool32));
    vkStream->read((VkBool32*)&forUnmarshaling->prefersLocalInvocationPrimitiveOutput,
                   sizeof(VkBool32));
    vkStream->read((VkBool32*)&forUnmarshaling->prefersCompactVertexOutput, sizeof(VkBool32));
    vkStream->read((VkBool32*)&forUnmarshaling->prefersCompactPrimitiveOutput, sizeof(VkBool32));
}

#endif
#ifdef VK_EXT_color_write_enable
void marshal_VkPhysicalDeviceColorWriteEnableFeaturesEXT(
'@ "VK_EXT_mesh_shader marshal/unmarshal functions"

    Replace-Once $marshalingPath @'
#endif
#ifdef VK_EXT_color_write_enable
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT: {
            marshal_VkPhysicalDeviceColorWriteEnableFeaturesEXT(
'@ @'
#endif
#ifdef VK_EXT_mesh_shader
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT: {
            marshal_VkPhysicalDeviceMeshShaderFeaturesEXT(
                vkStream, rootType,
                reinterpret_cast<const VkPhysicalDeviceMeshShaderFeaturesEXT*>(structExtension));
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT: {
            marshal_VkPhysicalDeviceMeshShaderPropertiesEXT(
                vkStream, rootType,
                reinterpret_cast<const VkPhysicalDeviceMeshShaderPropertiesEXT*>(structExtension));
            break;
        }
#endif
#ifdef VK_EXT_color_write_enable
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT: {
            marshal_VkPhysicalDeviceColorWriteEnableFeaturesEXT(
'@ "VK_EXT_mesh_shader marshal extension switch"

    Replace-Once $marshalingPath @'
#endif
#ifdef VK_EXT_color_write_enable
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT: {
            unmarshal_VkPhysicalDeviceColorWriteEnableFeaturesEXT(
'@ @'
#endif
#ifdef VK_EXT_mesh_shader
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT: {
            unmarshal_VkPhysicalDeviceMeshShaderFeaturesEXT(
                vkStream, rootType,
                reinterpret_cast<VkPhysicalDeviceMeshShaderFeaturesEXT*>(structExtension_out));
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT: {
            unmarshal_VkPhysicalDeviceMeshShaderPropertiesEXT(
                vkStream, rootType,
                reinterpret_cast<VkPhysicalDeviceMeshShaderPropertiesEXT*>(structExtension_out));
            break;
        }
#endif
#ifdef VK_EXT_color_write_enable
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT: {
            unmarshal_VkPhysicalDeviceColorWriteEnableFeaturesEXT(
'@ "VK_EXT_mesh_shader unmarshal extension switch"
}

if (Test-Path -LiteralPath $transformPath -PathType Leaf) {
    Replace-Once $transformPath @'
#endif
#ifdef VK_EXT_color_write_enable
void transform_tohost_VkPhysicalDeviceColorWriteEnableFeaturesEXT(
'@ @'
#endif
#ifdef VK_EXT_mesh_shader
void transform_tohost_VkPhysicalDeviceMeshShaderFeaturesEXT(
    VkDecoderGlobalState* resourceTracker, VkPhysicalDeviceMeshShaderFeaturesEXT* toTransform) {
    (void)resourceTracker;
    (void)toTransform;
    if (toTransform->pNext) {
        transform_tohost_extension_struct(resourceTracker, (void*)(toTransform->pNext));
    }
}

void transform_fromhost_VkPhysicalDeviceMeshShaderFeaturesEXT(
    VkDecoderGlobalState* resourceTracker, VkPhysicalDeviceMeshShaderFeaturesEXT* toTransform) {
    (void)resourceTracker;
    (void)toTransform;
    if (toTransform->pNext) {
        transform_fromhost_extension_struct(resourceTracker, (void*)(toTransform->pNext));
    }
}

void transform_tohost_VkPhysicalDeviceMeshShaderPropertiesEXT(
    VkDecoderGlobalState* resourceTracker, VkPhysicalDeviceMeshShaderPropertiesEXT* toTransform) {
    (void)resourceTracker;
    (void)toTransform;
    if (toTransform->pNext) {
        transform_tohost_extension_struct(resourceTracker, (void*)(toTransform->pNext));
    }
}

void transform_fromhost_VkPhysicalDeviceMeshShaderPropertiesEXT(
    VkDecoderGlobalState* resourceTracker, VkPhysicalDeviceMeshShaderPropertiesEXT* toTransform) {
    (void)resourceTracker;
    (void)toTransform;
    if (toTransform->pNext) {
        transform_fromhost_extension_struct(resourceTracker, (void*)(toTransform->pNext));
    }
}

#endif
#ifdef VK_EXT_color_write_enable
void transform_tohost_VkPhysicalDeviceColorWriteEnableFeaturesEXT(
'@ "VK_EXT_mesh_shader transform functions"

    Replace-Once $transformPath @'
#endif
#ifdef VK_EXT_color_write_enable
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT: {
            transform_tohost_VkPhysicalDeviceColorWriteEnableFeaturesEXT(
'@ @'
#endif
#ifdef VK_EXT_mesh_shader
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT: {
            transform_tohost_VkPhysicalDeviceMeshShaderFeaturesEXT(
                resourceTracker,
                reinterpret_cast<VkPhysicalDeviceMeshShaderFeaturesEXT*>(structExtension_out));
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT: {
            transform_tohost_VkPhysicalDeviceMeshShaderPropertiesEXT(
                resourceTracker,
                reinterpret_cast<VkPhysicalDeviceMeshShaderPropertiesEXT*>(structExtension_out));
            break;
        }
#endif
#ifdef VK_EXT_color_write_enable
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT: {
            transform_tohost_VkPhysicalDeviceColorWriteEnableFeaturesEXT(
'@ "VK_EXT_mesh_shader transform_tohost switch"

    Replace-Once $transformPath @'
#endif
#ifdef VK_EXT_color_write_enable
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT: {
            transform_fromhost_VkPhysicalDeviceColorWriteEnableFeaturesEXT(
'@ @'
#endif
#ifdef VK_EXT_mesh_shader
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT: {
            transform_fromhost_VkPhysicalDeviceMeshShaderFeaturesEXT(
                resourceTracker,
                reinterpret_cast<VkPhysicalDeviceMeshShaderFeaturesEXT*>(structExtension_out));
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT: {
            transform_fromhost_VkPhysicalDeviceMeshShaderPropertiesEXT(
                resourceTracker,
                reinterpret_cast<VkPhysicalDeviceMeshShaderPropertiesEXT*>(structExtension_out));
            break;
        }
#endif
#ifdef VK_EXT_color_write_enable
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT: {
            transform_fromhost_VkPhysicalDeviceColorWriteEnableFeaturesEXT(
'@ "VK_EXT_mesh_shader transform_fromhost switch"
}

if (Test-Path -LiteralPath $deepcopyPath -PathType Leaf) {
    Replace-Once $deepcopyPath @'
#endif
#ifdef VK_EXT_color_write_enable
void deepcopy_VkPhysicalDeviceColorWriteEnableFeaturesEXT(
'@ @'
#endif
#ifdef VK_EXT_mesh_shader
void deepcopy_VkPhysicalDeviceMeshShaderFeaturesEXT(
    Allocator* alloc, VkStructureType rootType,
    const VkPhysicalDeviceMeshShaderFeaturesEXT* from,
    VkPhysicalDeviceMeshShaderFeaturesEXT* to) {
    (void)alloc;
    (void)rootType;
    *to = *from;
    if (rootType == VK_STRUCTURE_TYPE_MAX_ENUM) {
        rootType = from->sType;
    }
    const void* from_pNext = from;
    size_t pNext_size = 0u;
    while (!pNext_size && from_pNext) {
        from_pNext = static_cast<const VkBaseOutStructure*>(from_pNext)->pNext;
        pNext_size = goldfish_vk_extension_struct_size(rootType, from_pNext);
    }
    to->pNext = nullptr;
    if (pNext_size) {
        to->pNext = (void*)alloc->alloc(pNext_size);
        deepcopy_extension_struct(alloc, rootType, from_pNext, (void*)(to->pNext));
    }
}

void deepcopy_VkPhysicalDeviceMeshShaderPropertiesEXT(
    Allocator* alloc, VkStructureType rootType,
    const VkPhysicalDeviceMeshShaderPropertiesEXT* from,
    VkPhysicalDeviceMeshShaderPropertiesEXT* to) {
    (void)alloc;
    (void)rootType;
    *to = *from;
    if (rootType == VK_STRUCTURE_TYPE_MAX_ENUM) {
        rootType = from->sType;
    }
    const void* from_pNext = from;
    size_t pNext_size = 0u;
    while (!pNext_size && from_pNext) {
        from_pNext = static_cast<const VkBaseOutStructure*>(from_pNext)->pNext;
        pNext_size = goldfish_vk_extension_struct_size(rootType, from_pNext);
    }
    to->pNext = nullptr;
    if (pNext_size) {
        to->pNext = (void*)alloc->alloc(pNext_size);
        deepcopy_extension_struct(alloc, rootType, from_pNext, (void*)(to->pNext));
    }
}

#endif
#ifdef VK_EXT_color_write_enable
void deepcopy_VkPhysicalDeviceColorWriteEnableFeaturesEXT(
'@ "VK_EXT_mesh_shader deepcopy functions"

    Replace-Once $deepcopyPath @'
#endif
#ifdef VK_EXT_color_write_enable
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT: {
            deepcopy_VkPhysicalDeviceColorWriteEnableFeaturesEXT(
'@ @'
#endif
#ifdef VK_EXT_mesh_shader
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT: {
            deepcopy_VkPhysicalDeviceMeshShaderFeaturesEXT(
                alloc, rootType,
                reinterpret_cast<const VkPhysicalDeviceMeshShaderFeaturesEXT*>(structExtension),
                reinterpret_cast<VkPhysicalDeviceMeshShaderFeaturesEXT*>(structExtension_out));
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT: {
            deepcopy_VkPhysicalDeviceMeshShaderPropertiesEXT(
                alloc, rootType,
                reinterpret_cast<const VkPhysicalDeviceMeshShaderPropertiesEXT*>(structExtension),
                reinterpret_cast<VkPhysicalDeviceMeshShaderPropertiesEXT*>(structExtension_out));
            break;
        }
#endif
#ifdef VK_EXT_color_write_enable
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT: {
            deepcopy_VkPhysicalDeviceColorWriteEnableFeaturesEXT(
'@ "VK_EXT_mesh_shader deepcopy switch"
}

# --- frame_buffer.cpp: force-enable GlDirectMem alongside Vulkan ----------------
# Guest Vulkan HOST_VISIBLE/coherent memory requires the GLDirectMem host feature;
# the ini value never reaches the DLL (cross-DLL FeatureSet copy is a no-op), so
# force it at the same site that forces Vulkan/EglOnEgl. Without this any real
# Vulkan app aborts in the guest encoder on its first vkAllocateMemory.
$modernFrameBufferFeatures = Join-Path $hostDir "frame_buffer.cpp"
if (Test-Path -LiteralPath $modernFrameBufferFeatures -PathType Leaf) {
    Replace-Text $modernFrameBufferFeatures @'
    impl->m_features.EglOnEgl.setEnabled(true);
    impl->m_features.Vulkan.setEnabled(true);
'@ @'
    impl->m_features.EglOnEgl.setEnabled(true);
    impl->m_features.Vulkan.setEnabled(true);
    // GlDirectMem: required for guest Vulkan HOST_VISIBLE/coherent memory. Without it the
    // guest encoder (libvulkan_enc createCoherentMemory) aborts with "Unsupported virtual
    // memory feature" on the first vkAllocateMemory of any real Vulkan app (GravityMark).
    // The emulator ini's GLDirectMem never reaches us (cross-DLL FeatureSet copy is a no-op),
    // so force it here like Vulkan/EglOnEgl. Kill switch: CHIMERA_GFXSTREAM_NO_DIRECT_MEM=1.
    if (gfxstream::base::getEnvironmentVariable("CHIMERA_GFXSTREAM_NO_DIRECT_MEM") != "1") {
        impl->m_features.GlDirectMem.setEnabled(true);
        std::fprintf(stderr, "[chimera-gfxstream] Impl::Create: GlDirectMem force-enabled\n");
        std::fflush(stderr);
    }
    // Batched descriptor-set updates (requires QueueSubmitWithCommands). The guest's
    // non-batched path blind-deepcopies VkWriteDescriptorSet including pointers the spec
    // allows to be garbage for the given descriptorType -> SIGSEGV in the prebuilt
    // libvulkan_enc (GravityMark's first vkUpdateDescriptorSets). The batched path reifies
    // writes per descriptorType and never touches ignored fields. Both features are "on"
    // in the SDK ini; they only read as off here because the cross-DLL FeatureSet copy is
    // a no-op. Kill switch: CHIMERA_GFXSTREAM_NO_BATCHED_DESCRIPTOR=1.
    if (gfxstream::base::getEnvironmentVariable("CHIMERA_GFXSTREAM_NO_BATCHED_DESCRIPTOR") !=
        "1") {
        impl->m_features.VulkanQueueSubmitWithCommands.setEnabled(true);
        impl->m_features.VulkanBatchedDescriptorSetUpdate.setEnabled(true);
        std::fprintf(stderr,
                     "[chimera-gfxstream] Impl::Create: QueueSubmitWithCommands + "
                     "BatchedDescriptorSetUpdate force-enabled\n");
        std::fflush(stderr);
    }
'@ "frame_buffer GlDirectMem force-enable"
}

# --- FeatureSet copy: same-build value copy by name ------------------------------
# The stock copy ctor/operator= were replaced (tree-only, S101 era) with no-ops to
# survive the emulator's cross-DLL FeatureSet (incompatible vtable). That silently
# dropped every same-DLL copy too: VkEmulation::setFeatures() became a no-op, so
# renderControl advertised features (guest side) the Vulkan decoder never saw --
# guest/host seqno protocol split => stream misalignment AV. Copy plain values by
# name when both sides are the same build (same feature count); keep defaults for
# the cross-DLL set.
$featuresHeader = Join-Path $root "host\features\include\gfxstream\host\Features.h"
if (Test-Path -LiteralPath $featuresHeader -PathType Leaf) {
    Replace-Text $featuresHeader @'
    void setReason(std::string reasonStr) { reason = reasonStr; }
    const std::string& getReason() const { return reason; }
    const std::string& getName() const { return name; }

    virtual bool parseValue(std::string_view strValue) = 0;
'@ @'
    void setReason(std::string reasonStr) { reason = reasonStr; }
    const std::string& getReason() const { return reason; }
    const std::string& getName() const { return name; }

    // Chimera: non-virtual plain-data access so FeatureSet copies can transfer
    // values by name without calling through the source object's vtable (which
    // may belong to another DLL build with an incompatible layout).
    const FeatureValue& rawValue() const { return value; }
    void setRawValue(const FeatureValue& v) { value = v; }

    virtual bool parseValue(std::string_view strValue) = 0;
'@ "Features.h rawValue accessors"
}

$featuresCpp = Join-Path $root "host\features\features.cpp"
if (Test-Path -LiteralPath $featuresCpp -PathType Leaf) {
    Replace-FirstAvailable $featuresCpp @(@'
FeatureSet::FeatureSet(const FeatureSet& rhs) : FeatureSet() {
    // Cannot safely copy from the emulator's FeatureSet across the DLL boundary:
    // the emulator's FeatureInfoBase objects may have an incompatible vtable layout
    // (different SDK build), and calling any virtual method through rhs's pointers
    // causes an AV. Use our own default features; callers that need specific flags
    // must enable them explicitly after construction (see FrameBuffer::Impl::Create).
    std::fprintf(stderr, "[chimera-gfxstream] FeatureSet copy ctor: skipping cross-DLL copy "
                 "(our=%zu rhs=%zu) -- using defaults\n", map.size(), rhs.map.size());
    std::fflush(stderr);
}

FeatureSet& FeatureSet::operator=(const FeatureSet& rhs) {
    // No-op: see FeatureSet(const FeatureSet&) comment above.
    return *this;
}
'@, @'
FeatureSet::FeatureSet(const FeatureSet& rhs) : FeatureSet() { *this = rhs; }
FeatureSet& FeatureSet::operator=(const FeatureSet& rhs) {
    for (auto& [name, featureInfo] : map) {
        featureInfo->parseValue(rhs.map.find(name)->second->getValueReadable());
        featureInfo->setReason(rhs.map.find(name)->second->getReason());
    }
    return *this;
}
'@) @'
// Chimera: copy feature VALUES by name using only non-virtual plain-data access.
// The emulator's FeatureSet may come from another DLL build whose FeatureInfoBase
// vtable layout differs (calling rhs's virtual methods AVs -- the original bug), but
// its std::map and FeatureValue members are plain MSVC STL data and safe to read.
// The previous fix made copies unconditional no-ops, which silently dropped every
// ini/emulator feature and split host state (renderControl advertised features the
// Vulkan decoder never saw -- guest/host seqno protocol mismatch => stream AV).
static void chimeraCopyFeatureValuesByName(FeatureSet& dst, const FeatureSet& src) {
    dst.guestVulkanMaxApiVersion = src.guestVulkanMaxApiVersion;
    if (dst.map.size() != src.map.size()) {
        // Different feature count == different build == member offsets of rhs's
        // FeatureInfoBase cannot be trusted either. Keep the old behavior for the
        // emulator's cross-DLL set: defaults + explicit force-sets in FrameBuffer.
        std::fprintf(stderr,
                     "[chimera-gfxstream] FeatureSet copy: cross-DLL set (our=%zu rhs=%zu) -- "
                     "using defaults\n",
                     dst.map.size(), src.map.size());
        std::fflush(stderr);
        return;
    }
    size_t copied = 0;
    for (auto& [name, info] : dst.map) {
        if (!info) continue;
        auto it = src.map.find(name);
        if (it == src.map.end() || !it->second) continue;
        info->setRawValue(it->second->rawValue());
        ++copied;
    }
    std::fprintf(stderr,
                 "[chimera-gfxstream] FeatureSet copy: %zu/%zu values copied by name\n",
                 copied, dst.map.size());
    std::fflush(stderr);
}

FeatureSet::FeatureSet(const FeatureSet& rhs) : FeatureSet() {
    chimeraCopyFeatureValuesByName(*this, rhs);
}

FeatureSet& FeatureSet::operator=(const FeatureSet& rhs) {
    chimeraCopyFeatureValuesByName(*this, rhs);
    return *this;
}
'@ "FeatureSet same-build value copy"
}

# --- Vulkan boxed handles: tolerate ignored-handle sentinels --------------------
# Some prebuilt guest Vulkan encoder paths send ignored/none handles as low32
# 0xFFFFFFFF, sometimes preserving a high32 tag/slot (for example 0x00000001FFFFFFFF).
# Treat those as null at the shared unbox point instead of killing the emulator.
$boxedHandlesCpp = Join-Path $root "host\vulkan\vulkan_boxed_handles.cpp"
if (Test-Path -LiteralPath $boxedHandlesCpp -PathType Leaf) {
    Replace-Text $boxedHandlesCpp @'
#include "vulkan_boxed_handles.h"

#include "vk_decoder_global_state.h"
'@ @'
#include "vulkan_boxed_handles.h"

#include <atomic>

#include "vk_decoder_global_state.h"
'@ "vulkan_boxed_handles atomic include"

    Replace-FirstAvailable $boxedHandlesCpp @(@'
            } else if ((uint64_t)(uintptr_t)boxed == 0xFFFFFFFFull) {
                // Chimera: guest-side sentinel for "ignored/none" handles (seen from the
                // prebuilt guest encoder, e.g. QSRI/ignored-handle paths). Killing the whole
                // emulator over it turns one bad handle into a total session loss; treat it
                // like VK_NULL_HANDLE and let the host driver report any real error.
                static std::atomic<int> sChimeraSentinelUnbox{0};
                if (sChimeraSentinelUnbox.fetch_add(1) % 100 == 0) {
                    GFXSTREAM_ERROR("Ignoring sentinel unbox of %s 0xFFFFFFFF (count=%d)",
                                    GetTypeStr<VkObjectT>(),
                                    sChimeraSentinelUnbox.load());
                }
'@, @'
            } else {
                GFXSTREAM_FATAL("Failed to unbox %s %p", GetTypeStr<VkObjectT>(), boxed);
            }
'@) @'
            } else if (((uint64_t)(uintptr_t)boxed & 0xFFFFFFFFull) == 0xFFFFFFFFull) {
                // Chimera: guest-side sentinel for "ignored/none" handles (seen from the
                // prebuilt guest encoder, e.g. QSRI/ignored-handle paths). Some encoders
                // preserve a high32 tag/slot, so match the low32 sentinel rather than only
                // the exact 0xFFFFFFFF value. Killing the whole emulator over it turns one
                // bad handle into a total session loss; treat it like VK_NULL_HANDLE and let
                // the host driver report any real error.
                static std::atomic<int> sChimeraSentinelUnbox{0};
                const int count = sChimeraSentinelUnbox.fetch_add(1) + 1;
                if (count % 100 == 1) {
                    GFXSTREAM_ERROR("Ignoring sentinel unbox of %s %p (count=%d)",
                                    GetTypeStr<VkObjectT>(), boxed, count);
                }
            } else {
                GFXSTREAM_FATAL("Failed to unbox %s %p", GetTypeStr<VkObjectT>(), boxed);
            }
'@ "vulkan_boxed_handles ignored-handle sentinel"
}

# --- Vulkan decoder: never echo guest fd placeholder on QSRI failure -----------
# If vkQueueSignalReleaseImageANDROID returns before the ANB path overwrites the
# out-param, the generated decoder echoes the guest's incoming placeholder (0).
# Android's QueuePresentKHR then treats fd 0 as a real fence and fdsan aborts in
# ARM64 apps running through libndk_translation (GravityMark). Default to -1.
$vkDecoderCpp = Join-Path $root "host\vulkan\vk_decoder.cpp"
if (Test-Path -LiteralPath $vkDecoderCpp -PathType Leaf) {
    Replace-Text $vkDecoderCpp @'
                vkReadStream->alloc((void**)&pNativeFenceFd, sizeof(int));
                memcpy((int*)pNativeFenceFd, *readStreamPtrPtr, sizeof(int));
                *readStreamPtrPtr += sizeof(int);
                if (m_logCalls) {
'@ @'
                vkReadStream->alloc((void**)&pNativeFenceFd, sizeof(int));
                memcpy((int*)pNativeFenceFd, *readStreamPtrPtr, sizeof(int));
                *readStreamPtrPtr += sizeof(int);
                // Chimera: never echo the guest's incoming fd placeholder back on failure.
                // Android's QueuePresentKHR treats fd 0 as a real fence and fdsan aborts.
                *pNativeFenceFd = -1;
                if (m_logCalls) {
'@ "vk_decoder QSRI native fence default"
}

# The mesh-shader blocks above cover the first observed crash; the size table
# knows ~400 more struct types than the marshaling switches (mixed-generation
# snapshot). The generator synthesizes handlers for every missing plain-POD
# struct (all VkPhysicalDevice*Features/Properties) so blind capability queries
# cannot abort the emulator.
$vkExtGen = Join-Path $PSScriptRoot "generate-chimera-vk-ext-handlers.py"
if (Test-Path -LiteralPath $vkExtGen -PathType Leaf) {
    & python $vkExtGen $root
    if ($LASTEXITCODE -ne 0) {
        throw "chimera vk extension handler generation failed"
    }
}

Write-Host "Applied Chimera gfxstream shared texture patch to $root"
