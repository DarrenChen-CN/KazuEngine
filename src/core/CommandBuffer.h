// ============================================================================
// KazuEngine - Core Layer: CommandBuffer
//
// RAII wrapper for a single VkCommandBuffer allocated from a CommandPool.
// ============================================================================

#pragma once

#include "Context.h"

namespace kazu {

class CommandBuffer {
public:
    CommandBuffer(Context& ctx, VkCommandPool pool);
    ~CommandBuffer();

    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;
    CommandBuffer(CommandBuffer&& other) noexcept;
    CommandBuffer& operator=(CommandBuffer&& other) noexcept;

    void begin(VkCommandBufferUsageFlags flags = 0);
    void end();
    void submit(VkQueue queue, VkSemaphore waitSemaphore = VK_NULL_HANDLE,
                VkSemaphore signalSemaphore = VK_NULL_HANDLE,
                VkFence fence = VK_NULL_HANDLE);
    void reset();

    VkCommandBuffer handle() const { return m_cmd; }

private:
    Context* m_ctx = nullptr;
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkCommandBuffer m_cmd = VK_NULL_HANDLE;
};

} // namespace kazu
