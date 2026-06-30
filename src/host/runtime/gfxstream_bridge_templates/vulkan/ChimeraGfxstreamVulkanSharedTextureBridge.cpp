// Chimera shared D3D11 texture publisher for gfxstream Vulkan display.
#include "ChimeraGfxstreamVulkanSharedTextureBridge.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
#else
#include <strings.h>
#endif

namespace gfxstream {
namespace vk {
namespace {

constexpr uint32_t kD3D11TextureMagic = 0x43485458;  // CHTX
constexpr uint32_t kD3D11TextureVersion = 1;
constexpr uint32_t kD3D11FlagHasAlpha = 0x1;
constexpr uint32_t kD3D11TextureNameChars = 260;
constexpr uint32_t kMinimumSharedTextureWidth = 1920;
constexpr uint32_t kMinimumSharedTextureHeight = 1080;

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

bool shouldLogCounter(uint64_t count) {
    return count == 1 || count == 60 || (count % 240) == 0;
}

#ifdef _WIN32
static_assert(sizeof(SharedD3D11TextureHeader) == 560,
              "shared D3D11 texture header ABI must match Chimera host");

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
             L"Local\\ChimeraGfxstreamVkD3D11Texture_%lu", GetCurrentProcessId());
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

uint32_t findMemoryType(const VulkanDispatch& vk,
                        VkPhysicalDevice physicalDevice,
                        uint32_t typeBits,
                        VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties props = {};
    vk.vkGetPhysicalDeviceMemoryProperties(physicalDevice, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        const bool typeOk = (typeBits & (1u << i)) != 0;
        const bool flagsOk = (props.memoryTypes[i].propertyFlags & required) == required;
        if (typeOk && flagsOk) return i;
    }
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if (typeBits & (1u << i)) return i;
    }
    return UINT32_MAX;
}
#endif

}  // namespace

extern "C" __declspec(dllexport) const char* ChimeraGfxstreamVulkanSharedTextureBridgeMarker() {
    return "ChimeraGfxstreamVulkanSharedTextureBridge";
}

extern "C" __declspec(dllexport) const char* ChimeraGfxstreamVulkanSharedTextureBridgeGpuCopyMarker() {
    return "ChimeraGfxstreamVulkanSharedTextureBridgeGpuCopy";
}

ChimeraGfxstreamVulkanSharedTextureBridge&
ChimeraGfxstreamVulkanSharedTextureBridge::get() {
    static ChimeraGfxstreamVulkanSharedTextureBridge bridge;
    return bridge;
}

ChimeraGfxstreamVulkanSharedTextureBridge::
ChimeraGfxstreamVulkanSharedTextureBridge() {
#ifdef _WIN32
    mMetadataName = firstEnv("CHIMERA_GFXSTREAM_D3D11_TEXTURE_METADATA",
                             "CHIMERA_D3D11_TEXTURE_METADATA");
    mTextureName = firstEnv("CHIMERA_GFXSTREAM_D3D11_TEXTURE_NAME",
                            "CHIMERA_D3D11_TEXTURE_NAME");
    if (mTextureName.empty()) mTextureName = defaultTextureName();
    mEventName = firstEnv("CHIMERA_GFXSTREAM_D3D11_TEXTURE_EVENT",
                          "CHIMERA_D3D11_TEXTURE_EVENT");
    mEnabled = !mMetadataName.empty();
    if (envTruthy("CHIMERA_DISABLE_GFXSTREAM_VK_D3D11_TEXTURE")) {
        mEnabled = false;
    }
    if (mEnabled) {
        std::fprintf(stderr,
                     "Chimera gfxstream Vulkan bridge: enabled metadata=%ls texture=%ls event=%ls\n",
                     mMetadataName.c_str(), mTextureName.c_str(), mEventName.c_str());
    }
#endif
}

ChimeraGfxstreamVulkanSharedTextureBridge::
~ChimeraGfxstreamVulkanSharedTextureBridge() {
    reset();
}

bool ChimeraGfxstreamVulkanSharedTextureBridge::isEnabled() const {
    return mEnabled;
}

