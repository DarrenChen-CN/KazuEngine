// ============================================================================
// KazuEngine - RHI Layer: Texture
//
// Combines Image + Sampler into a single usable texture resource.
// Loads from file (PNG/JPG/etc via stb_image) and performs staging upload.
// ============================================================================

#pragma once

#include "../core/Context.h"
#include "../core/Image.h"
#include "../core/Sampler.h"
#include <vulkan/vulkan.h>
#include <string>
#include <memory>

namespace kazu {

class Texture {
public:
    // Load from file (PNG/JPG/etc). Uploads to GPU via temporary command buffer.
    Texture(Context& ctx, const std::string& path);
    ~Texture() = default;

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&) = default;
    Texture& operator=(Texture&&) = default;

    VkImageView view() const { return m_image->view(); }
    VkSampler sampler() const { return m_sampler->handle(); }
    VkDescriptorImageInfo descriptorInfo() const;

private:
    Context* m_ctx = nullptr;
    std::unique_ptr<Image> m_image;
    std::unique_ptr<Sampler> m_sampler;
};

} // namespace kazu
