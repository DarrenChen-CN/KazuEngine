// ============================================================================
// KazuEngine - Core Layer: Image
//
// RAII wrapper for VkImage + VkDeviceMemory + VkImageView.
// ============================================================================

#pragma once

#include "Context.h"

namespace kazu {

class Image {
public:
    Image(Context& ctx, uint32_t width, uint32_t height, VkFormat format,
          VkImageUsageFlags usage, VkMemoryPropertyFlags properties);
    ~Image();

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;

    VkImage handle() const { return m_image; }
    VkImageView view() const { return m_view; }
    VkFormat format() const { return m_format; }
    VkExtent2D extent() const { return m_extent; }

    // Note: With VMA, the underlying VkDeviceMemory is managed by the allocator.
    VkDeviceMemory memory() const { return VK_NULL_HANDLE; }

    void transitionLayout(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout);

private:
    Context* m_ctx = nullptr;
    VkImage m_image = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkFormat m_format;
    VkExtent2D m_extent;
};

} // namespace kazu
