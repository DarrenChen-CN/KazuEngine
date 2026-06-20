// ============================================================================
// KazuEngine - Core Layer: Image
//
// RAII wrapper for VkImage + VkDeviceMemory + VkImageView.
// ============================================================================

#pragma once

#include "Context.h"
#include <vector>

namespace kazu {

struct ImageDesc {
    VkImageType type = VK_IMAGE_TYPE_2D;
    VkExtent3D extent = {1, 1, 1};
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags usage = 0;
    VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkImageCreateFlags flags = 0;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
};

struct ImageViewDesc {
    VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
    VkFormat format = VK_FORMAT_UNDEFINED; // Use image format if undefined.
    uint32_t baseMipLevel = 0;
    uint32_t levelCount = 1;
    uint32_t baseArrayLayer = 0;
    uint32_t layerCount = 1;
    VkImageAspectFlags aspectMask = 0; // Auto-detect if 0.
};

class Image {
public:
    explicit Image(Context& ctx, const ImageDesc& desc);

    // Convenience overload for the most common 2D single-mip case.
    Image(Context& ctx, uint32_t width, uint32_t height, VkFormat format,
          VkImageUsageFlags usage, VkMemoryPropertyFlags properties);

    ~Image();

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;

    VkImage handle() const { return m_image; }

    // Default view covering the whole image (mip 0, all layers).
    VkImageView view() const { return m_defaultView; }

    // Create an additional view with custom subresource range. The view is owned
    // by this Image and destroyed when the Image is destroyed.
    VkImageView createView(const ImageViewDesc& desc);

    const ImageDesc& desc() const { return m_desc; }
    VkFormat format() const { return m_desc.format; }
    VkExtent2D extent2D() const { return {m_desc.extent.width, m_desc.extent.height}; }
    VkExtent3D extent() const { return m_desc.extent; }

    // Note: With VMA, the underlying VkDeviceMemory is managed by the allocator.
    VkDeviceMemory memory() const { return VK_NULL_HANDLE; }

    void transitionLayout(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout);

private:
    void createDefaultView();
    VkImageView createViewInternal(const ImageViewDesc& desc) const;

    Context* m_ctx = nullptr;
    ImageDesc m_desc{};
    VkImage m_image = VK_NULL_HANDLE;
    VkImageView m_defaultView = VK_NULL_HANDLE;
    std::vector<VkImageView> m_extraViews;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
};

} // namespace kazu