bool ChimeraGfxstreamVulkanSharedTextureBridge::ensureD3D11Initialized(VkExtent2D extent) {
#ifndef _WIN32
    (void)extent;
    return false;
#else
    if (!mEnabled || mHardUnavailable) return false;
    if (extent.width < kMinimumSharedTextureWidth || extent.height < kMinimumSharedTextureHeight) {
        std::fprintf(stderr,
                     "Chimera gfxstream Vulkan bridge: refusing undersized D3D11 texture %ux%u\n",
                     extent.width, extent.height);
        return false;
    }
    if (mD3D11Texture && mExtent.width == extent.width && mExtent.height == extent.height) {
        return true;
    }

    reset();
    mExtent = extent;
    mMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                  0, sizeof(SharedD3D11TextureHeader),
                                  mMetadataName.c_str());
    if (!mMapping) {
        std::fprintf(stderr, "Chimera gfxstream Vulkan bridge: CreateFileMappingW failed %lu\n",
                     GetLastError());
        mHardUnavailable = true;
        return false;
    }
    mView = MapViewOfFile(static_cast<HANDLE>(mMapping), FILE_MAP_WRITE, 0, 0,
                          sizeof(SharedD3D11TextureHeader));
    if (!mView) {
        std::fprintf(stderr, "Chimera gfxstream Vulkan bridge: MapViewOfFile failed %lu\n",
                     GetLastError());
        reset();
        mHardUnavailable = true;
        return false;
    }
    if (!mEventName.empty()) {
        mEvent = CreateEventW(nullptr, FALSE, FALSE, mEventName.c_str());
        if (!mEvent) {
            std::fprintf(stderr, "Chimera gfxstream Vulkan bridge: CreateEventW failed %lu\n",
                         GetLastError());
            reset();
            mHardUnavailable = true;
            return false;
        }
    }

    ID3D11Device* d3dDevice = nullptr;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                   nullptr, 0, D3D11_SDK_VERSION, &d3dDevice, nullptr, nullptr);
    if (FAILED(hr) || !d3dDevice) {
        std::fprintf(stderr, "Chimera gfxstream Vulkan bridge: D3D11CreateDevice failed hr=0x%lx\n",
                     static_cast<unsigned long>(hr));
        reset();
        mHardUnavailable = true;
        return false;
    }

    ID3D11Texture2D* d3dTex = nullptr;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = extent.width;
    desc.Height = extent.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    hr = d3dDevice->CreateTexture2D(&desc, nullptr, &d3dTex);
    if (FAILED(hr) || !d3dTex) {
        std::fprintf(stderr, "Chimera gfxstream Vulkan bridge: CreateTexture2D failed hr=0x%lx\n",
                     static_cast<unsigned long>(hr));
        d3dDevice->Release();
        reset();
        mHardUnavailable = true;
        return false;
    }

    HANDLE sharedHandle = nullptr;
    IDXGIResource1* dxgiRes = nullptr;
    hr = d3dTex->QueryInterface(__uuidof(IDXGIResource1), reinterpret_cast<void**>(&dxgiRes));
    if (SUCCEEDED(hr) && dxgiRes) {
        hr = dxgiRes->CreateSharedHandle(nullptr,
                                         DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                         mTextureName.c_str(), &sharedHandle);
        dxgiRes->Release();
    }
    if (FAILED(hr) || !sharedHandle) {
        std::fprintf(stderr, "Chimera gfxstream Vulkan bridge: CreateSharedHandle failed hr=0x%lx\n",
                     static_cast<unsigned long>(hr));
        d3dTex->Release();
        d3dDevice->Release();
        reset();
        mHardUnavailable = true;
        return false;
    }

    ID3D11DeviceContext* d3dCtx = nullptr;
    d3dDevice->GetImmediateContext(&d3dCtx);
    if (!d3dCtx) {
        std::fprintf(stderr, "Chimera gfxstream Vulkan bridge: GetImmediateContext failed\n");
        CloseHandle(sharedHandle);
        d3dTex->Release();
        d3dDevice->Release();
        reset();
        mHardUnavailable = true;
        return false;
    }

    mD3D11Device = d3dDevice;
    mD3D11Texture = d3dTex;
    mD3D11SharedHandle = sharedHandle;
    mD3D11Context = d3dCtx;
    *static_cast<SharedD3D11TextureHeader*>(mView) = {};
    std::fprintf(stderr,
                 "Chimera gfxstream Vulkan bridge: initialized %ux%u shared texture (D3D11 CPU path)\n",
                 extent.width, extent.height);
    return true;
