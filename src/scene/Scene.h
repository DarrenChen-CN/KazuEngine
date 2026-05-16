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
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>

namespace kazu {

struct SceneConfig {
    glm::vec3 cameraEye{0.0f, 1.0f, -3.0f};
    glm::vec3 cameraTarget{0.0f, 0.8f, 0.0f};
    glm::vec3 cameraUp{0.0f, 1.0f, 0.0f};
    glm::vec3 lightPos{2.0f, 3.0f, 2.0f};
    uint32_t windowWidth = 1280;
    uint32_t windowHeight = 720;
};

struct ModelInstance {
    std::unique_ptr<Mesh> mesh;
    std::unique_ptr<Material> material;
    std::unique_ptr<Texture> texture;
};

class Scene {
public:
    void loadFromFile(Context& ctx, ShaderLibrary& shaderLib, DescriptorSetLayoutCache& dslCache,
                      const std::string& scenePath);
    void draw(VkCommandBuffer cmd, VkPipelineLayout pipelineLayout);

    const SceneConfig& config() const { return m_config; }

private:
    SceneConfig m_config;
    std::vector<ModelInstance> m_models;

    void loadObjModel(Context& ctx, ShaderLibrary& shaderLib, DescriptorSetLayoutCache& dslCache,
                      const std::string& path, float scale);
    void loadGltfModel(Context& ctx, ShaderLibrary& shaderLib, DescriptorSetLayoutCache& dslCache,
                       const std::string& path, float scale);
};

} // namespace kazu
