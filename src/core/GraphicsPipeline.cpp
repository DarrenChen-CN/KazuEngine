// ============================================================================
// KazuEngine - Core Layer: GraphicsPipeline (Implementation)
// ============================================================================

#include "GraphicsPipeline.h"
#include "Utils.h"

namespace kazu {

GraphicsPipeline::GraphicsPipeline(Context& ctx, const VkGraphicsPipelineCreateInfo& createInfo)
    : m_ctx(&ctx)
{
    VK_CHECK(vkCreateGraphicsPipelines(m_ctx->device(), VK_NULL_HANDLE, 1, &createInfo, nullptr, &m_pipeline));
}

GraphicsPipeline::~GraphicsPipeline() {
    if (m_pipeline != VK_NULL_HANDLE && m_ctx) {
        vkDestroyPipeline(m_ctx->device(), m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
}

GraphicsPipeline::GraphicsPipeline(GraphicsPipeline&& other) noexcept
    : m_ctx(other.m_ctx)
    , m_pipeline(other.m_pipeline)
{
    other.m_pipeline = VK_NULL_HANDLE;
}

GraphicsPipeline& GraphicsPipeline::operator=(GraphicsPipeline&& other) noexcept {
    if (this != &other) {
        this->~GraphicsPipeline();
        m_ctx = other.m_ctx;
        m_pipeline = other.m_pipeline;
        other.m_pipeline = VK_NULL_HANDLE;
    }
    return *this;
}

} // namespace kazu
