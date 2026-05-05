// ============================================================================
// KazuEngine - Core Layer: Buffer (Implementation)
// ============================================================================

#include "Buffer.h"
#include "Utils.h"
#include <cstring>

namespace kazu {

Buffer::Buffer(Context& ctx, VkDeviceSize size, VkBufferUsageFlags usage,
               VkMemoryPropertyFlags properties)
    : m_ctx(&ctx)
    , m_size(size)
    , m_properties(properties)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.requiredFlags = properties;

    VK_CHECK(vmaCreateBuffer(m_ctx->allocator(), &bufferInfo, &allocInfo, &m_buffer, &m_allocation, nullptr));

    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        VK_CHECK(vmaMapMemory(m_ctx->allocator(), m_allocation, &m_mapped));
    }
}

Buffer::~Buffer() {
    if (!m_ctx) return;

    if (m_mapped) {
        vmaUnmapMemory(m_ctx->allocator(), m_allocation);
        m_mapped = nullptr;
    }

    if (m_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_ctx->allocator(), m_buffer, m_allocation);
        m_buffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }
}

Buffer::Buffer(Buffer&& other) noexcept
    : m_ctx(other.m_ctx)
    , m_buffer(other.m_buffer)
    , m_allocation(other.m_allocation)
    , m_size(other.m_size)
    , m_mapped(other.m_mapped)
    , m_properties(other.m_properties)
{
    other.m_buffer = VK_NULL_HANDLE;
    other.m_allocation = VK_NULL_HANDLE;
    other.m_mapped = nullptr;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        this->~Buffer();

        m_ctx = other.m_ctx;
        m_buffer = other.m_buffer;
        m_allocation = other.m_allocation;
        m_size = other.m_size;
        m_mapped = other.m_mapped;
        m_properties = other.m_properties;

        other.m_buffer = VK_NULL_HANDLE;
        other.m_allocation = VK_NULL_HANDLE;
        other.m_mapped = nullptr;
    }
    return *this;
}

void Buffer::upload(const void* data, VkDeviceSize size, VkDeviceSize offset) {
    if (!m_mapped) {
        // TODO: Support GPU-only buffers via staging buffer
        fatalError("Buffer::upload called on non-host-visible buffer. Staging not yet implemented.");
    }
    std::memcpy(static_cast<char*>(m_mapped) + offset, data, static_cast<size_t>(size));
}

void Buffer::flush() {
    if (!m_mapped) return;
    vmaFlushAllocation(m_ctx->allocator(), m_allocation, 0, VK_WHOLE_SIZE);
}

} // namespace kazu
