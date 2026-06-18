// ============================================================================
// KazuEngine - Scene Layer: Scene (Implementation)
// ============================================================================

#include "Scene.h"
#include "../core/Path.h"
#include "../core/Utils.h"
#include "../rhi/ShaderEffect.h"
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fstream>
#include <filesystem>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace kazu {

void Scene::loadFromFile(Context& ctx, const std::string& scenePath) {
    std::ifstream file(scenePath);
    if (!file.is_open()) {
        fatalError("Failed to open scene file: " + scenePath);
    }

    json j;
    file >> j;

    // Parse camera
    auto cam = j.value("camera", json::object());
    auto eye = cam.value("eye", std::vector<float>{0.0f, 1.0f, -3.0f});
    auto target = cam.value("target", std::vector<float>{0.0f, 0.8f, 0.0f});
    auto up = cam.value("up", std::vector<float>{0.0f, 1.0f, 0.0f});
    m_config.cameraEye = glm::vec3(eye[0], eye[1], eye[2]);
    m_config.cameraTarget = glm::vec3(target[0], target[1], target[2]);
    m_config.cameraUp = glm::vec3(up[0], up[1], up[2]);

    // Parse light
    auto light = j.value("light", json::object());
    auto lpos = light.value("position", std::vector<float>{2.0f, 3.0f, 2.0f});
    m_config.lightPos = glm::vec3(lpos[0], lpos[1], lpos[2]);
    auto ldir = light.value("direction", std::vector<float>{-lpos[0], -lpos[1], -lpos[2]});
    auto lcolor = light.value("color", std::vector<float>{1.0f, 1.0f, 1.0f});
    glm::vec3 direction(ldir[0], ldir[1], ldir[2]);
    if (glm::length(direction) < 0.0001f) {
        direction = glm::vec3(-1.0f, -1.0f, -1.0f);
    }
    m_directionalLight.direction = glm::normalize(direction);
    m_directionalLight.color = glm::vec3(lcolor[0], lcolor[1], lcolor[2]);
    m_directionalLight.intensity = light.value("intensity", 1.0f);

    m_pointLights.clear();
    auto parsePointLight = [&](const json& node) {
        PointLight point{};
        auto pos = node.value("position", std::vector<float>{2.0f, 3.0f, 2.0f});
        auto color = node.value("color", std::vector<float>{1.0f, 1.0f, 1.0f});
        point.position = glm::vec3(pos[0], pos[1], pos[2]);
        point.color = glm::vec3(color[0], color[1], color[2]);
        point.intensity = node.value("intensity", 1.0f);
        point.range = node.value("range", 10.0f);
        m_pointLights.push_back(point);
    };

    if (light.value("type", std::string{}) == "point") {
        parsePointLight(light);
    }
    if (j.contains("pointLights") && j["pointLights"].is_array()) {
        for (const auto& point : j["pointLights"]) {
            parsePointLight(point);
        }
    }
    if (j.contains("lights") && j["lights"].is_array()) {
        for (const auto& item : j["lights"]) {
            if (item.value("type", std::string{}) == "point") {
                parsePointLight(item);
            }
        }
    }
    rebuildLightViews();

    // Parse window
    auto window = j.value("window", json::object());
    m_config.windowWidth = window.value("width", 1280);
    m_config.windowHeight = window.value("height", 720);

    spdlog::info("[Scene] Camera Eye: ({:.1f}, {:.1f}, {:.1f})", m_config.cameraEye.x, m_config.cameraEye.y, m_config.cameraEye.z);
    spdlog::info("[Scene] Light Position: ({:.1f}, {:.1f}, {:.1f})", m_config.lightPos.x, m_config.lightPos.y, m_config.lightPos.z);
    spdlog::info("[Scene] Directional Light: dir=({:.2f}, {:.2f}, {:.2f}), color=({:.2f}, {:.2f}, {:.2f}), intensity={:.2f}",
                 m_directionalLight.direction.x, m_directionalLight.direction.y, m_directionalLight.direction.z,
                 m_directionalLight.color.x, m_directionalLight.color.y, m_directionalLight.color.z,
                 m_directionalLight.intensity);
    spdlog::info("[Scene] Point Lights: {}", m_pointLights.size());
    spdlog::info("[Scene] Loading models...");

    // Parse models
    for (const auto& model : j.value("models", json::array())) {
        std::string path = model.value("path", "");
        std::string format = model.value("format", "obj");
        float scale = model.value("scale", 1.0f);
        auto pos = model.value("position", std::vector<float>{0.0f, 0.0f, 0.0f});
        glm::vec3 position(pos[0], pos.size() > 1 ? pos[1] : 0.0f, pos.size() > 2 ? pos[2] : 0.0f);
        bool snapToGround = model.value("snapToGround", true);

        if (path.empty()) continue;

        if (format == "obj") {
            loadObjModel(ctx, path, scale, position, snapToGround);
        } else if (format == "gltf" || format == "glb") {
            loadGltfModel(ctx, path, scale, position, snapToGround);
        } else {
            spdlog::warn("[Scene] Unknown model format: {}", format);
        }
    }

    spdlog::info("[Scene] Loaded {} model(s) from {}", m_instances.size(), scenePath);

    // Add a ground plane for shadow visualization.
    addGroundPlane(ctx);

    // Compute world-space bounds from all model instances.
    m_bounds = Bounds{};
    for (const auto& inst : m_instances) {
        if (!inst.mesh) continue;
        const auto& localBounds = inst.mesh->bounds();
        if (!localBounds.isValid()) continue;

        // Transform all 8 corners of the local AABB and expand the world bounds.
        for (int i = 0; i < 8; ++i) {
            glm::vec3 localCorner(
                (i & 1) ? localBounds.max.x : localBounds.min.x,
                (i & 2) ? localBounds.max.y : localBounds.min.y,
                (i & 4) ? localBounds.max.z : localBounds.min.z);
            glm::vec3 worldCorner = glm::vec3(inst.transform * glm::vec4(localCorner, 1.0f));
            m_bounds.expand(worldCorner);
        }
    }

    if (m_bounds.isValid()) {
        auto c = m_bounds.center();
        auto e = m_bounds.extent();
        spdlog::info("[Scene] World bounds center=({:.2f}, {:.2f}, {:.2f}) extent=({:.2f}, {:.2f}, {:.2f})",
                     c.x, c.y, c.z, e.x, e.y, e.z);
    }
}

