// ============================================================================
// KazuEngine - RHI Layer: DescriptorSetLayoutCache
//
// Prevents duplicate VkDescriptorSetLayout creation. Two layouts with the
// same binding signature return the same handle.
// ============================================================================

#pragma once

#include "../core/Context.h"
#include "../core/DescriptorSetLayout.h"
#include <vector>
#include <memory>
#include <unordered_map>

namespace kazu {

// Hashable wrapper for a sequence of VkDescriptorSetLayoutBinding
struct LayoutSignature {
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    bool operator==(const LayoutSignature& other) const {
        if (bindings.size() != other.bindings.size()) return false;
        for (size_t i = 0; i < bindings.size(); ++i) {
            const auto& a = bindings[i];
            const auto& b = other.bindings[i];
            if (a.binding != b.binding ||
                a.descriptorType != b.descriptorType ||
                a.descriptorCount != b.descriptorCount ||
                a.stageFlags != b.stageFlags) {
                return false;
            }
        }
        return true;
    }
};

struct LayoutSignatureHash {
    size_t operator()(const LayoutSignature& sig) const {
        size_t hash = sig.bindings.size();
        for (const auto& b : sig.bindings) {
            hash ^= std::hash<uint32_t>{}(b.binding) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<uint32_t>{}(static_cast<uint32_t>(b.descriptorType)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<uint32_t>{}(b.descriptorCount) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<uint32_t>{}(b.stageFlags) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        return hash;
    }
};

class DescriptorSetLayoutCache {
public:
    explicit DescriptorSetLayoutCache(Context& ctx);
    ~DescriptorSetLayoutCache() = default;

    DescriptorSetLayoutCache(const DescriptorSetLayoutCache&) = delete;
    DescriptorSetLayoutCache& operator=(const DescriptorSetLayoutCache&) = delete;

    // Returns cached layout or creates a new one. Caller does NOT own the returned handle.
    VkDescriptorSetLayout getOrCreate(const std::vector<VkDescriptorSetLayoutBinding>& bindings);

    // For debugging
    size_t cacheSize() const { return m_cache.size(); }

private:
    Context* m_ctx = nullptr;
    std::unordered_map<LayoutSignature, std::unique_ptr<DescriptorSetLayout>, LayoutSignatureHash> m_cache;
};

} // namespace kazu
