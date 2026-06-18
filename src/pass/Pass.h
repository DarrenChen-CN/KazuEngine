// ============================================================================
// KazuEngine - RenderGraph Pass Base Interface
//
// Reference: 04-00-rendergraph-and-deferred-shading.md Section 2.6
// All render passes derive from this interface for uniform lifecycle management.
// ============================================================================

#pragma once
#include <vulkan/vulkan.h>

namespace kazu {

class RHI;
class Scene;
class Camera;
class RenderGraph;
struct Light;

struct PassCreateContext {
    RHI* rhi = nullptr;
    RenderGraph* renderGraph = nullptr;
    Scene* scene = nullptr;
};

struct PassExecuteContext {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    uint32_t imageIndex = 0;
    const Camera* camera = nullptr;
    const Light* light = nullptr;
};

class Pass {
public:
    virtual ~Pass() = default;

    // Human-readable name for debugging / profiling
    virtual const char* name() const = 0;

    // Phase 1: register textures and pass dependencies on the RenderGraph
    virtual void declare(RHI* rhi, RenderGraph* rg) = 0;

    // Phase 2: create VK objects after rg->compile() (ImageViews are now valid)
    virtual void create(const PassCreateContext& ctx) = 0;

    // Phase 3: record this pass into the current command buffer.
    virtual void execute(const PassExecuteContext& ctx) = 0;
};

} // namespace kazu
