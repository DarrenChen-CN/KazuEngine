// ============================================================================
// KazuEngine - RHI Layer: ShaderEffect
//
// Encapsulates a shader program + fixed-function pipeline state into a
// reusable, globally-cached effect. Provides Pipeline, PipelineLayout, and
// DescriptorSetLayout for all passes that use the same shader + state combo.
//
// Replaces direct PipelineBuilder usage in Pass code.
// ============================================================================

#pragma once

#include "PipelineState.h"
#include "PipelineCache.h"
#include "DescriptorSetLayoutCache.h"
#include "ShaderLibrary.h"
#include "PipelineBuilder.h"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

namespace kazu {

class Context;
class PipelineLayout;

class ShaderEffect {
public:
    // Key uniquely identifies a shader + pipeline-state combination.
    struct Key {
        std::vector<std::string> shaderPaths;  // e.g. {vert, frag}
        PipelineState state;                   // renderPass, cullMode, depthTest, etc.

        bool operator==(const Key& o) const;
    };

    struct KeyHash {
        size_t operator()(const Key& k) const;
    };

    // Global cache: get-or-create a ShaderEffect.
    // The returned pointer is stable for the lifetime of the application.
    static ShaderEffect* getOrCreate(Context& ctx,
                                     ShaderLibrary& lib,
                                     DescriptorSetLayoutCache& dslCache,
                                     PipelineCache& pipeCache,
                                     const Key& key);

    // Accessors
    VkPipeline pipeline() const;
    VkPipelineLayout pipelineLayout() const;
    VkDescriptorSetLayout descriptorSetLayout() const;
    const Key& key() const { return m_key; }

public:
    explicit ShaderEffect(Key key);

    Key m_key;
    GraphicsPipeline* m_pipeline = nullptr;            // borrowed from PipelineCache
    std::unique_ptr<PipelineLayout> m_pipelineLayout;   // owned
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;  // from DSLCache

    // Global cache storage
    static std::unordered_map<Key, std::unique_ptr<ShaderEffect>, KeyHash> s_cache;
};

} // namespace kazu
