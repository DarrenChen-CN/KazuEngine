// ============================================================================
// KazuEngine - RHI Layer: Material (Implementation)
// ============================================================================

#include "Material.h"
#include "../core/Utils.h"
#include <spdlog/spdlog.h>
#include <map>

namespace kazu {

Material::Material(Context& ctx, ShaderLibrary& shaderLib, DescriptorSetLayoutCache& dslCache)
    : m_ctx(&ctx), m_shaderLib(&shaderLib), m_dslCache(&dslCache) {}

Material::~Material() {
    // DescriptorSet is pool-managed; pool destruction frees it automatically.
    // DescriptorSetLayout is cache-managed.
}

Material::Material(Material&& other) noexcept
    : m_ctx(other.m_ctx), m_shaderLib(other.m_shaderLib), m_dslCache(other.m_dslCache),
      m_vertPath(std::move(other.m_vertPath)), m_fragPath(std::move(other.m_fragPath)),
      m_textures(std::move(other.m_textures)),
      m_descriptorSetLayout(other.m_descriptorSetLayout),
      m_descriptorPool(std::move(other.m_descriptorPool)),
      m_descriptorSet(other.m_descriptorSet) {
    other.m_descriptorSetLayout = VK_NULL_HANDLE;
    other.m_descriptorSet = VK_NULL_HANDLE;
}

Material& Material::operator=(Material&& other) noexcept {
    if (this != &other) {
        m_ctx = other.m_ctx;
        m_shaderLib = other.m_shaderLib;
        m_dslCache = other.m_dslCache;
        m_vertPath = std::move(other.m_vertPath);
        m_fragPath = std::move(other.m_fragPath);
        m_textures = std::move(other.m_textures);
        m_descriptorSetLayout = other.m_descriptorSetLayout;
        m_descriptorPool = std::move(other.m_descriptorPool);
        m_descriptorSet = other.m_descriptorSet;
        other.m_descriptorSetLayout = VK_NULL_HANDLE;
        other.m_descriptorSet = VK_NULL_HANDLE;
    }
    return *this;
}

void Material::setShaders(const std::string& vertPath, const std::string& fragPath) {
    m_vertPath = vertPath;
    m_fragPath = fragPath;
}

void Material::setTexture(uint32_t binding, Texture& texture) {
    for (auto& [b, tex] : m_textures) {
        if (b == binding) {
            tex = &texture;
            return;
        }
    }
    m_textures.emplace_back(binding, &texture);
}

void Material::build() {
    if (m_vertPath.empty() || m_fragPath.empty()) {
        fatalError("Material: shaders not set");
    }

    // Ensure shaders are loaded (and cached in ShaderLibrary)
    m_shaderLib->load(m_vertPath);
    m_shaderLib->load(m_fragPath);

    // Collect reflections and merge descriptor bindings
    const auto& vertRefl = m_shaderLib->getReflection(m_vertPath);
    const auto& fragRefl = m_shaderLib->getReflection(m_fragPath);

    std::map<std::pair<uint32_t, uint32_t>, ShaderDescriptorBinding> merged;
    for (const auto& refl : {vertRefl, fragRefl}) {
        for (const auto& b : refl.descriptorBindings) {
            auto key = std::make_pair(b.set, b.binding);
            auto it = merged.find(key);
            if (it == merged.end()) {
                merged[key] = b;
            } else {
                it->second.stageFlags |= b.stageFlags;
            }
        }
    }

    // Convert to VkDescriptorSetLayoutBinding
    std::vector<VkDescriptorSetLayoutBinding> vkBindings;
    vkBindings.reserve(merged.size());
    std::map<VkDescriptorType, uint32_t> typeCounts;
    for (const auto& [key, b] : merged) {
        VkDescriptorSetLayoutBinding vkBinding{};
        vkBinding.binding = b.binding;
        vkBinding.descriptorType = b.descriptorType;
        vkBinding.descriptorCount = b.count;
        vkBinding.stageFlags = b.stageFlags;
        vkBinding.pImmutableSamplers = nullptr;
        vkBindings.push_back(vkBinding);
        typeCounts[b.descriptorType] += b.count;
    }

    // Get or create DescriptorSetLayout via cache
    m_descriptorSetLayout = m_dslCache->getOrCreate(vkBindings);

    // Create DescriptorPool sized to this material's needs
    std::vector<VkDescriptorPoolSize> poolSizes;
    poolSizes.reserve(typeCounts.size());
    for (const auto& [type, count] : typeCounts) {
        poolSizes.push_back({type, count});
    }

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    m_descriptorPool = std::make_unique<DescriptorPool>(*m_ctx, poolInfo);

    // Allocate DescriptorSet
    m_descriptorSet = m_descriptorPool->allocate(m_descriptorSetLayout);

    // Update DescriptorSet with bound textures
    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(m_textures.size());
    std::vector<VkDescriptorImageInfo> imageInfos;
    imageInfos.reserve(m_textures.size());

    for (const auto& [binding, texture] : m_textures) {
        imageInfos.push_back(texture->descriptorInfo());
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_descriptorSet;
        write.dstBinding = binding;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfos.back();
        writes.push_back(write);
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(m_ctx->device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    spdlog::info("[Material] Built: shaders=[{}, {}], bindings={}, textures={}",
                 m_vertPath, m_fragPath, vkBindings.size(), m_textures.size());
}

} // namespace kazu
