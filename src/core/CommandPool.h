// ============================================================================
// KazuEngine - Core Layer: CommandPool
//
// RAII wrapper for VkCommandPool.
// ============================================================================

#pragma once

#include "Context.h"

namespace kazu {

class CommandPool {
public:
    CommandPool(Context& ctx, uint32_t queueFamilyIndex);
    ~CommandPool();

    CommandPool(const CommandPool&) = delete;
    CommandPool& operator=(const CommandPool&) = delete;
    CommandPool(CommandPool&& other) noexcept;
    CommandPool& operator=(CommandPool&& other) noexcept;

    VkCommandPool handle() const { return m_pool; }
    VkCommandBuffer allocate();

private:
    Context* m_ctx = nullptr;
    VkCommandPool m_pool = VK_NULL_HANDLE;
};

} // namespace kazu