#endif
}

bool ChimeraGfxstreamVulkanSharedTextureBridge::ensureInitialized(
    const VulkanDispatch& vk,
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkExtent2D extent) {
#ifndef _WIN32
    (void)vk;
    (void)physicalDevice;
    (void)device;
    (void)extent;
    return false;
#else
    if (!mEnabled || mHardUnavailable) return false;
    if (extent.width < kMinimumSharedTextureWidth ||
        extent.height < kMinimumSharedTextureHeight) {
        std::fprintf(stderr,
                     "Chimera gfxstream Vulkan bridge: refusing undersized texture %ux%u\n",
                     extent.width, extent.height);
        return false;
    }
    if (mImage != VK_NULL_HANDLE &&
        mDevice == device &&
        mExtent.width == extent.width &&
        mExtent.height == extent.height) {
        return true;
    }

    reset(&vk, device);
    mDevice = device;
    mExtent = extent;

    mMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                  0, sizeof(SharedD3D11TextureHeader),
                                  mMetadataName.c_str());
    if (!mMapping) {
        std::fprintf(stderr, "Chimera gfxstream Vulkan bridge: CreateFileMappingW failed %lu\n",
                     GetLastError());
        mHardUnavailable = true;
        return false;
    }
    mView = MapViewOfFile(static_cast<HANDLE>(mMapping), FILE_MAP_WRITE, 0, 0,
                          sizeof(SharedD3D11TextureHeader));
    if (!mView) {
        std::fprintf(stderr, "Chimera gfxstream Vulkan bridge: MapViewOfFile failed %lu\n",
                     GetLastError());
        reset(&vk, device);
        mHardUnavailable = true;
        return false;
    }
    if (!mEventName.empty()) {
        mEvent = CreateEventW(nullptr, FALSE, FALSE, mEventName.c_str());
        if (!mEvent) {
            std::fprintf(stderr, "Chimera gfxstream Vulkan bridge: CreateEventW failed %lu\n",
                         GetLastError());
            reset(&vk, device);
            mHardUnavailable = true;
            return false;
        }
    }

    // D3D11 is the primary owner of the named shared texture; Vulkan imports the
    // same NT handle and blits directly into it. This is the 60 FPS path: no
    // HOST_COHERENT staging buffer and no D3D11 UpdateSubresource copy.
    ID3D11Device* d3dDevice = nullptr;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                   nullptr, 0, D3D11_SDK_VERSION, &d3dDevice, nullptr, nullptr);
    if (FAILED(hr) || !d3dDevice) {
        std::fprintf(stderr, "Chimera gfxstream Vulkan bridge: D3D11CreateDevice failed hr=0x%lx\n",
                     static_cast<unsigned long>(hr));
        reset(&vk, device);
        mHardUnavailable = true;
        return false;
    }

    ID3D11Texture2D* d3dTex = nullptr;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = extent.width;
    desc.Height = extent.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
                     D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    hr = d3dDevice->CreateTexture2D(&desc, nullptr, &d3dTex);
    if (FAILED(hr) || !d3dTex) {
        std::fprintf(stderr, "Chimera gfxstream Vulkan bridge: CreateTexture2D failed hr=0x%lx\n",
                     static_cast<unsigned long>(hr));
        d3dDevice->Release();
        reset(&vk, device);
        mHardUnavailable = true;
        return false;
    }

    HANDLE sharedHandle = nullptr;
    IDXGIResource1* dxgiRes = nullptr;
    hr = d3dTex->QueryInterface(__uuidof(IDXGIResource1), reinterpret_cast<void**>(&dxgiRes));
    if (FAILED(hr) || !dxgiRes) {
        std::fprintf(stderr, "Chimera gfxstream Vulkan bridge: IDXGIResource1 unavailable hr=0x%lx\n",
                     static_cast<unsigned long>(hr));
        d3dTex->Release();
        d3dDevice->Release();
        reset(&vk, device);
        mHardUnavailable = true;
        return false;
    }
    hr = dxgiRes->CreateSharedHandle(nullptr,
                                     DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                     mTextureName.c_str(), &sharedHandle);
    dxgiRes->Release();
    if (FAILED(hr) || !sharedHandle) {
        std::fprintf(stderr, "Chimera gfxstream Vulkan bridge: CreateSharedHandle failed hr=0x%lx\n",
                     static_cast<unsigned long>(hr));
        d3dTex->Release();
        d3dDevice->Release();
        reset(&vk, device);
        mHardUnavailable = true;
        return false;
    }

    VkExternalMemoryImageCreateInfo externalImageCi = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT,
    };
    VkImageCreateInfo imageCi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = &externalImageCi,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {extent.width, extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkResult res = vk.vkCreateImage(device, &imageCi, nullptr, &mImage);
    if (res != VK_SUCCESS) {
        std::fprintf(stderr, "Chimera gfxstream Vulkan bridge: vkCreateImage(GPU-direct) failed %d\n", res);
        CloseHandle(sharedHandle);
        d3dTex->Release();
        d3dDevice->Release();
        reset(&vk, device);
        mHardUnavailable = true;
        return false;
    }

    VkMemoryRequirements reqs = {};
    vk.vkGetImageMemoryRequirements(device, mImage, &reqs);
    // Some Windows Vulkan drivers reject vkGetMemoryWin32HandlePropertiesKHR for
    // D3D11-created NT handles even though import succeeds. Use the image memory
    // requirements directly; this matched the previously verified GPU-direct path
    // (memType=1 on the target NVIDIA system).
    const uint32_t memoryType = findMemoryType(
        vk, physicalDevice, reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryType == UINT32_MAX) {
        std::fprintf(stderr, "Chimera gfxstream Vulkan bridge: no GPU-direct memory type\n");
        CloseHandle(sharedHandle);
        d3dTex->Release();
        d3dDevice->Release();
        reset(&vk, device);
        mHardUnavailable = true;
        return false;
    }

    VkImportMemoryWin32HandleInfoKHR importInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
        .pNext = nullptr,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT,
        .handle = sharedHandle,
        .name = nullptr,
    };
    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importInfo,
        .allocationSize = reqs.size,
        .memoryTypeIndex = memoryType,
    };
    res = vk.vkAllocateMemory(device, &allocInfo, nullptr, &mMemory);
    if (res != VK_SUCCESS) {
        std::fprintf(stderr, "Chimera gfxstream Vulkan bridge: vkAllocateMemory(import D3D11) failed %d\n", res);
        CloseHandle(sharedHandle);
        d3dTex->Release();
        d3dDevice->Release();
        reset(&vk, device);
        mHardUnavailable = true;
        return false;
    }
    res = vk.vkBindImageMemory(device, mImage, mMemory, 0);
    if (res != VK_SUCCESS) {
        std::fprintf(stderr, "Chimera gfxstream Vulkan bridge: vkBindImageMemory(GPU-direct) failed %d\n", res);
        CloseHandle(sharedHandle);
        d3dTex->Release();
        d3dDevice->Release();
        reset(&vk, device);
        mHardUnavailable = true;
        return false;
    }

    mD3D11Device = d3dDevice;
    mD3D11Texture = d3dTex;
    mD3D11SharedHandle = sharedHandle;
    mD3D11Context = nullptr;
    *static_cast<SharedD3D11TextureHeader*>(mView) = {};
    std::fprintf(stderr,
                 "[chimera-gfxstream] GPU-direct D3D11 import OK %ux%u memType=%u\n",
                 extent.width, extent.height, memoryType);
    std::fprintf(stderr,
                 "Chimera gfxstream Vulkan bridge: initialized %ux%u shared texture (GPU-direct)\n",
                 extent.width, extent.height);
    return true;
