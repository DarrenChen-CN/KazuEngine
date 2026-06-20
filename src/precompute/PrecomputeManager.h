// ============================================================================
// KazuEngine - Application Layer: Precompute Manager
//
// Generic manager for one-shot GPU precompute passes. Each pass declares the
// persistent resource it wants to produce; the manager creates the Texture,
// imports it into a one-shot RenderGraph, runs the pass, and stores the result.
// ============================================================================

#pragma once

#include "precompute/PrecomputePass.h"
#include "rhi/Texture.h"
#include <memory>
#include <vector>
#include <unordered_map>

namespace kazu {

class RHI;
class Scene;

class PrecomputeManager {
public:
    PrecomputeManager();
    ~PrecomputeManager();

    PrecomputeManager(const PrecomputeManager&) = delete;
    PrecomputeManager& operator=(const PrecomputeManager&) = delete;

    void init(RHI* rhi, Scene* scene);

    // Register a precompute pass. Ownership transfers to the manager.
    void registerPass(std::unique_ptr<PrecomputePass> pass);

    // Execute all registered passes in declaration order.
    // Must be called after init() and before the main render technique is initialized.
    void run();

    // Access a precomputed resource by name (as declared by the pass).
    Texture* getTexture(const std::string& name) const;

private:
    struct PassSlot {
        std::unique_ptr<PrecomputePass> pass;
        std::unique_ptr<Texture> output;
    };

    void executePass(PassSlot& slot);

    RHI* m_rhi = nullptr;
    Scene* m_scene = nullptr;
    bool m_initialized = false;

    std::vector<PassSlot> m_passes;
    std::unordered_map<std::string, Texture*> m_outputMap;
};

} // namespace kazu
