// ============================================================================
// KazuEngine - Core Layer: GraphicsPipeline
//
// RAII wrapper for VkPipeline.
// ============================================================================

#pragma once

#include "Context.h"

namespace kazu {

class GraphicsPipeline {
public:
    GraphicsPipeline(Context& ctx, const VkGraphicsPipelineCreateInfo& createInfo);
    ~GraphicsPipeline();

    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;
    GraphicsPipeline(GraphicsPipeline&& other) noexcept;
    GraphicsPipeline& operator=(GraphicsPipeline&& other) noexcept;

    VkPipeline handle() const { return m_pipeline; }

private:
    Context* m_ctx = nullptr;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

} // namespace kazu
