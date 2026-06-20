// ============================================================================
// KazuEngine - Pass Layer: Precompute Pass Base Interface
//
// Extends Pass with output metadata so PrecomputeManager can create the
// persistent resource, import it into a one-shot RenderGraph, and hand the
// result back to the application.
// ============================================================================

#pragma once

#include "pass/Pass.h"
#include "rendergraph/RenderGraph.h"
#include "core/Image.h"
#include "rhi/Texture.h"
#include <string>

namespace kazu {

class PrecomputePass : public Pass {
public:
    struct OutputDesc {
        std::string name;
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageUsageFlags usage = 0;
        VkImageCreateFlags flags = 0;
    };

    // Metadata: what persistent resource this pass produces.
    virtual OutputDesc outputDesc() const = 0;

    // Hand the imported resource handle + persistent image to the pass before declare().
    virtual void setOutputResource(RenderGraph::ResourceHandle handle, Image* image) = 0;

    // Access the produced Texture after execution (owned by PrecomputeManager).
    virtual Texture* outputTexture() const = 0;

    // Called by PrecomputeManager before executing this pass, allowing passes
    // to resolve inputs from previously produced outputs.
    virtual void resolveInputs(class PrecomputeManager* mgr) { (void)mgr; }
};

} // namespace kazu