void Scene::buildMaterials(Context& ctx, ShaderEffect* effect,
                           DescriptorSetLayoutCache& dslCache) {
    for (auto& inst : m_instances) {
        if (inst.material != nullptr) continue;  // already built

        auto mat = std::make_unique<PBRMaterial>();
        mat->setEffect(effect);
        mat->albedoMap = inst.pendingAlbedoMap;
        mat->baseColorFactor = inst.pendingBaseColorFactor;
        mat->metallic = inst.pendingMetallic;
        mat->roughness = inst.pendingRoughness;
        mat->build(ctx, dslCache);

        Material* sharedMat = m_materialCache.getOrCreate(std::move(mat));
        inst.material = sharedMat;
    }

    spdlog::info("[Scene] Built {} material(s)", m_instances.size());
}

void Scene::draw(VkCommandBuffer cmd, VkPipelineLayout pipelineLayout,
                    const InstanceDrawFn& beforeDraw) {
    for (auto& inst : m_instances) {
        if (beforeDraw) {
            beforeDraw(cmd, pipelineLayout, inst);
        }

        if (inst.material) {
            inst.material->bind(cmd, pipelineLayout);
        }
        if (inst.mesh) {
            inst.mesh->draw(cmd);
        }
    }
}

void Scene::loadObjModel(Context& ctx, const std::string& path, float scale,
                         const glm::vec3& position, bool snapToGround) {
    // Try to load texture from model directory
    std::filesystem::path modelPath(path);
    std::filesystem::path dir = modelPath.parent_path();
    std::string texturePath;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        auto ext = entry.path().extension().string();
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
            texturePath = entry.path().string();
            break;
        }
    }

    Mesh* meshPtr = getOrLoadMesh(ctx, path);
    Texture* texturePtr = getOrLoadTexture(ctx, texturePath);

    ModelInstance inst;
    inst.mesh = meshPtr;

    // Ground plane is at y = -0.1. Snap the model so its lowest point touches it.
    glm::vec3 finalPosition = position;
    if (snapToGround && meshPtr->bounds().isValid()) {
        float minY = meshPtr->bounds().min.y;
        finalPosition.y = -0.1f - minY * scale;
    }

    inst.transform = glm::translate(glm::mat4(1.0f), finalPosition)
                   * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
    inst.pendingAlbedoMap = texturePtr;
    inst.pendingBaseColorFactor = glm::vec4(1.0f);
    inst.pendingMetallic = 0.0f;
    inst.pendingRoughness = 1.0f;
    m_instances.push_back(inst);
}

