// ============================================================================
// KazuEngine - Core Layer: Buffer
//
// RAII wrapper for VkBuffer + VkDeviceMemory.
// Supports CPU-visible (HOST_VISIBLE) and GPU-only (DEVICE_LOCAL) buffers.
// ============================================================================

#pragma once

#include "Context.h"
#include <vulkan/vulkan.h>

namespace kazu {

class Buffer {
public:
    Buffer(Context& ctx, VkDeviceSize size, VkBufferUsageFlags usage,
           VkMemoryPropertyFlags properties);
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    VkBuffer handle() const { return m_buffer; }
    VkDeviceSize size() const { return m_size; }
    void* mapped() const { return m_mapped; }

    // Note: With VMA, the underlying VkDeviceMemory is managed by the allocator.
    // This method is kept for API compatibility but returns VK_NULL_HANDLE.
    VkDeviceMemory memory() const { return VK_NULL_HANDLE; }

    // Upload data to CPU-visible buffer. For GPU-only buffers, use staging (TODO).
    void upload(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
    void flush();

private:
    Context* m_ctx = nullptr;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkDeviceSize m_size = 0;
    void* m_mapped = nullptr;
    VkMemoryPropertyFlags m_properties = 0;
};

} // namespace kazu