#endif
}

bool ChimeraGfxstreamVulkanSharedTextureBridge::recordCopy(
    const VulkanDispatch& vk,
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkCommandBuffer commandBuffer,
    const BorrowedImageInfoVk& source,
    VkExtent2D targetExtent,
    VkFilter filter) {
#ifndef _WIN32
    (void)vk;
    (void)physicalDevice;
    (void)device;
    (void)commandBuffer;
    (void)source;
    (void)targetExtent;
    (void)filter;
    return false;
#else
    ++mRecordAttempts;
    if (!ensureInitialized(vk, physicalDevice, device, targetExtent)) {
        if (shouldLogCounter(mRecordAttempts)) {
            std::fprintf(stderr,
                         "Chimera gfxstream Vulkan bridge: recordCopy unavailable attempts=%llu target=%ux%u source=%ux%u hard=%d enabled=%d\n",
                         static_cast<unsigned long long>(mRecordAttempts),
                         targetExtent.width, targetExtent.height,
                         source.imageCreateInfo.extent.width,
                         source.imageCreateInfo.extent.height,
                         mHardUnavailable ? 1 : 0, mEnabled ? 1 : 0);
        }
        return false;
    }

    VkImageMemoryBarrier acquireTargetBarrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = mLayout,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = mImage,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    vk.vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                            &acquireTargetBarrier);

    const VkImageBlit region = {
        .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .mipLevel = 0,
                           .baseArrayLayer = 0,
                           .layerCount = 1},
        .srcOffsets = {{0, 0, 0},
                       {static_cast<int32_t>(source.imageCreateInfo.extent.width),
                        static_cast<int32_t>(source.imageCreateInfo.extent.height), 1}},
        .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .mipLevel = 0,
                           .baseArrayLayer = 0,
                           .layerCount = 1},
        .dstOffsets = {{0, 0, 0},
                       {static_cast<int32_t>(targetExtent.width),
                        static_cast<int32_t>(targetExtent.height), 1}},
    };
    vk.vkCmdBlitImage(commandBuffer, source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      mImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, filter);

    if (mStagingBuffer != VK_NULL_HANDLE) {
        // Transition mImage from TRANSFER_DST_OPTIMAL → TRANSFER_SRC_OPTIMAL for the buffer copy.
        VkImageMemoryBarrier toSrcBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = mImage,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        vk.vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                                1, &toSrcBarrier);

        VkBufferImageCopy copyRegion = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .imageOffset = {0, 0, 0},
            .imageExtent = {targetExtent.width, targetExtent.height, 1},
        };
        vk.vkCmdCopyImageToBuffer(commandBuffer, mImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  mStagingBuffer, 1, &copyRegion);

        // Ensure the staging buffer write is visible to the host after the fence signals.
        VkBufferMemoryBarrier bufBarrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = mStagingBuffer,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        };
        vk.vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr,
                                1, &bufBarrier, 0, nullptr);
        mLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    } else {
        // DisplayVk path: no staging buffer, just transition to GENERAL.
        VkImageMemoryBarrier releaseTargetBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = mImage,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        vk.vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr,
                                1, &releaseTargetBarrier);
        mLayout = VK_IMAGE_LAYOUT_GENERAL;
    }
    ++mRecordSuccesses;
    if (shouldLogCounter(mRecordSuccesses)) {
        std::fprintf(stderr,
                     "Chimera gfxstream Vulkan bridge: recordCopy ok successes=%llu target=%ux%u source=%ux%u\n",
                     static_cast<unsigned long long>(mRecordSuccesses),
                     targetExtent.width, targetExtent.height,
                     source.imageCreateInfo.extent.width,
                     source.imageCreateInfo.extent.height);
    }
    return true;