void Scene::loadGltfModel(Context& ctx, const std::string& path, float scale,
                          const glm::vec3& position, bool snapToGround) {
    fastgltf::Parser parser;

    auto gltfFile = fastgltf::MappedGltfFile::FromPath(path);
    if (!bool(gltfFile)) {
        fatalError("Failed to open GLTF file: " + path);
    }

    auto extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c){ return std::tolower(c); });

    fastgltf::Options options = fastgltf::Options::LoadExternalBuffers
                              | fastgltf::Options::LoadExternalImages
                              | fastgltf::Options::GenerateMeshIndices;

    auto parentPath = std::filesystem::path(path).parent_path();
    auto assetResult = (extension == ".glb")
        ? parser.loadGltfBinary(gltfFile.get(), parentPath, options)
        : parser.loadGltf(gltfFile.get(), parentPath, options);

    if (assetResult.error() != fastgltf::Error::None) {
        fatalError("Failed to load GLTF: " + path);
    }

    auto asset = std::move(assetResult.get());
    if (asset.meshes.empty()) {
        spdlog::warn("[Scene] GLTF has no meshes: {}", path);
        return;
    }

    // Load all meshes and their primitives
    for (size_t meshIdx = 0; meshIdx < asset.meshes.size(); ++meshIdx) {
        auto& mesh = asset.meshes[meshIdx];
        for (size_t primIdx = 0; primIdx < mesh.primitives.size(); ++primIdx) {
            auto& primitive = mesh.primitives[primIdx];
            auto* posIt = primitive.findAttribute("POSITION");
            if (posIt == primitive.attributes.end()) continue;
            auto& posAccessor = asset.accessors[posIt->accessorIndex];

            auto* normalIt = primitive.findAttribute("NORMAL");
            if (normalIt == primitive.attributes.end()) continue;
            auto& normalAccessor = asset.accessors[normalIt->accessorIndex];

            auto* uvIt = primitive.findAttribute("TEXCOORD_0");
            bool hasUV = (uvIt != primitive.attributes.end());

            // Read vertices
            std::vector<Vertex> vertices;
            vertices.resize(posAccessor.count);

            fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, posAccessor, [&](glm::vec3 pos, std::size_t idx) {
                vertices[idx].position = pos;
            });

            fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, normalAccessor, [&](glm::vec3 normal, std::size_t idx) {
                vertices[idx].normal = normal;
            });

            if (hasUV) {
                auto& uvAccessor = asset.accessors[uvIt->accessorIndex];
                fastgltf::iterateAccessorWithIndex<glm::vec2>(asset, uvAccessor, [&](glm::vec2 uv, std::size_t idx) {
                    vertices[idx].texCoord = uv;
                });
            }

            // Read indices
            std::vector<uint32_t> indices;
            if (primitive.indicesAccessor.has_value()) {
                auto& idxAccessor = asset.accessors[primitive.indicesAccessor.value()];
                indices.resize(idxAccessor.count);
                if (idxAccessor.componentType == fastgltf::ComponentType::UnsignedShort) {
                    fastgltf::iterateAccessorWithIndex<std::uint16_t>(asset, idxAccessor, [&](std::uint16_t index, std::size_t idx) {
                        indices[idx] = static_cast<uint32_t>(index);
                    });
                } else {
                    fastgltf::iterateAccessorWithIndex<std::uint32_t>(asset, idxAccessor, [&](std::uint32_t index, std::size_t idx) {
                        indices[idx] = index;
                    });
                }
            } else {
                indices.resize(vertices.size());
                for (uint32_t i = 0; i < vertices.size(); ++i) indices[i] = i;
            }

            // Load material / texture
            std::string texturePath;
            glm::vec4 baseColorFactor = glm::vec4(1.0f);
            float metallic = 0.0f;
            float roughness = 1.0f;

            if (primitive.materialIndex.has_value()) {
                auto& material = asset.materials[primitive.materialIndex.value()];
                if (material.pbrData.baseColorTexture.has_value()) {
                    auto& texInfo = material.pbrData.baseColorTexture.value();
                    auto& textureAsset = asset.textures[texInfo.textureIndex];
                    if (textureAsset.imageIndex.has_value()) {
                        auto& image = asset.images[textureAsset.imageIndex.value()];
                        if (auto* uriPtr = std::get_if<fastgltf::sources::URI>(&image.data)) {
                            std::filesystem::path tp = parentPath / std::string(uriPtr->uri.string());
                            if (std::filesystem::exists(tp)) {
                                texturePath = tp.string();
                            }
                        }
                    }
                }
                baseColorFactor = glm::vec4(
                    material.pbrData.baseColorFactor.x(),
                    material.pbrData.baseColorFactor.y(),
                    material.pbrData.baseColorFactor.z(),
                    material.pbrData.baseColorFactor.w());
                metallic = material.pbrData.metallicFactor;
                roughness = material.pbrData.roughnessFactor;
            }

            std::string meshKey = path + "#" + std::to_string(meshIdx) + ":" + std::to_string(primIdx);
            Mesh* meshPtr = getOrLoadMesh(ctx, meshKey, vertices, indices);
            Texture* texturePtr = getOrLoadTexture(ctx, texturePath);

            ModelInstance inst;
            inst.mesh = meshPtr;
            inst.transform = glm::scale(glm::mat4(1.0f), glm::vec3(scale));
            inst.pendingAlbedoMap = texturePtr;
            inst.pendingBaseColorFactor = baseColorFactor;
            inst.pendingMetallic = metallic;
            inst.pendingRoughness = roughness;
            m_instances.push_back(inst);
        }
    }
}

