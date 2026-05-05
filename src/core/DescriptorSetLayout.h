// ============================================================================
// KazuEngine - Core Layer: DescriptorSetLayout
//
// RAII wrapper for VkDescriptorSetLayout.
// ============================================================================

#pragma once

#include "Context.h"

namespace kazu {

class DescriptorSetLayout {
public:
    DescriptorSetLayout(Context& ctx, const VkDescriptorSetLayoutCreateInfo& createInfo);
    ~DescriptorSetLayout();

    DescriptorSetLayout(const DescriptorSetLayout&) = delete;
    DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;
    DescriptorSetLayout(DescriptorSetLayout&& other) noexcept;
    DescriptorSetLayout& operator=(DescriptorSetLayout&& other) noexcept;

    VkDescriptorSetLayout handle() const { return m_layout; }

private:
    Context* m_ctx = nullptr;
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
};

} // namespace kazu
