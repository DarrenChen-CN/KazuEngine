// ============================================================================
// KazuEngine - Core Layer: CommandPool (Implementation)
// ============================================================================

#include "CommandPool.h"
#include "Utils.h"

namespace kazu {

CommandPool::CommandPool(Context& ctx, uint32_t queueFamilyIndex)
    : m_ctx(&ctx)
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    VK_CHECK(vkCreateCommandPool(m_ctx->device(), &poolInfo, nullptr, &m_pool));
}

CommandPool::~CommandPool() {
    if (m_pool != VK_NULL_HANDLE && m_ctx) {
        vkDestroyCommandPool(m_ctx->device(), m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }
}

CommandPool::CommandPool(CommandPool&& other) noexcept
    : m_ctx(other.m_ctx)
    , m_pool(other.m_pool)
{
    other.m_pool = VK_NULL_HANDLE;
}

CommandPool& CommandPool::operator=(CommandPool&& other) noexcept {
    if (this != &other) {
        this->~CommandPool();
        m_ctx = other.m_ctx;
        m_pool = other.m_pool;
        other.m_pool = VK_NULL_HANDLE;
    }
    return *this;
}

VkCommandBuffer CommandPool::allocate() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(m_ctx->device(), &allocInfo, &cmd));
    return cmd;
}

} // namespace kazu
