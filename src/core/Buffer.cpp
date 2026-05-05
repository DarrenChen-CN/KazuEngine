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

    VK_CHECK(vkCreateBuffer(m_ctx->device(), &bufferInfo, nullptr, &m_buffer));

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_ctx->device(), m_buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    VK_CHECK(vkAllocateMemory(m_ctx->device(), &allocInfo, nullptr, &m_memory));
    VK_CHECK(vkBindBufferMemory(m_ctx->device(), m_buffer, m_memory, 0));

    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        VK_CHECK(vkMapMemory(m_ctx->device(), m_memory, 0, size, 0, &m_mapped));
    }
}

Buffer::~Buffer() {
    if (!m_ctx) return;

    if (m_mapped) {
        vkUnmapMemory(m_ctx->device(), m_memory);
        m_mapped = nullptr;
    }

    if (m_memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_ctx->device(), m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }

    if (m_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_ctx->device(), m_buffer, nullptr);
        m_buffer = VK_NULL_HANDLE;
    }
}

Buffer::Buffer(Buffer&& other) noexcept
    : m_ctx(other.m_ctx)
    , m_buffer(other.m_buffer)
    , m_memory(other.m_memory)
    , m_size(other.m_size)
    , m_mapped(other.m_mapped)
    , m_properties(other.m_properties)
{
    other.m_buffer = VK_NULL_HANDLE;
    other.m_memory = VK_NULL_HANDLE;
    other.m_mapped = nullptr;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        this->~Buffer();

        m_ctx = other.m_ctx;
        m_buffer = other.m_buffer;
        m_memory = other.m_memory;
        m_size = other.m_size;
        m_mapped = other.m_mapped;
        m_properties = other.m_properties;

        other.m_buffer = VK_NULL_HANDLE;
        other.m_memory = VK_NULL_HANDLE;
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

    VkMappedMemoryRange mappedRange{};
    mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    mappedRange.memory = m_memory;
    mappedRange.offset = 0;
    mappedRange.size = m_size;

    VK_CHECK(vkFlushMappedMemoryRanges(m_ctx->device(), 1, &mappedRange));
}

uint32_t Buffer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_ctx->physicalDevice(), &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    fatalError("Failed to find suitable memory type!");
}

} // namespace kazu
