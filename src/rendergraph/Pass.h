// ============================================================================
// KazuEngine - RenderGraph Pass Base Interface
//
// Reference: 04-00-rendergraph-and-deferred-shading.md Section 2.6
// All render passes derive from this interface for uniform lifecycle management.
// ============================================================================

#pragma once

namespace kazu {

class RHI;
class Scene;
class Camera;
class RenderGraph;

class Pass {
public:
    virtual ~Pass() = default;

    // Human-readable name for debugging / profiling
    virtual const char* name() const = 0;

    // Phase 1: register textures and pass dependencies on the RenderGraph
    virtual void declare(RHI* rhi, RenderGraph* rg) = 0;

    // Phase 2: create VK objects after rg->compile() (ImageViews are now valid)
    virtual void create(Scene* scene, Camera* camera, RenderGraph* rg) = 0;
};

} // namespace kazu
