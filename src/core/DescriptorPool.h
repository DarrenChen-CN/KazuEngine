// ============================================================================
// KazuEngine - Core Layer: DescriptorPool
//
// RAII wrapper for VkDescriptorPool.
// ============================================================================

#pragma once

#include "Context.h"

namespace kazu {

class DescriptorPool {
public:
    DescriptorPool(Context& ctx, const VkDescriptorPoolCreateInfo& createInfo);
    ~DescriptorPool();

    DescriptorPool(const DescriptorPool&) = delete;
    DescriptorPool& operator=(const DescriptorPool&) = delete;
    DescriptorPool(DescriptorPool&& other) noexcept;
    DescriptorPool& operator=(DescriptorPool&& other) noexcept;

    VkDescriptorPool handle() const { return m_pool; }
    VkDescriptorSet allocate(VkDescriptorSetLayout layout);
    void free(VkDescriptorSet set);

private:
    Context* m_ctx = nullptr;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
};

} // namespace kazu