#endif
}

bool ChimeraGfxstreamVulkanSharedTextureBridge::publishFrame(VkExtent2D extent) {
#ifndef _WIN32
    (void)extent;
    return false;
#else
    ++mPublishAttempts;
    if (!mView) {
        ++mPublishFailures;
        if (shouldLogCounter(mPublishFailures)) {
            std::fprintf(stderr,
                         "Chimera gfxstream Vulkan bridge: publish without metadata view failures=%llu attempts=%llu\n",
                         static_cast<unsigned long long>(mPublishFailures),
                         static_cast<unsigned long long>(mPublishAttempts));
        }
        return false;
    }
    auto* header = static_cast<SharedD3D11TextureHeader*>(mView);
    const uint64_t nextSequence = mSequence + 2;
    header->sequence = nextSequence | 1ULL;
    MemoryBarrier();
    header->magic = kD3D11TextureMagic;
    header->version = kD3D11TextureVersion;
    header->headerSize = sizeof(SharedD3D11TextureHeader);
    header->width = extent.width;
    header->height = extent.height;
    header->dxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    header->flags = kD3D11FlagHasAlpha;
    if (!writeTextureName(header->textureName, mTextureName)) {
        ++mPublishFailures;
        if (shouldLogCounter(mPublishFailures)) {
            std::fprintf(stderr,
                         "Chimera gfxstream Vulkan bridge: publish failed invalid texture name failures=%llu attempts=%llu\n",
                         static_cast<unsigned long long>(mPublishFailures),
                         static_cast<unsigned long long>(mPublishAttempts));
        }
        return false;
    }
    MemoryBarrier();
    header->sequence = nextSequence;
    mSequence = nextSequence;
    if (mEvent) SetEvent(static_cast<HANDLE>(mEvent));
    if (mSequence == 2 || (mSequence % 240) == 0) {
        std::fprintf(stderr,
                     "Chimera gfxstream Vulkan bridge: published sequence=%llu size=%ux%u\n",
                     static_cast<unsigned long long>(mSequence), extent.width, extent.height);
    }
    return true;
#endif
}

