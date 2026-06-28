// ============================================================================
// KazuEngine - Scene Layer: Renderer Settings
// ============================================================================

#pragma once
#include <string>
#include <glm/glm.hpp>

namespace kazu {

enum LightingModel : int {
    LightingModel_Lambert = 0,
    LightingModel_PBR = 1,
};

enum ShadowMode : int {
    ShadowMode_None = 0,
    ShadowMode_Hard = 1,
    ShadowMode_PCF = 2,
    ShadowMode_PCSS = 3,
    ShadowMode_CSM = 4,
};

enum LightingDebugView : int {
    LightingDebugView_Lit = 0,
    LightingDebugView_Albedo = 1,
    LightingDebugView_Normal = 2,
    LightingDebugView_ShadowMap = 3,
};

enum ToneMappingMode : int {
    ToneMappingMode_Reinhard = 0,
    ToneMappingMode_ACES = 1,
};

struct LightingSettings {
    int lightingModel = LightingModel_PBR;
    int shadowMode = ShadowMode_Hard;
    int debugView = LightingDebugView_Lit;

    float shadowBias = 0.005f;
    int   pcfSampleCount = 1;
    float pcfFilterSize = 0.005f;
    float lightWidth = 0.05f;

    bool enableIBL = false;
    bool enableSSAO = false;
    bool enableBloom = false;
    bool enableTAA = false;
    bool enableFXAA = true;

    float exposure = 1.0f;
    float gamma = 2.2f;
    int   toneMappingMode = ToneMappingMode_ACES;

    float bloomThreshold = 1.0f;
    float bloomIntensity = 0.1f;
};

struct EnvironmentSettings {
    std::string hdrPath;
    bool enabled = false;
};

struct GroundPlaneSettings {
    bool enabled = true;
    float size = 10.0f;
    float y = -0.1f;

    glm::vec4 baseColorFactor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    float metallic = 0.0f;
    float roughness = 0.9f;
    float ao = 1.0f;
};

struct RendererSettings {
    LightingSettings lighting;
    EnvironmentSettings environment;
    GroundPlaneSettings groundPlane;
};

} // namespace kazu
