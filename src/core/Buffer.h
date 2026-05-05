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
    VkDeviceMemory memory() const { return m_memory; }
    VkDeviceSize size() const { return m_size; }
    void* mapped() const { return m_mapped; }

    // Upload data to CPU-visible buffer. For GPU-only buffers, use staging (TODO).
    void upload(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
    void flush();

private:
    Context* m_ctx = nullptr;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkDeviceSize m_size = 0;
    void* m_mapped = nullptr;
    VkMemoryPropertyFlags m_properties = 0;

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
};

} // namespace kazu
