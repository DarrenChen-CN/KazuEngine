// ============================================================================
// KazuEngine - Scene Layer: Scene
//
// Manages scene configuration (JSON), model loading (OBJ/GLTF), and rendering.
// ============================================================================

#pragma once

#include "../core/Context.h"
#include "../rhi/Mesh.h"
#include "../rhi/Bounds.h"
#include "../rhi/Material.h"
#include "../rhi/Texture.h"
#include "Light.h"
#include "RendererSettings.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>

namespace kazu {

class ShaderEffect;

struct SceneConfig {
    glm::vec3 cameraEye{0.0f, 1.0f, -3.0f};
    glm::vec3 cameraTarget{0.0f, 0.8f, 0.0f};
    glm::vec3 cameraUp{0.0f, 1.0f, 0.0f};
    glm::vec3 lightPos{2.0f, 3.0f, 2.0f};
    uint32_t windowWidth = 1280;
    uint32_t windowHeight = 720;
};

struct ModelInstance {
    Mesh* mesh = nullptr;          // owned by Scene's mesh pool
    Material* material = nullptr;  // owned by MaterialCache, filled by buildMaterials()
    glm::mat4 transform = glm::mat4(1.0f);

    // Material build parameters (used before buildMaterials() is called)
    Texture* pendingAlbedoMap = nullptr;
    Texture* pendingNormalMap = nullptr;
    Texture* pendingMetallicRoughnessMap = nullptr;
    Texture* pendingAoMap = nullptr;
    glm::vec4 pendingBaseColorFactor = glm::vec4(1.0f);
    float pendingMetallic = 0.0f;
    float pendingRoughness = 1.0f;
    float pendingAo = 1.0f;
    bool pendingFlipV = true;

    // If true, this instance is rendered by LightVisualizePass instead of
    // going through the full GBuffer + Lighting pipeline.
    bool unlit = false;
};

class Scene {
public:
    using InstanceDrawFn = std::function<void(VkCommandBuffer, VkPipelineLayout, const ModelInstance&)>;

    void loadFromFile(Context& ctx, const std::string& scenePath);

    // Build materials after ShaderEffect is ready (called by Pass/Technique layer)
    void buildMaterials(Context& ctx, ShaderEffect* effect,
                        DescriptorSetLayoutCache& dslCache);

    // Per-instance draw: caller records pass-specific per-instance state before mesh draw.
    void draw(VkCommandBuffer cmd, VkPipelineLayout pipelineLayout,
              const InstanceDrawFn& beforeDraw);

    // Read-only access to scene instances for passes that need custom draw logic
    // (e.g. ShadowMapPass only needs geometry, no material binding).
    const std::vector<ModelInstance>& instances() const { return m_instances; }

    const SceneConfig& config() const { return m_config; }
    const DirectionalLight& directionalLight() const { return m_directionalLight; }
    const std::vector<PointLight>& pointLights() const { return m_pointLights; }
    const std::vector<AreaLight>& areaLights() const { return m_areaLights; }
    const std::vector<Light*>& lights() const { return m_lights; }
    const RendererSettings& rendererSettings() const { return m_rendererSettings; }
    const Bounds& bounds() const { return m_bounds; }

private:
    Bounds m_bounds;
    SceneConfig m_config;
    RendererSettings m_rendererSettings;
    DirectionalLight m_directionalLight;
    std::vector<PointLight> m_pointLights;
    std::vector<AreaLight> m_areaLights;
    std::vector<Light*> m_lights;

    // Resource pools (Scene owns the lifetime)
    std::vector<std::unique_ptr<Mesh>> m_meshes;
    std::vector<std::unique_ptr<Texture>> m_textures;

    // Path-based deduplication: identical resources share the same GPU object
    std::unordered_map<std::string, Mesh*> m_meshMap;
    std::unordered_map<std::string, Texture*> m_textureMap;

    MaterialCache m_materialCache;

    // Instances (reference pools above; material filled in by buildMaterials)
    std::vector<ModelInstance> m_instances;

    void loadObjModel(Context& ctx, const std::string& path, const glm::vec3& scale,
                      const glm::vec3& position = glm::vec3(0.0f),
                      bool snapToGround = true,
                      const std::string& texturePathOverride = {},
                      const std::string& normalTexturePath = {},
                      const std::string& metallicRoughnessTexturePath = {},
                      const std::string& aoTexturePath = {},
                      const glm::vec4& baseColorFactor = glm::vec4(1.0f),
                      float metallic = 0.0f,
                      float roughness = 1.0f,
                      float ao = 1.0f,
                      bool flipV = true,
                      const glm::vec3& rotationDegrees = glm::vec3(0.0f),
                      bool centerPivot = false);
    void loadGltfModel(Context& ctx, const std::string& path, float scale,
                       const glm::vec3& position = glm::vec3(0.0f), bool snapToGround = true);
    void addGroundPlane(Context& ctx, const GroundPlaneSettings& settings);
    void addLightVisualizers(Context& ctx, float size = 0.15f);
    void addAreaLightVisualizers(Context& ctx);

    // Resource loaders with path-based deduplication
    Mesh* getOrLoadMesh(Context& ctx, const std::string& path);
    Mesh* getOrLoadMesh(Context& ctx, const std::string& key,
                        const std::vector<Vertex>& vertices,
                        const std::vector<uint32_t>& indices);
    Texture* getOrLoadTexture(Context& ctx, const std::string& path, bool srgb = true);
    void rebuildLightViews();
};

} // namespace kazu

