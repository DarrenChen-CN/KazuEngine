// ============================================================================
// KazuEngine - Scene Layer: Scene (Implementation)
// ============================================================================

#include "Scene.h"
#include "../core/Path.h"
#include "../core/Utils.h"
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

void Scene::loadFromFile(Context& ctx, ShaderLibrary& shaderLib, DescriptorSetLayoutCache& dslCache,
                         const std::string& scenePath) {
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

    // Parse window
    auto window = j.value("window", json::object());
    m_config.windowWidth = window.value("width", 1280);
    m_config.windowHeight = window.value("height", 720);

    spdlog::info("[Scene] Camera Eye: ({:.1f}, {:.1f}, {:.1f})", m_config.cameraEye.x, m_config.cameraEye.y, m_config.cameraEye.z);
    spdlog::info("[Scene] Light Position: ({:.1f}, {:.1f}, {:.1f})", m_config.lightPos.x, m_config.lightPos.y, m_config.lightPos.z);
    spdlog::info("[Scene] Loading models...");

    // Parse models
    for (const auto& model : j.value("models", json::array())) {
        std::string path = model.value("path", "");
        std::string format = model.value("format", "obj");
        float scale = model.value("scale", 1.0f);

        if (path.empty()) continue;

        if (format == "obj") {
            loadObjModel(ctx, shaderLib, dslCache, path, scale);
        } else if (format == "gltf" || format == "glb") {
            loadGltfModel(ctx, shaderLib, dslCache, path, scale);
        } else {
            spdlog::warn("[Scene] Unknown model format: {}", format);
        }
    }

    spdlog::info("[Scene] Loaded {} model(s) from {}", m_models.size(), scenePath);
}

void Scene::draw(VkCommandBuffer cmd, VkPipelineLayout pipelineLayout) {
    for (auto& model : m_models) {
        if (model.material && model.material->descriptorSet() != VK_NULL_HANDLE) {
            VkDescriptorSet ds = model.material->descriptorSet();
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                    0, 1, &ds, 0, nullptr);
        }
        if (model.mesh) {
            model.mesh->draw(cmd);
        }
    }
}

void Scene::loadObjModel(Context& ctx, ShaderLibrary& shaderLib, DescriptorSetLayoutCache& dslCache,
                         const std::string& path, float scale) {
    auto mesh = std::make_unique<Mesh>(Mesh::loadObj(ctx, path));

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

    ModelInstance instance;
    instance.mesh = std::move(mesh);
    instance.material = std::make_unique<Material>(ctx, shaderLib, dslCache);
    instance.material->setShaders(
        kazu::Path::resolveShader("triangle.vert.spv"),
        kazu::Path::resolveShader("triangle.frag.spv"));

    if (!texturePath.empty()) {
        instance.texture = std::make_unique<Texture>(ctx, texturePath);
        instance.material->setTexture(0, *instance.texture);
    }
    instance.material->build();

    m_models.push_back(std::move(instance));
}

void Scene::loadGltfModel(Context& ctx, ShaderLibrary& shaderLib, DescriptorSetLayoutCache& dslCache,
                          const std::string& path, float scale) {
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
    for (auto& mesh : asset.meshes) {
        for (auto& primitive : mesh.primitives) {
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
                vertices[idx].position = pos * scale;
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

            auto meshObj = std::make_unique<Mesh>(ctx, vertices, indices);

            // Load material / texture
            std::unique_ptr<Texture> texture;
            if (primitive.materialIndex.has_value()) {
                auto& material = asset.materials[primitive.materialIndex.value()];
                if (material.pbrData.baseColorTexture.has_value()) {
                    auto& texInfo = material.pbrData.baseColorTexture.value();
                    auto& textureAsset = asset.textures[texInfo.textureIndex];
                    if (textureAsset.imageIndex.has_value()) {
                        auto& image = asset.images[textureAsset.imageIndex.value()];
                        if (auto* uriPtr = std::get_if<fastgltf::sources::URI>(&image.data)) {
                            std::filesystem::path texturePath = parentPath / std::string(uriPtr->uri.string());
                            if (std::filesystem::exists(texturePath)) {
                                texture = std::make_unique<Texture>(ctx, texturePath.string());
                            }
                        }
                    }
                }
            }

            ModelInstance instance;
            instance.mesh = std::move(meshObj);
            instance.material = std::make_unique<Material>(ctx, shaderLib, dslCache);
            instance.material->setShaders(
                kazu::Path::resolveShader("triangle.vert.spv"),
                kazu::Path::resolveShader("triangle.frag.spv"));
            if (texture) {
                instance.texture = std::move(texture);
                instance.material->setTexture(0, *instance.texture);
            }
            instance.material->build();

            m_models.push_back(std::move(instance));
        }
    }
}

} // namespace kazu