bool ChimeraGfxstreamVulkanSharedTextureBridge::postFrameCpu(
    const void* pixels, uint32_t width, uint32_t height, uint32_t strideBytes) {
#ifndef _WIN32
    (void)pixels; (void)width; (void)height; (void)strideBytes;
    return false;
#else
    if (!pixels || width == 0 || height == 0 || strideBytes < width * 4) return false;
    const VkExtent2D extent = {width, height};
    if (!ensureD3D11Initialized(extent)) return false;
    auto* ctx = static_cast<ID3D11DeviceContext*>(mD3D11Context);
    auto* tex = static_cast<ID3D11Texture2D*>(mD3D11Texture);
    if (!ctx || !tex) return false;
    ctx->UpdateSubresource(tex, 0, nullptr, pixels, strideBytes, 0);
    return publishFrame(extent);
#endif
}

void ChimeraGfxstreamVulkanSharedTextureBridge::reset(const VulkanDispatch* vk,
                                                      VkDevice device) {
#ifdef _WIN32
    if (vk && device != VK_NULL_HANDLE) {
        if (mStagingData) {
            vk->vkUnmapMemory(device, mStagingMemory);
            mStagingData = nullptr;
        }
        if (mStagingBuffer != VK_NULL_HANDLE) {
            vk->vkDestroyBuffer(device, mStagingBuffer, nullptr);
            mStagingBuffer = VK_NULL_HANDLE;
        }
        if (mStagingMemory != VK_NULL_HANDLE) {
            vk->vkFreeMemory(device, mStagingMemory, nullptr);
            mStagingMemory = VK_NULL_HANDLE;
        }
        if (mImage != VK_NULL_HANDLE) {
            vk->vkDestroyImage(device, mImage, nullptr);
        }
        if (mMemory != VK_NULL_HANDLE) {
            vk->vkFreeMemory(device, mMemory, nullptr);
        }
    }
    mStagingData = nullptr;
    mStagingBuffer = VK_NULL_HANDLE;
    mStagingMemory = VK_NULL_HANDLE;
    mImage = VK_NULL_HANDLE;
    mMemory = VK_NULL_HANDLE;
    mDevice = VK_NULL_HANDLE;
    mLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    mExtent = {};
    // Release D3D11 resources.
    if (mD3D11Context) {
        static_cast<ID3D11DeviceContext*>(mD3D11Context)->Release();
        mD3D11Context = nullptr;
    }
    if (mD3D11SharedHandle) {
        CloseHandle(static_cast<HANDLE>(mD3D11SharedHandle));
        mD3D11SharedHandle = nullptr;
    }
    if (mD3D11Texture) {
        static_cast<ID3D11Texture2D*>(mD3D11Texture)->Release();
        mD3D11Texture = nullptr;
    }
    if (mD3D11Device) {
        static_cast<ID3D11Device*>(mD3D11Device)->Release();
        mD3D11Device = nullptr;
    }
    if (mView) {
        UnmapViewOfFile(mView);
        mView = nullptr;
    }
    if (mEvent) {
        CloseHandle(static_cast<HANDLE>(mEvent));
        mEvent = nullptr;
    }
    if (mMapping) {
        CloseHandle(static_cast<HANDLE>(mMapping));
        mMapping = nullptr;
    }
    mSequence = 0;
#else
    (void)vk;
    (void)device;
#endif
}

