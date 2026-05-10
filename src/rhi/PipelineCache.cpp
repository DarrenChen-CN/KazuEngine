// ============================================================================
// KazuEngine - RHI Layer: PipelineCache (Implementation)
// ============================================================================

#include "PipelineCache.h"
#include <spdlog/spdlog.h>

namespace kazu {

PipelineCache::PipelineCache(Context& ctx) : m_ctx(&ctx) {}

GraphicsPipeline* PipelineCache::find(const PipelineState& state) const {
    auto it = m_cache.find(state);
    if (it != m_cache.end()) {
        spdlog::debug("[PipelineCache] Hit ({} shader(s))", state.shaderPaths.size());
        return it->second.get();
    }
    return nullptr;
}

void PipelineCache::insert(const PipelineState& state, std::unique_ptr<GraphicsPipeline> pipeline) {
    spdlog::info("[PipelineCache] Miss -> create new pipeline (cache size: {} -> {})",
                 m_cache.size(), m_cache.size() + 1);
    m_cache[state] = std::move(pipeline);
}

void PipelineCache::clear() {
    m_cache.clear();
}

} // namespace kazu
