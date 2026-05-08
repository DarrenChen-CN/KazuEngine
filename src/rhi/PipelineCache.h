// ============================================================================
// KazuEngine - RHI Layer: PipelineCache
//
// Prevents duplicate VkPipeline creation. Two pipelines with the same
// PipelineState return the same GraphicsPipeline pointer.
//
// Usage (by PipelineBuilder):
//   auto* cached = cache.find(state);
//   if (cached) return cached;
//   auto pipeline = createGraphicsPipeline(state);
//   cache.insert(state, std::move(pipeline));
// ============================================================================

#pragma once

#include "PipelineState.h"
#include "../core/Context.h"
#include "../core/GraphicsPipeline.h"
#include <unordered_map>
#include <memory>

namespace kazu {

class PipelineCache {
public:
    explicit PipelineCache(Context& ctx);
    ~PipelineCache() = default;

    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;

    // Lookup. Returns nullptr if not found.
    GraphicsPipeline* find(const PipelineState& state) const;

    // Insert a newly created pipeline. Cache takes ownership.
    void insert(const PipelineState& state, std::unique_ptr<GraphicsPipeline> pipeline);

    void clear();
    size_t size() const { return m_cache.size(); }

private:
    Context* m_ctx = nullptr;
    std::unordered_map<PipelineState, std::unique_ptr<GraphicsPipeline>, PipelineStateHash> m_cache;
};

} // namespace kazu