bool ChimeraGfxstreamVulkanSharedTextureBridge::initDirectVkResources(
    const VulkanDispatch* vk,
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    uint32_t queueFamilyIndex,
    VkQueue queue,
    std::shared_ptr<android::base::Lock> queueLock) {
#ifndef _WIN32
    (void)vk; (void)physicalDevice; (void)device; (void)queueFamilyIndex;
    (void)queue; (void)queueLock;
    return false;
#else
    if (!mEnabled || !vk || device == VK_NULL_HANDLE || queue == VK_NULL_HANDLE) {
        return false;
    }
    if (mDirectVkReady) return true;

    mDirectVk = vk;
    mDirectPhysDev = physicalDevice;
    mDirectDevice = device;
    mDirectQueueFamilyIndex = queueFamilyIndex;
    mDirectQueue = queue;
    mDirectQueueLock = queueLock;

    VkCommandPoolCreateInfo poolCI = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queueFamilyIndex,
    };
    VkResult res = vk->vkCreateCommandPool(device, &poolCI, nullptr, &mDirectCommandPool);
    if (res != VK_SUCCESS) {
        std::fprintf(stderr, "Chimera Vulkan bridge: vkCreateCommandPool failed %d\n", res);
        return false;
    }

    VkCommandBufferAllocateInfo cbAI = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = mDirectCommandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    res = vk->vkAllocateCommandBuffers(device, &cbAI, &mDirectCommandBuffer);
    if (res != VK_SUCCESS) {
        std::fprintf(stderr, "Chimera Vulkan bridge: vkAllocateCommandBuffers failed %d\n", res);
        vk->vkDestroyCommandPool(device, mDirectCommandPool, nullptr);
        mDirectCommandPool = VK_NULL_HANDLE;
        return false;
    }

    VkFenceCreateInfo fenceCI = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    res = vk->vkCreateFence(device, &fenceCI, nullptr, &mDirectFence);
    if (res != VK_SUCCESS) {
        std::fprintf(stderr, "Chimera Vulkan bridge: vkCreateFence failed %d\n", res);
        vk->vkDestroyCommandPool(device, mDirectCommandPool, nullptr);
        mDirectCommandPool = VK_NULL_HANDLE;
        mDirectCommandBuffer = VK_NULL_HANDLE;
        return false;
    }

    mDirectVkReady = true;
    std::fprintf(stderr, "[chimera-display-vk] Direct GPU bridge resources initialized\n");
    return true;
#endif
}

bool ChimeraGfxstreamVulkanSharedTextureBridge::isDirectVkReady() const {
    return mDirectVkReady;
}

