// ============================================================================
// KazuEngine - Core Layer: CommandBuffer (Implementation)
// ============================================================================

#include "CommandBuffer.h"
#include "Utils.h"

namespace kazu {

CommandBuffer::CommandBuffer(Context& ctx, VkCommandPool pool)
    : m_ctx(&ctx)
    , m_pool(pool)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(m_ctx->device(), &allocInfo, &m_cmd));
}

CommandBuffer::~CommandBuffer() {
    if (m_cmd != VK_NULL_HANDLE && m_ctx) {
        vkFreeCommandBuffers(m_ctx->device(), m_pool, 1, &m_cmd);
        m_cmd = VK_NULL_HANDLE;
    }
}

CommandBuffer::CommandBuffer(CommandBuffer&& other) noexcept
    : m_ctx(other.m_ctx)
    , m_pool(other.m_pool)
    , m_cmd(other.m_cmd)
{
    other.m_cmd = VK_NULL_HANDLE;
}

CommandBuffer& CommandBuffer::operator=(CommandBuffer&& other) noexcept {
    if (this != &other) {
        this->~CommandBuffer();
        m_ctx = other.m_ctx;
        m_pool = other.m_pool;
        m_cmd = other.m_cmd;
        other.m_cmd = VK_NULL_HANDLE;
    }
    return *this;
}

void CommandBuffer::begin(VkCommandBufferUsageFlags flags) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = flags;
    VK_CHECK(vkBeginCommandBuffer(m_cmd, &beginInfo));
}

void CommandBuffer::end() {
    VK_CHECK(vkEndCommandBuffer(m_cmd));
}

void CommandBuffer::submit(VkQueue queue, VkSemaphore waitSemaphore,
                           VkSemaphore signalSemaphore, VkFence fence) {
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    if (waitSemaphore != VK_NULL_HANDLE) {
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &waitSemaphore;
        submitInfo.pWaitDstStageMask = &waitStage;
    }
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_cmd;
    if (signalSemaphore != VK_NULL_HANDLE) {
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &signalSemaphore;
    }
    VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, fence));
}

void CommandBuffer::reset() {
    vkResetCommandBuffer(m_cmd, 0);
}

} // namespace kazu
