// ============================================================================
// KazuEngine - Scene Layer: Renderer Settings
// ============================================================================

#pragma once
#include <string>

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
};

struct EnvironmentSettings {
    std::string hdrPath;
    bool enabled = false;
};

struct RendererSettings {
    LightingSettings lighting;
    EnvironmentSettings environment;
};

} // namespace kazu