void ChimeraGfxstreamVulkanSharedTextureBridge::postFrameDirectGpu(
    const BorrowedImageInfoVk& src, VkExtent2D extent) {
#ifndef _WIN32
    (void)src; (void)extent;
#else
    if (!mDirectVkReady || !mDirectVk) return;
    static std::atomic<int> sDirectFrameCount{0};
    const int frameIndex = sDirectFrameCount.fetch_add(1) + 1;
    const bool logTiming = (frameIndex == 1 || frameIndex % 120 == 0);
    const auto tStart = std::chrono::steady_clock::now();
    const VulkanDispatch& vk = *mDirectVk;

    // Wait for previous submission, then reset fence.
    vk.vkWaitForFences(mDirectDevice, 1, &mDirectFence, VK_TRUE, UINT64_MAX);
    const auto tPrevFence = std::chrono::steady_clock::now();
    vk.vkResetFences(mDirectDevice, 1, &mDirectFence);
    vk.vkResetCommandBuffer(mDirectCommandBuffer, 0);

    // Compute pre-acquire barriers for the source image (layout → TRANSFER_SRC_OPTIMAL).
    std::vector<VkImageMemoryBarrier> preAcqQueue, preAcqLayout, postRelLayout, postRelQueue;
    addNeededBarriersToUseBorrowedImage(
        src, mDirectQueueFamilyIndex,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_TRANSFER_READ_BIT,
        &preAcqQueue, &preAcqLayout, &postRelLayout, &postRelQueue);

    VkCommandBuffer cmdBuf = mDirectCommandBuffer;
    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vk.vkBeginCommandBuffer(cmdBuf, &beginInfo);

    constexpr VkPipelineStageFlags kMemStages =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (!preAcqQueue.empty()) {
        vk.vkCmdPipelineBarrier(cmdBuf, kMemStages, kMemStages, 0, 0, nullptr, 0, nullptr,
                                static_cast<uint32_t>(preAcqQueue.size()), preAcqQueue.data());
    }
    if (!preAcqLayout.empty()) {
        vk.vkCmdPipelineBarrier(cmdBuf, kMemStages, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                                nullptr, 0, nullptr,
                                static_cast<uint32_t>(preAcqLayout.size()), preAcqLayout.data());
    }

    const bool copied = recordCopy(vk, mDirectPhysDev, mDirectDevice, cmdBuf,
                                   src, extent, VK_FILTER_LINEAR);
    const auto tRecordCopy = std::chrono::steady_clock::now();

    if (!postRelLayout.empty()) {
        vk.vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
                                static_cast<uint32_t>(postRelLayout.size()), postRelLayout.data());
    }
    if (!postRelQueue.empty()) {
        vk.vkCmdPipelineBarrier(cmdBuf, kMemStages, kMemStages, 0, 0, nullptr, 0, nullptr,
                                static_cast<uint32_t>(postRelQueue.size()), postRelQueue.data());
    }

    vk.vkEndCommandBuffer(cmdBuf);

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmdBuf,
    };
    {
        android::base::AutoLock lock(*mDirectQueueLock);
        vk.vkQueueSubmit(mDirectQueue, 1, &submitInfo, mDirectFence);
    }
    const auto tSubmit = std::chrono::steady_clock::now();

    if (copied) {
        vk.vkWaitForFences(mDirectDevice, 1, &mDirectFence, VK_TRUE, UINT64_MAX);
        const auto tFence = std::chrono::steady_clock::now();
        publishFrame(extent);
        const auto tPublish = std::chrono::steady_clock::now();
        if (logTiming) {
            const auto ms = [](auto a, auto b) {
                return std::chrono::duration<double, std::milli>(b - a).count();
            };
            std::fprintf(stderr,
                "[chimera-timing] postFrameDirectGpu: prevFence=%.1fms submit=%.1fms fence=%.1fms pub=%.1fms total=%.1fms path=GPU-direct\n",
                ms(tStart, tPrevFence), ms(tRecordCopy, tSubmit), ms(tSubmit, tFence),
                ms(tFence, tPublish), ms(tStart, tPublish));
            std::fflush(stderr);
        }
    }
#endif
}

}  // namespace vk
}  // namespace gfxstream
