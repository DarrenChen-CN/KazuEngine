// ============================================================================
// KazuEngine - Core Layer: Sampler (Implementation)
// ============================================================================

#include "Sampler.h"
#include "Utils.h"

namespace kazu {

Sampler::Sampler(Context& ctx, const VkSamplerCreateInfo& createInfo)
    : m_ctx(&ctx)
{
    VK_CHECK(vkCreateSampler(m_ctx->device(), &createInfo, nullptr, &m_sampler));
}

Sampler::~Sampler() {
    if (m_sampler != VK_NULL_HANDLE && m_ctx) {
        vkDestroySampler(m_ctx->device(), m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
}

Sampler::Sampler(Sampler&& other) noexcept
    : m_ctx(other.m_ctx)
    , m_sampler(other.m_sampler)
{
    other.m_sampler = VK_NULL_HANDLE;
}

Sampler& Sampler::operator=(Sampler&& other) noexcept {
    if (this != &other) {
        this->~Sampler();
        m_ctx = other.m_ctx;
        m_sampler = other.m_sampler;
        other.m_sampler = VK_NULL_HANDLE;
    }
    return *this;
}

} // namespace kazu
