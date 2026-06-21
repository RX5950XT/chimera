// Chimera shared D3D11 texture publisher for gfxstream Vulkan display.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "BorrowedImageVk.h"
#include "goldfish_vk_dispatch.h"
#include "aemu/base/synchronization/Lock.h"

namespace gfxstream {
namespace vk {

class ChimeraGfxstreamVulkanSharedTextureBridge {
   public:
    static ChimeraGfxstreamVulkanSharedTextureBridge& get();

    bool isEnabled() const;
    bool recordCopy(const VulkanDispatch& vk,
                    VkPhysicalDevice physicalDevice,
                    VkDevice device,
                    VkCommandBuffer commandBuffer,
                    const BorrowedImageInfoVk& source,
                    VkExtent2D targetExtent,
                    VkFilter filter);
    bool publishFrame(VkExtent2D extent);

    // Direct GPU bridge: initialized from VkEmulation, bypasses DisplayVk/CompositorVk.
    bool initDirectVkResources(const VulkanDispatch* vk,
                               VkPhysicalDevice physicalDevice,
                               VkDevice device,
                               uint32_t queueFamilyIndex,
                               VkQueue queue,
                               std::shared_ptr<android::base::Lock> queueLock);
    bool isDirectVkReady() const;
    void postFrameDirectGpu(const BorrowedImageInfoVk& src, VkExtent2D extent);
    bool postFrameCpu(const void* pixels, uint32_t width, uint32_t height, uint32_t strideBytes);

   private:
    ChimeraGfxstreamVulkanSharedTextureBridge();
    ~ChimeraGfxstreamVulkanSharedTextureBridge();

    ChimeraGfxstreamVulkanSharedTextureBridge(
        const ChimeraGfxstreamVulkanSharedTextureBridge&) = delete;
    ChimeraGfxstreamVulkanSharedTextureBridge& operator=(
        const ChimeraGfxstreamVulkanSharedTextureBridge&) = delete;

    bool ensureInitialized(const VulkanDispatch& vk,
                           VkPhysicalDevice physicalDevice,
                           VkDevice device,
                           VkExtent2D extent);
    bool ensureD3D11Initialized(VkExtent2D extent);
    void reset(const VulkanDispatch* vk = nullptr, VkDevice device = VK_NULL_HANDLE);

    bool mEnabled = false;
    bool mHardUnavailable = false;
    std::wstring mMetadataName;
    std::wstring mTextureName;
    std::wstring mEventName;
    void* mMapping = nullptr;
    void* mEvent = nullptr;
    void* mView = nullptr;
    // D3D11 is sole owner (no Vulkan import); Vulkan blits into a separate device-local image,
    // then copies to a host-visible staging buffer, then UpdateSubresource into the D3D11 texture.
    void* mD3D11Device = nullptr;        // ID3D11Device*
    void* mD3D11Texture = nullptr;       // ID3D11Texture2D*
    void* mD3D11SharedHandle = nullptr;  // HANDLE — kept open to preserve NT name lifetime
    void* mD3D11Context = nullptr;       // ID3D11DeviceContext* (immediate)
    VkDevice mDevice = VK_NULL_HANDLE;
    VkImage mImage = VK_NULL_HANDLE;
    VkDeviceMemory mMemory = VK_NULL_HANDLE;
    VkBuffer mStagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory mStagingMemory = VK_NULL_HANDLE;
    void* mStagingData = nullptr;  // persistently mapped HOST_COHERENT staging pointer
    VkImageLayout mLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkExtent2D mExtent = {};
    uint64_t mSequence = 0;
    uint64_t mRecordAttempts = 0;
    uint64_t mRecordSuccesses = 0;
    uint64_t mPublishAttempts = 0;
    uint64_t mPublishFailures = 0;

    // Direct GPU bridge resources (no DisplayVk/CompositorVk)
    bool mDirectVkReady = false;
    const VulkanDispatch* mDirectVk = nullptr;
    VkPhysicalDevice mDirectPhysDev = VK_NULL_HANDLE;
    VkDevice mDirectDevice = VK_NULL_HANDLE;
    uint32_t mDirectQueueFamilyIndex = 0;
    VkQueue mDirectQueue = VK_NULL_HANDLE;
    std::shared_ptr<android::base::Lock> mDirectQueueLock;
    VkCommandPool mDirectCommandPool = VK_NULL_HANDLE;
    VkCommandBuffer mDirectCommandBuffer = VK_NULL_HANDLE;
    VkFence mDirectFence = VK_NULL_HANDLE;
};

}  // namespace vk
}  // namespace gfxstream