// ============================================================================
// Resource loaders with path-based deduplication
// ============================================================================

Mesh* Scene::getOrLoadMesh(Context& ctx, const std::string& path) {
    auto it = m_meshMap.find(path);
    if (it != m_meshMap.end()) {
        return it->second;
    }
    auto mesh = std::make_unique<Mesh>(Mesh::loadObj(ctx, path));
    Mesh* ptr = mesh.get();
    m_meshes.push_back(std::move(mesh));
    m_meshMap[path] = ptr;
    return ptr;
}

Mesh* Scene::getOrLoadMesh(Context& ctx, const std::string& key,
                           const std::vector<Vertex>& vertices,
                           const std::vector<uint32_t>& indices) {
    auto it = m_meshMap.find(key);
    if (it != m_meshMap.end()) {
        return it->second;
    }
    auto mesh = std::make_unique<Mesh>(ctx, vertices, indices);
    Mesh* ptr = mesh.get();
    m_meshes.push_back(std::move(mesh));
    m_meshMap[key] = ptr;
    return ptr;
}

Texture* Scene::getOrLoadTexture(Context& ctx, const std::string& path) {
    if (path.empty()) return nullptr;
    auto it = m_textureMap.find(path);
    if (it != m_textureMap.end()) {
        return it->second;
    }
    auto texture = std::make_unique<Texture>(ctx, path);
    Texture* ptr = texture.get();
    m_textures.push_back(std::move(texture));
    m_textureMap[path] = ptr;
    return ptr;
}

void Scene::rebuildLightViews() {
    m_lights.clear();
    m_lights.push_back(&m_directionalLight);
    for (auto& point : m_pointLights) {
        m_lights.push_back(&point);
    }
}

void Scene::addGroundPlane(Context& ctx, float size, float y) {
    const float half = size * 0.5f;
    std::vector<Vertex> vertices = {
        {{-half, y, -half}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ half, y, -half}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ half, y,  half}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-half, y,  half}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
    };
    std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};

    Mesh* meshPtr = getOrLoadMesh(ctx, "__scene_ground_plane__", vertices, indices);
    Texture* texturePtr = getOrLoadTexture(ctx, "assets/textures/container.png");

    ModelInstance inst;
    inst.mesh = meshPtr;
    inst.transform = glm::mat4(1.0f);
    inst.pendingAlbedoMap = texturePtr;
    inst.pendingBaseColorFactor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    inst.pendingMetallic = 0.0f;
    inst.pendingRoughness = 0.9f;
    m_instances.push_back(inst);

    spdlog::info("[Scene] Added ground plane (size={:.1f}, y={:.2f})", size, y);
}

void Scene::addLightVisualizers(Context& ctx, float size) {
    // Load a sphere primitive for light visualization
    Mesh* meshPtr = getOrLoadMesh(ctx, "assets/models/Primitives/sphere.obj");
    Texture* texturePtr = getOrLoadTexture(ctx, "assets/textures/container.png");

    // Visualize point lights as bright yellow spheres
    for (const auto& point : m_pointLights) {
        ModelInstance inst;
        inst.mesh = meshPtr;
        inst.transform = glm::translate(glm::mat4(1.0f), point.position)
                       * glm::scale(glm::mat4(1.0f), glm::vec3(size));
        inst.pendingAlbedoMap = texturePtr;
        inst.pendingBaseColorFactor = glm::vec4(1.0f, 0.9f, 0.2f, 1.0f); // bright yellow
        inst.pendingMetallic = 0.0f;
        inst.pendingRoughness = 0.1f;
        inst.unlit = true;  // rendered by LightVisualizePass
        m_instances.push_back(inst);
    }

    spdlog::info("[Scene] Added {} light visualizer(s)", m_pointLights.size());
}

} // namespace kazu
