// ============================================================================
// KazuEngine - RHI Layer: DescriptorSetLayoutCache (Implementation)
// ============================================================================

#include "DescriptorSetLayoutCache.h"
#include <spdlog/spdlog.h>

namespace kazu {

DescriptorSetLayoutCache::DescriptorSetLayoutCache(Context& ctx) : m_ctx(&ctx) {}

VkDescriptorSetLayout DescriptorSetLayoutCache::getOrCreate(
    const std::vector<VkDescriptorSetLayoutBinding>& bindings) {

    LayoutSignature sig{bindings};
    auto it = m_cache.find(sig);
    if (it != m_cache.end()) {
        spdlog::debug("[DescriptorSetLayoutCache] Cache hit ({} bindings)", bindings.size());
        return it->second->handle();
    }

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<uint32_t>(bindings.size());
    info.pBindings = bindings.data();

    auto layout = std::make_unique<DescriptorSetLayout>(*m_ctx, info);
    VkDescriptorSetLayout handle = layout->handle();
    m_cache[sig] = std::move(layout);

    spdlog::info("[DescriptorSetLayoutCache] Created new layout ({} bindings), cache size = {}",
                 bindings.size(), m_cache.size());
    return handle;
}

} // namespace kazu
