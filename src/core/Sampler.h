// ============================================================================
// KazuEngine - Core Layer: Sampler
//
// RAII wrapper for VkSampler.
// ============================================================================

#pragma once

#include "Context.h"

namespace kazu {

class Sampler {
public:
    Sampler(Context& ctx, const VkSamplerCreateInfo& createInfo);
    ~Sampler();

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;
    Sampler(Sampler&& other) noexcept;
    Sampler& operator=(Sampler&& other) noexcept;

    VkSampler handle() const { return m_sampler; }

private:
    Context* m_ctx = nullptr;
    VkSampler m_sampler = VK_NULL_HANDLE;
};

} // namespace kazu
