// ============================================================================
// KazuEngine - RHI Layer: Material System (Implementation)
// ============================================================================

#include "Material.h"
#include "../core/Utils.h"
#include <spdlog/spdlog.h>

namespace kazu {

// ============================================================================
// Material base helpers
// ============================================================================

std::unique_ptr<DescriptorPool> Material::createPool(Context& ctx,
    const std::vector<VkDescriptorPoolSize>& sizes) {
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
    poolInfo.pPoolSizes = sizes.data();
    return std::make_unique<DescriptorPool>(ctx, poolInfo);
}

// ============================================================================
// PBRMaterial
// ============================================================================

void PBRMaterial::build(Context& ctx, DescriptorSetLayoutCache& dslCache) {
    if (!m_effect) {
        fatalError("PBRMaterial::build: effect not set");
    }

    m_textures = {albedoMap, normalMap, metallicRoughnessMap, aoMap};

    std::vector<VkDescriptorPoolSize> poolSizes;
    std::array<VkDescriptorImageInfo, 4> imageInfos{};
    std::vector<VkWriteDescriptorSet> writes;

    for (uint32_t i = 0; i < 4; ++i) {
        if (m_textures[i]) {
            imageInfos[i] = m_textures[i]->descriptorInfo();
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstBinding = i;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &imageInfos[i];
            writes.push_back(write);
        }
    }

    if (!writes.empty()) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = static_cast<uint32_t>(writes.size());
        poolSizes.push_back(poolSize);

        m_pool = createPool(ctx, poolSizes);

        VkDescriptorSetLayout dsl = m_effect->descriptorSetLayout();
        m_descriptorSet = m_pool->allocate(dsl);

        for (auto& write : writes) {
            write.dstSet = m_descriptorSet;
        }
        vkUpdateDescriptorSets(ctx.device(), static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }

    spdlog::info("[PBRMaterial] Built: effect={}, textures={}",
                 m_effect ? "ok" : "null", writes.size());
}

void PBRMaterial::bind(VkCommandBuffer cmd, VkPipelineLayout layout) {
    if (m_descriptorSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                layout, 0, 1, &m_descriptorSet, 0, nullptr);
    } else {
        spdlog::warn("[PBRMaterial::bind] descriptorSet is NULL!");
    }
}

// ============================================================================
// MaterialCache
// ============================================================================

bool MaterialCache::PBRKey::operator==(const PBRKey& o) const {
    return effect == o.effect
        && textures == o.textures
        && baseColorFactor == o.baseColorFactor
        && metallic == o.metallic
        && roughness == o.roughness
        && emissiveStrength == o.emissiveStrength
        && doubleSided == o.doubleSided;
}

size_t MaterialCache::PBRKeyHash::operator()(const PBRKey& k) const {
    size_t h = reinterpret_cast<size_t>(k.effect);
    for (auto* tex : k.textures) {
        h ^= reinterpret_cast<size_t>(tex) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    h ^= std::hash<float>{}(k.baseColorFactor.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<float>{}(k.baseColorFactor.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<float>{}(k.baseColorFactor.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<float>{}(k.baseColorFactor.w) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<float>{}(k.metallic) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<float>{}(k.roughness) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<float>{}(k.emissiveStrength) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<bool>{}(k.doubleSided) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

Material* MaterialCache::getOrCreate(std::unique_ptr<Material> prototype) {
    if (!prototype) return nullptr;

    auto* pbr = dynamic_cast<PBRMaterial*>(prototype.get());
    if (!pbr) {
        // Non-PBR materials: no dedup for now
        return prototype.release();
    }

    PBRKey key;
    key.effect = pbr->effect();
    key.textures = pbr->textureSlots();
    key.baseColorFactor = pbr->baseColorFactor;
    key.metallic = pbr->metallic;
    key.roughness = pbr->roughness;
    key.emissiveStrength = pbr->emissiveStrength;
    key.doubleSided = pbr->doubleSided;

    auto it = m_pbrCache.find(key);
    if (it != m_pbrCache.end()) {
        return it->second.get();
    }

    Material* raw = prototype.get();
    m_pbrCache.emplace(key, std::move(prototype));
    return raw;
}

void MaterialCache::clear() {
    m_pbrCache.clear();
}

} // namespace kazu
