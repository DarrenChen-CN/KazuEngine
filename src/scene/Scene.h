// ============================================================================
// KazuEngine - Scene Layer: Scene
//
// Manages scene configuration (JSON), model loading (OBJ/GLTF), and rendering.
// ============================================================================

#pragma once

#include "../core/Context.h"
#include "../rhi/Mesh.h"
#include "../rhi/Material.h"
#include "../rhi/Texture.h"
#include "Light.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

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

// Shared push constant layout between GBufferPass and Scene::draw
struct GBufferPush {
    glm::mat4 mvp;
    glm::vec4 lightPos;
    glm::vec4 viewPos;
    int displayMode;
    int _pad[3];
};

struct ModelInstance {
    Mesh* mesh = nullptr;          // owned by Scene's mesh pool
    Material* material = nullptr;  // owned by MaterialCache, filled by buildMaterials()
    glm::mat4 transform = glm::mat4(1.0f);

    // Material build parameters (used before buildMaterials() is called)
    Texture* pendingAlbedoMap = nullptr;
    glm::vec4 pendingBaseColorFactor = glm::vec4(1.0f);
    float pendingMetallic = 0.0f;
    float pendingRoughness = 1.0f;
};

class Scene {
public:
    void loadFromFile(Context& ctx, const std::string& scenePath);

    // Build materials after ShaderEffect is ready (called by Pass/Technique layer)
    void buildMaterials(Context& ctx, ShaderEffect* effect,
                        DescriptorSetLayoutCache& dslCache);

    // Per-instance draw: updates push constants (mvp = viewProj * transform) for each instance
    void draw(VkCommandBuffer cmd, VkPipelineLayout pipelineLayout,
              const glm::mat4& viewProj, const glm::vec4& lightPos,
              const glm::vec4& viewPos, int displayMode);

    const SceneConfig& config() const { return m_config; }
    const DirectionalLight& directionalLight() const { return m_directionalLight; }

private:
    SceneConfig m_config;
    DirectionalLight m_directionalLight;

    // Resource pools (Scene owns the lifetime)
    std::vector<std::unique_ptr<Mesh>> m_meshes;
    std::vector<std::unique_ptr<Texture>> m_textures;

    // Path-based deduplication: identical resources share the same GPU object
    std::unordered_map<std::string, Mesh*> m_meshMap;
    std::unordered_map<std::string, Texture*> m_textureMap;

    MaterialCache m_materialCache;

    // Instances (reference pools above; material filled in by buildMaterials)
    std::vector<ModelInstance> m_instances;

    void loadObjModel(Context& ctx, const std::string& path, float scale);
    void loadGltfModel(Context& ctx, const std::string& path, float scale);

    // Resource loaders with path-based deduplication
    Mesh* getOrLoadMesh(Context& ctx, const std::string& path);
    Mesh* getOrLoadMesh(Context& ctx, const std::string& key,
                        const std::vector<Vertex>& vertices,
                        const std::vector<uint32_t>& indices);
    Texture* getOrLoadTexture(Context& ctx, const std::string& path);
};

} // namespace kazu
