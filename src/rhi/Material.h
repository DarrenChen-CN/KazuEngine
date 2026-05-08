// ============================================================================
// KazuEngine - RHI Layer: Material
//
// Binds shaders + textures into a renderable unit.
// Manages DescriptorSetLayout (via cache), DescriptorPool, and DescriptorSet.
// ============================================================================

#pragma once

#include "../core/Context.h"
#include "../core/DescriptorPool.h"
#include "ShaderLibrary.h"
#include "DescriptorSetLayoutCache.h"
#include "Texture.h"
#include <vector>
#include <string>
#include <memory>

namespace kazu {

class Material {
public:
    Material(Context& ctx, ShaderLibrary& shaderLib, DescriptorSetLayoutCache& dslCache);
    ~Material();

    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;
    Material(Material&&) noexcept;
    Material& operator=(Material&&) noexcept;

    void setShaders(const std::string& vertPath, const std::string& fragPath);
    void setTexture(uint32_t binding, Texture& texture);

    // Creates DSL (via cache), allocates pool & set, updates set with textures
    void build();

    VkDescriptorSet descriptorSet() const { return m_descriptorSet; }
    VkDescriptorSetLayout descriptorSetLayout() const { return m_descriptorSetLayout; }

private:
    Context* m_ctx = nullptr;
    ShaderLibrary* m_shaderLib = nullptr;
    DescriptorSetLayoutCache* m_dslCache = nullptr;

    std::string m_vertPath;
    std::string m_fragPath;
    std::vector<std::pair<uint32_t, Texture*>> m_textures;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    std::unique_ptr<DescriptorPool> m_descriptorPool;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
};

} // namespace kazu
