// Chimera shared D3D11 texture publisher for gfxstream Vulkan display.
#include "ChimeraGfxstreamVulkanSharedTextureBridge.h"

#include <algorithm>
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

extern "C" const char* ChimeraGfxstreamVulkanSharedTextureBridgeMarker() {
    return "ChimeraGfxstreamVulkanSharedTextureBridge";
}

extern "C" const char* ChimeraGfxstreamVulkanSharedTextureBridgeGpuCopyMarker() {
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

    VkExternalMemoryImageCreateInfo externalImage = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT,
    };
    VkImageCreateInfo imageCi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = &externalImage,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .extent = {extent.width, extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkResult res = vk.vkCreateImage(device, &imageCi, nullptr, &mImage);
    if (res != VK_SUCCESS) {
        std::fprintf(stderr, "Chimera gfxstream Vulkan bridge: vkCreateImage failed %d\n", res);
        reset(&vk, device);
        mHardUnavailable = true;
        return false;
    }

    VkMemoryRequirements reqs = {};
    vk.vkGetImageMemoryRequirements(device, mImage, &reqs);
    const uint32_t memoryType = findMemoryType(
        vk, physicalDevice, reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryType == UINT32_MAX) {
        std::fprintf(stderr, "Chimera gfxstream Vulkan bridge: no compatible memory type\n");
        reset(&vk, device);
        mHardUnavailable = true;
        return false;
    }

    VkExportMemoryWin32HandleInfoKHR win32Export = {
        .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
        .pNext = nullptr,
        .pAttributes = nullptr,
        .dwAccess = DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
        .name = mTextureName.c_str(),
    };
    VkExportMemoryAllocateInfo exportAlloc = {
        .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
        .pNext = &win32Export,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT,
    };
    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &exportAlloc,
        .allocationSize = reqs.size,
        .memoryTypeIndex = memoryType,
    };
    res = vk.vkAllocateMemory(device, &allocInfo, nullptr, &mMemory);
    if (res != VK_SUCCESS) {
        std::fprintf(stderr, "Chimera gfxstream Vulkan bridge: vkAllocateMemory failed %d\n", res);
        reset(&vk, device);
        mHardUnavailable = true;
        return false;
    }
    res = vk.vkBindImageMemory(device, mImage, mMemory, 0);
    if (res != VK_SUCCESS) {
        std::fprintf(stderr, "Chimera gfxstream Vulkan bridge: vkBindImageMemory failed %d\n", res);
        reset(&vk, device);
        mHardUnavailable = true;
        return false;
    }
    mLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vk.vkGetMemoryWin32HandleKHR) {
        VkMemoryGetWin32HandleInfoKHR handleInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR,
            .pNext = nullptr,
            .memory = mMemory,
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT,
        };
        HANDLE exportedHandle = nullptr;
        if (vk.vkGetMemoryWin32HandleKHR(device, &handleInfo, &exportedHandle) == VK_SUCCESS &&
            exportedHandle) {
            CloseHandle(exportedHandle);
        }
    }

    *static_cast<SharedD3D11TextureHeader*>(mView) = {};
    std::fprintf(stderr,
                 "Chimera gfxstream Vulkan bridge: initialized %ux%u shared texture\n",
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
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    vk.vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                            &releaseTargetBarrier);
    mLayout = VK_IMAGE_LAYOUT_GENERAL;
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
    header->dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
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

void ChimeraGfxstreamVulkanSharedTextureBridge::reset(const VulkanDispatch* vk,
                                                      VkDevice device) {
#ifdef _WIN32
    if (vk && device != VK_NULL_HANDLE) {
        if (mImage != VK_NULL_HANDLE) {
            vk->vkDestroyImage(device, mImage, nullptr);
        }
        if (mMemory != VK_NULL_HANDLE) {
            vk->vkFreeMemory(device, mMemory, nullptr);
        }
    }
    mImage = VK_NULL_HANDLE;
    mMemory = VK_NULL_HANDLE;
    mDevice = VK_NULL_HANDLE;
    mLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    mExtent = {};
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

}  // namespace vk
}  // namespace gfxstream
