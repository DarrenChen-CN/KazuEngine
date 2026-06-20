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

namespace {

glm::vec3 readVec3(const json& node, const char* key, const glm::vec3& fallback) {
    auto values = node.value(key, std::vector<float>{fallback.x, fallback.y, fallback.z});
    return glm::vec3(values[0],
                     values.size() > 1 ? values[1] : fallback.y,
                     values.size() > 2 ? values[2] : fallback.z);
}

int parseLightingModel(const std::string& value, int fallback) {
    if (value == "lambert") return LightingModel_Lambert;
    if (value == "pbr") return LightingModel_PBR;
    return fallback;
}

int parseShadowMode(const std::string& value, int fallback) {
    if (value == "none") return ShadowMode_None;
    if (value == "hard") return ShadowMode_Hard;
    if (value == "pcf") return ShadowMode_PCF;
    if (value == "pcss") return ShadowMode_PCSS;
    if (value == "csm") return ShadowMode_CSM;
    return fallback;
}

} // anonymous namespace

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

    m_rendererSettings = RendererSettings{};
    auto renderer = j.value("renderer", json::object());
    if (!renderer.empty()) {
        m_rendererSettings.lighting.lightingModel =
            parseLightingModel(renderer.value("lightingModel", std::string{}),
                               m_rendererSettings.lighting.lightingModel);

        auto shadow = renderer.value("shadow", json::object());
        if (!shadow.empty()) {
            m_rendererSettings.lighting.shadowMode =
                parseShadowMode(shadow.value("mode", std::string{}),
                                m_rendererSettings.lighting.shadowMode);
            m_rendererSettings.lighting.shadowBias =
                shadow.value("bias", m_rendererSettings.lighting.shadowBias);
            m_rendererSettings.lighting.pcfSampleCount =
                shadow.value("pcfSamples", m_rendererSettings.lighting.pcfSampleCount);
            m_rendererSettings.lighting.pcfFilterSize =
                shadow.value("pcfFilterSize", m_rendererSettings.lighting.pcfFilterSize);
            m_rendererSettings.lighting.lightWidth =
                shadow.value("lightWidth", m_rendererSettings.lighting.lightWidth);
        }

        auto features = renderer.value("features", json::object());
        if (!features.empty()) {
            m_rendererSettings.lighting.enableIBL =
                features.value("ibl", m_rendererSettings.lighting.enableIBL);
            m_rendererSettings.lighting.enableSSAO =
                features.value("ssao", m_rendererSettings.lighting.enableSSAO);
            m_rendererSettings.lighting.enableBloom =
                features.value("bloom", m_rendererSettings.lighting.enableBloom);
            m_rendererSettings.lighting.enableTAA =
                features.value("taa", m_rendererSettings.lighting.enableTAA);
            m_rendererSettings.lighting.bloomThreshold =
                features.value("bloomThreshold", m_rendererSettings.lighting.bloomThreshold);
            m_rendererSettings.lighting.bloomIntensity =
                features.value("bloomIntensity", m_rendererSettings.lighting.bloomIntensity);
        }
    }

    // Parse environment (IBL source)
    auto environment = j.value("environment", json::object());
    if (!environment.empty()) {
        m_rendererSettings.environment.enabled = environment.value("enabled", false);
        m_rendererSettings.environment.hdrPath = environment.value("hdr", std::string{});
        if (m_rendererSettings.environment.enabled) {
            m_rendererSettings.lighting.enableIBL = true;
        }
    }

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
    m_directionalLight.castsShadow = light.value("shadow", true);
    m_directionalLight.visualize = light.value("visualize", false);

    m_pointLights.clear();
    auto parsePointLight = [&](const json& node) {
        PointLight point{};
        point.position = readVec3(node, "position", glm::vec3(2.0f, 3.0f, 2.0f));
        point.color = readVec3(node, "color", glm::vec3(1.0f));
        point.intensity = node.value("intensity", 1.0f);
        point.range = node.value("range", 10.0f);
        point.castsShadow = node.value("shadow", true);
        point.visualize = node.value("visualize", false);
        m_pointLights.push_back(point);
    };
    auto parseDirectionalLight = [&](const json& node) {
        glm::vec3 parsedDirection = readVec3(node, "direction", m_directionalLight.direction);
        if (glm::length(parsedDirection) < 0.0001f) {
            parsedDirection = m_directionalLight.direction;
        }
        m_directionalLight.direction = glm::normalize(parsedDirection);
        m_directionalLight.color = readVec3(node, "color", m_directionalLight.color);
        m_directionalLight.intensity = node.value("intensity", m_directionalLight.intensity);
        m_directionalLight.castsShadow = node.value("shadow", true);
        m_directionalLight.visualize = node.value("visualize", false);
    };

    if (light.value("type", std::string{}) == "point") {
        parsePointLight(light);
    } else if (light.value("type", std::string{}) == "directional") {
        parseDirectionalLight(light);
    }
    if (j.contains("pointLights") && j["pointLights"].is_array()) {
        for (const auto& point : j["pointLights"]) {
            parsePointLight(point);
        }
    }
    if (j.contains("lights") && j["lights"].is_array()) {
        for (const auto& item : j["lights"]) {
            auto type = item.value("type", std::string{});
            if (type == "point") {
                parsePointLight(item);
            } else if (type == "directional") {
                parseDirectionalLight(item);
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
    spdlog::info("[Scene] Renderer Settings: lightingModel={}, shadowMode={}, pcfSamples={}",
                 m_rendererSettings.lighting.lightingModel,
                 m_rendererSettings.lighting.shadowMode,
                 m_rendererSettings.lighting.pcfSampleCount);
    spdlog::info("[Scene] Loading models...");

    // Parse models
    for (const auto& model : j.value("models", json::array())) {
        std::string path = model.value("path", "");
        std::string format = model.value("format", "obj");
        float scale = model.value("scale", 1.0f);
        auto pos = model.value("position", std::vector<float>{0.0f, 0.0f, 0.0f});
        glm::vec3 position(pos[0], pos.size() > 1 ? pos[1] : 0.0f, pos.size() > 2 ? pos[2] : 0.0f);
        bool snapToGround = model.value("snapToGround", true);
        auto baseColor = model.value("baseColor", std::vector<float>{1.0f, 1.0f, 1.0f, 1.0f});
        glm::vec4 baseColorFactor(baseColor[0],
                                  baseColor.size() > 1 ? baseColor[1] : 1.0f,
                                  baseColor.size() > 2 ? baseColor[2] : 1.0f,
                                  baseColor.size() > 3 ? baseColor[3] : 1.0f);
        std::string texturePathOverride = model.value("albedoTexture", model.value("texture", ""));
        std::string normalTexturePath = model.value("normalTexture", "");
        std::string metallicRoughnessTexturePath = model.value("metallicRoughnessTexture", "");
        std::string aoTexturePath = model.value("aoTexture", "");
        float metallic = model.value("metallic", 0.0f);
        float roughness = model.value("roughness", 1.0f);
        float ao = model.value("ao", 1.0f);
        bool flipV = model.value("flipV", format == "obj");

        if (path.empty()) continue;

        if (format == "obj") {
            loadObjModel(ctx, path, scale, position, snapToGround,
                         texturePathOverride, normalTexturePath,
                         metallicRoughnessTexturePath, aoTexturePath,
                         baseColorFactor, metallic, roughness, ao, flipV);
        } else if (format == "gltf" || format == "glb") {
            loadGltfModel(ctx, path, scale, position, snapToGround);
        } else {
            spdlog::warn("[Scene] Unknown model format: {}", format);
        }
    }

    spdlog::info("[Scene] Loaded {} model(s) from {}", m_instances.size(), scenePath);

    // Add a ground plane for shadow visualization.
    addGroundPlane(ctx);

    bool hasLightVisualizer = false;
    for (const auto& point : m_pointLights) {
        hasLightVisualizer = hasLightVisualizer || point.visualize;
    }
    if (hasLightVisualizer) {
        addLightVisualizers(ctx);
    }

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
    Texture* fallbackTexture = getOrLoadTexture(ctx, "assets/textures/white.png", true);
    for (auto& inst : m_instances) {
        if (inst.material != nullptr) continue;  // already built

        auto mat = std::make_unique<PBRMaterial>();
        mat->setEffect(effect);
        mat->albedoMap = inst.pendingAlbedoMap ? inst.pendingAlbedoMap : fallbackTexture;
        mat->normalMap = inst.pendingNormalMap ? inst.pendingNormalMap : fallbackTexture;
        mat->metallicRoughnessMap = inst.pendingMetallicRoughnessMap ? inst.pendingMetallicRoughnessMap : fallbackTexture;
        mat->aoMap = inst.pendingAoMap ? inst.pendingAoMap : fallbackTexture;
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
        if (inst.unlit) continue;

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
                         const glm::vec3& position, bool snapToGround,
                         const std::string& texturePathOverride,
                         const std::string& normalTexturePath,
                         const std::string& metallicRoughnessTexturePath,
                         const std::string& aoTexturePath,
                         const glm::vec4& baseColorFactor,
                         float metallic,
                         float roughness,
                         float ao,
                         bool flipV) {
    // Use explicit scene texture when available; otherwise keep the legacy
    // OBJ fallback of picking an image from the model directory.
    std::filesystem::path modelPath(path);
    std::filesystem::path dir = modelPath.parent_path();
    std::string texturePath = texturePathOverride;
    if (texturePath.empty()) {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            auto ext = entry.path().extension().string();
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
                texturePath = entry.path().string();
                break;
            }
        }
    }

    Mesh* meshPtr = getOrLoadMesh(ctx, path);
    Texture* texturePtr = getOrLoadTexture(ctx, texturePath);
    Texture* normalTexturePtr = getOrLoadTexture(ctx, normalTexturePath, false);
    Texture* metallicRoughnessTexturePtr = getOrLoadTexture(ctx, metallicRoughnessTexturePath, false);
    Texture* aoTexturePtr = getOrLoadTexture(ctx, aoTexturePath, false);

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
    inst.pendingNormalMap = normalTexturePtr;
    inst.pendingMetallicRoughnessMap = metallicRoughnessTexturePtr;
    inst.pendingAoMap = aoTexturePtr;
    inst.pendingBaseColorFactor = baseColorFactor;
    inst.pendingMetallic = glm::clamp(metallic, 0.0f, 1.0f);
    inst.pendingRoughness = glm::clamp(roughness, 0.04f, 1.0f);
    inst.pendingAo = glm::clamp(ao, 0.0f, 1.0f);
    inst.pendingFlipV = flipV;
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
            std::string normalTexturePath;
            std::string metallicRoughnessTexturePath;
            std::string aoTexturePath;
            glm::vec4 baseColorFactor = glm::vec4(1.0f);
            float metallic = 0.0f;
            float roughness = 1.0f;

            if (primitive.materialIndex.has_value()) {
                auto& material = asset.materials[primitive.materialIndex.value()];
                auto resolveTexturePath = [&](std::size_t textureIndex) -> std::string {
                    auto& textureAsset = asset.textures[textureIndex];
                    if (!textureAsset.imageIndex.has_value()) {
                        return {};
                    }
                    auto& image = asset.images[textureAsset.imageIndex.value()];
                    if (auto* uriPtr = std::get_if<fastgltf::sources::URI>(&image.data)) {
                        std::filesystem::path tp = parentPath / std::string(uriPtr->uri.string());
                        if (std::filesystem::exists(tp)) {
                            return tp.string();
                        }
                    }
                    return {};
                };

                if (material.pbrData.baseColorTexture.has_value()) {
                    texturePath = resolveTexturePath(material.pbrData.baseColorTexture->textureIndex);
                }
                if (material.normalTexture.has_value()) {
                    normalTexturePath = resolveTexturePath(material.normalTexture->textureIndex);
                }
                if (material.pbrData.metallicRoughnessTexture.has_value()) {
                    metallicRoughnessTexturePath =
                        resolveTexturePath(material.pbrData.metallicRoughnessTexture->textureIndex);
                }
                if (material.occlusionTexture.has_value()) {
                    aoTexturePath = resolveTexturePath(material.occlusionTexture->textureIndex);
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
            Texture* normalTexturePtr = getOrLoadTexture(ctx, normalTexturePath, false);
            Texture* metallicRoughnessTexturePtr = getOrLoadTexture(ctx, metallicRoughnessTexturePath, false);
            Texture* aoTexturePtr = getOrLoadTexture(ctx, aoTexturePath, false);

            ModelInstance inst;
            inst.mesh = meshPtr;
            glm::vec3 finalPosition = position;
            if (snapToGround && meshPtr->bounds().isValid()) {
                float minY = meshPtr->bounds().min.y;
                finalPosition.y = -0.1f - minY * scale;
            }
            inst.transform = glm::translate(glm::mat4(1.0f), finalPosition)
                           * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
            inst.pendingAlbedoMap = texturePtr;
            inst.pendingNormalMap = normalTexturePtr;
            inst.pendingMetallicRoughnessMap = metallicRoughnessTexturePtr;
            inst.pendingAoMap = aoTexturePtr;
            inst.pendingBaseColorFactor = baseColorFactor;
            inst.pendingMetallic = metallic;
            inst.pendingRoughness = roughness;
            inst.pendingFlipV = false;
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

Texture* Scene::getOrLoadTexture(Context& ctx, const std::string& path, bool srgb) {
    if (path.empty()) return nullptr;
    std::string key = path + (srgb ? "|srgb" : "|linear");
    auto it = m_textureMap.find(key);
    if (it != m_textureMap.end()) {
        return it->second;
    }
    auto texture = std::make_unique<Texture>(ctx, path, srgb);
    Texture* ptr = texture.get();
    m_textures.push_back(std::move(texture));
    m_textureMap[key] = ptr;
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
    Texture* texturePtr = getOrLoadTexture(ctx, "assets/textures/white.png");

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
    Texture* texturePtr = getOrLoadTexture(ctx, "assets/textures/white.png");

    // Visualize point lights as bright yellow spheres
    uint32_t visualizerCount = 0;
    for (const auto& point : m_pointLights) {
        if (!point.visualize) continue;

        ModelInstance inst;
        inst.mesh = meshPtr;
        inst.transform = glm::translate(glm::mat4(1.0f), point.position)
                       * glm::scale(glm::mat4(1.0f), glm::vec3(size));
        inst.pendingAlbedoMap = texturePtr;
        inst.pendingBaseColorFactor = glm::vec4(point.color * point.intensity, 1.0f);
        inst.pendingMetallic = 0.0f;
        inst.pendingRoughness = 0.1f;
        inst.unlit = true;  // rendered by LightVisualizePass
        m_instances.push_back(inst);
        ++visualizerCount;
    }

    spdlog::info("[Scene] Added {} light visualizer(s)", visualizerCount);
}

} // namespace kazu
