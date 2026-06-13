// Chimera shared D3D11 texture publisher for gfxstream Vulkan display.
#pragma once

#include <cstdint>
#include <string>

#include "BorrowedImageVk.h"
#include "goldfish_vk_dispatch.h"

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
    void reset(const VulkanDispatch* vk = nullptr, VkDevice device = VK_NULL_HANDLE);

    bool mEnabled = false;
    bool mHardUnavailable = false;
    std::wstring mMetadataName;
    std::wstring mTextureName;
    std::wstring mEventName;
    void* mMapping = nullptr;
    void* mEvent = nullptr;
    void* mView = nullptr;
    VkDevice mDevice = VK_NULL_HANDLE;
    VkImage mImage = VK_NULL_HANDLE;
    VkDeviceMemory mMemory = VK_NULL_HANDLE;
    VkImageLayout mLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkExtent2D mExtent = {};
    uint64_t mSequence = 0;
    uint64_t mRecordAttempts = 0;
    uint64_t mRecordSuccesses = 0;
    uint64_t mPublishAttempts = 0;
    uint64_t mPublishFailures = 0;
};

}  // namespace vk
}  // namespace gfxstream
