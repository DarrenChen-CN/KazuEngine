// ============================================================================
// KazuEngine - Core Layer: PipelineLayout
//
// RAII wrapper for VkPipelineLayout.
// ============================================================================

#pragma once

#include "Context.h"

namespace kazu {

class PipelineLayout {
public:
    PipelineLayout(Context& ctx, const VkPipelineLayoutCreateInfo& createInfo);
    ~PipelineLayout();

    PipelineLayout(const PipelineLayout&) = delete;
    PipelineLayout& operator=(const PipelineLayout&) = delete;
    PipelineLayout(PipelineLayout&& other) noexcept;
    PipelineLayout& operator=(PipelineLayout&& other) noexcept;

    VkPipelineLayout handle() const { return m_layout; }

private:
    Context* m_ctx = nullptr;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
};

} // namespace kazu
