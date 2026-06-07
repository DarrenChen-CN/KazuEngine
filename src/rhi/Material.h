// ============================================================================
// KazuEngine - RHI Layer: Material System
//
// Material is an abstract base class. Concrete subclasses (PBRMaterial,
// UnlitMaterial, etc.) define their own properties and binding logic.
//
// A Material owns its DescriptorSet (texture bindings) but does NOT own
// VkPipeline / VkPipelineLayout — those come from ShaderEffect.
//
// MaterialCache provides global deduplication: identical materials share
// the same DescriptorSet.
// ============================================================================

#pragma once

#include "ShaderEffect.h"
#include "Texture.h"
#include "DescriptorSetLayoutCache.h"
#include "../core/Context.h"
#include "../core/DescriptorPool.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <array>
#include <string>
#include <unordered_map>

namespace kazu {

// ---------------------------------------------------------------------------
// Material: abstract base
// ---------------------------------------------------------------------------

class Material {
public:
    virtual ~Material() = default;

    // Material type identifier ("PBR", "Unlit", "Custom", ...)
    virtual const char* type() const = 0;

    // The ShaderEffect that determines the Pipeline for this material
    virtual ShaderEffect* effect() const = 0;

    // Build GPU resources (DescriptorSet, etc.)
    virtual void build(Context& ctx, DescriptorSetLayoutCache& dslCache) = 0;

    // Bind material resources (DescriptorSet, PushConstants, etc.)
    // Called by the Pass before drawing a mesh with this material.
    virtual void bind(VkCommandBuffer cmd, VkPipelineLayout layout) = 0;

protected:
    // Helper: create a basic DescriptorPool for a single set
    static std::unique_ptr<DescriptorPool> createPool(Context& ctx,
        const std::vector<VkDescriptorPoolSize>& sizes);
};

// ---------------------------------------------------------------------------
// PBRMaterial
// ---------------------------------------------------------------------------

class PBRMaterial : public Material {
public:
    const char* type() const override { return "PBR"; }
    ShaderEffect* effect() const override { return m_effect; }

    // Set the ShaderEffect before calling build()
    void setEffect(ShaderEffect* effect) { m_effect = effect; }

    // Texture slots (PBR workflow)
    Texture* albedoMap = nullptr;                // binding 0
    Texture* normalMap = nullptr;                // binding 1
    Texture* metallicRoughnessMap = nullptr;     // binding 2
    Texture* aoMap = nullptr;                    // binding 3 (optional)

    // Scalar parameters (fallback when texture is absent)
    glm::vec4 baseColorFactor = glm::vec4(1.0f);
    float metallic = 0.0f;
    float roughness = 0.5f;
    float emissiveStrength = 0.0f;
    bool doubleSided = false;

    void build(Context& ctx, DescriptorSetLayoutCache& dslCache) override;
    void bind(VkCommandBuffer cmd, VkPipelineLayout layout) override;

    // Accessors for cache key computation
    const std::array<Texture*, 4>& textureSlots() const { return m_textures; }

private:
    ShaderEffect* m_effect = nullptr;
    std::array<Texture*, 4> m_textures = {};

    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    std::unique_ptr<DescriptorPool> m_pool;
};

// ---------------------------------------------------------------------------
// MaterialCache
// ---------------------------------------------------------------------------

class MaterialCache {
public:
    // Get-or-create: returns a cached Material if an identical one exists.
    // Otherwise takes ownership of the prototype and returns it.
    Material* getOrCreate(std::unique_ptr<Material> prototype);

    void clear();

private:
    // Cache key for PBRMaterial (extend for other types as needed)
    struct PBRKey {
        ShaderEffect* effect;
        std::array<Texture*, 4> textures;
        glm::vec4 baseColorFactor;
        float metallic;
        float roughness;
        float emissiveStrength;
        bool doubleSided;

        bool operator==(const PBRKey& o) const;
    };

    struct PBRKeyHash {
        size_t operator()(const PBRKey& k) const;
    };

    std::unordered_map<PBRKey, std::unique_ptr<Material>, PBRKeyHash> m_pbrCache;
};

} // namespace kazu
