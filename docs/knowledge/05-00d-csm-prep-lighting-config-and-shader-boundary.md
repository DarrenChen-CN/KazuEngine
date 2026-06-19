# 05-00d CSM 前置改造：Lighting 配置、Shader 边界与 Light Visual

> Status: 05-00d prep implemented; ready to start 05-02a CSM.

这篇文档回答一个问题：在继续实现 CSM 之前，当前代码应该先怎么调整，才能避免 LightingPass / lighting.frag / Scene 配置继续膨胀失控。

核心结论：

- CSM 是 ShadowMap 家族的升级，不是新的 LightingPass。
- Lighting shader 需要拆出功能边界，否则 CSM / IBL / SSAO 继续加进去会变成一个巨大的总控 shader。
- Light marker 是 debug visual，不是普通受光模型。
- 光源、阴影模式、光照模型、feature toggle 应该由 scene/config 指定，UI 只做运行时覆盖。

## 1. 当前问题

### 1.1 Lighting shader 已经开始臃肿

当前 `lighting.frag` 已经承担：

- GBuffer 解包
- depth -> world position reconstruction
- Lambert direct lighting
- PBR direct lighting
- shadow map projection
- Hard shadow
- PCF
- PCSS
- debug display mode

这在 05-01 / 05-02 阶段还能接受，因为功能还少。但 CSM 会继续加入：

- cascade split distances
- cascade index selection
- cascade shadow matrix array
- texture array / 多张 shadow map 采样
- cascade debug view
- cascade blend / border handling

如果直接继续塞进 `lighting.frag`，这个 shader 很快会变成难以维护的“大泥球”。

### 1.2 Light marker 不能作为普通模型渲染

如果用一个球表示 light，并让它进入 GBuffer / Lighting / ShadowMap，那么它会出现不合理行为：

- 被主光照照亮，明暗来自 BRDF，而不是 debug color。
- 被 shadow map 判定成一半亮一半暗。
- 可能参与投影，污染真实场景阴影。
- debug marker 和真实 renderable 的语义混在一起。

Light marker 的职责只是“告诉开发者光在哪里”，不是参与真实光照。

### 1.3 配置来源太分散

当前很多选项来自 C++ 默认值或 UI：

- lighting model
- shadow mode
- PCF / PCSS 参数
- light type / position / direction
- 是否显示 light marker

这会带来两个问题：

- Demo scene 无法自描述。换一个 scene 后，不知道它期待什么光照技术。
- 后续测试 CSM / IBL / TAA 时，无法通过配置文件固定复现。

Week 5 后续应该转向 config-driven pipeline。

## 2. 推荐改造顺序

在正式写 CSM 前，推荐按这个顺序调整：

```text
05-00d-1 RendererConfig / LightingSettings 从 scene JSON 读取
05-00d-2 Light 数据模型整理：Directional / Point / visual marker flags
05-00d-3 lighting.frag 功能分区或 shader include 预处理
05-00d-4 LightVisualizePass 明确 overlay 规则
05-02a   CSM
```

这几个小改造不会改变渲染结果太多，但会让 CSM 接入更稳。

当前实现状态：

- [x] `LightingSettings` / lighting enums 已从 `LightingPass.h` 移到中性头文件 `scene/RendererSettings.h`。
- [x] `Scene` 已解析 `renderer.lightingModel`、`renderer.shadow`、`renderer.features`。
- [x] `DeferredShading` 第一次初始化时从 `Scene::rendererSettings()` 读取默认 lighting settings。
- [x] `Light` 已增加 `castsShadow` / `visualize` flags。
- [x] `Scene` 已兼容旧 `"light"` 字段，并支持新 `"lights": []` 中的 directional / point light。
- [x] `LightVisualizePass` 已接入 graph：`Lighting -> LightVisualize -> Present`。
- [x] `Scene::draw()` 和 `ShadowMapPass` 会跳过 `unlit` marker。
- [x] RenderGraph 对 read+write 同一 color attachment 的 pass 使用 `LOAD`，避免 overlay 清掉已有 SceneColor。
- [x] `lighting.frag` 已按 Constants / GBuffer / Shadow / BRDF / Main 分区。
- [x] ShadowMap 第一版已支持 directional / point 两类 shadow camera：directional 使用虚拟 eye + ortho，point 使用 position + perspective。

## 3. RendererConfig

建议新增一个轻量配置结构，不要让 `Scene` 只保存模型和 light，也要保存本场景期望的 renderer 设置。

建议结构：

```cpp
struct ShadowSettings {
    int mode = ShadowMode_Hard;
    int filter = ShadowFilter_PCF;
    int cascadeCount = 1;
    float bias = 0.005f;
    int pcfSampleCount = 1;
    float pcfFilterSize = 0.005f;
    float lightWidth = 0.05f;
};

struct FeatureSettings {
    bool ibl = false;
    bool ssao = false;
    bool bloom = false;
    bool taa = false;
};

struct RendererSettings {
    int lightingModel = LightingModel_PBR;
    ShadowSettings shadow;
    FeatureSettings features;
};
```

短期可以继续复用现有 `LightingSettings`，但解析入口应该来自 scene JSON：

```text
Scene::loadFromFile()
  parse camera
  parse renderer settings
  parse lights
  parse models

DeferredShading::onInit()
  m_lightingSettings = scene->rendererSettings().toLightingSettings()
  LightingPass::setSettings(m_lightingSettings)
```

UI 的职责变成：

```text
初始值来自配置文件
UI 修改运行时 copy
不反写 scene JSON
```

## 4. Scene JSON 建议格式

下一版 scene JSON 建议采用：

```json
{
  "camera": {
    "eye": [0.0, 1.5, 5.0],
    "target": [0.0, 0.6, 0.0],
    "up": [0.0, 1.0, 0.0]
  },
  "renderer": {
    "technique": "deferred",
    "lightingModel": "pbr",
    "shadow": {
      "mode": "csm",
      "filter": "pcf",
      "cascadeCount": 4,
      "bias": 0.005,
      "pcfSamples": 9,
      "pcfFilterSize": 0.003,
      "lightWidth": 0.05
    },
    "features": {
      "ibl": false,
      "ssao": false,
      "bloom": false,
      "taa": false
    }
  },
  "lights": [
    {
      "type": "directional",
      "direction": [-0.5, -1.0, -0.3],
      "color": [1.0, 0.96, 0.9],
      "intensity": 3.0,
      "shadow": true,
      "visualize": false
    },
    {
      "type": "point",
      "position": [1.5, 2.0, 1.0],
      "color": [1.0, 0.8, 0.6],
      "intensity": 5.0,
      "range": 8.0,
      "shadow": false,
      "visualize": true
    }
  ]
}
```

兼容策略：

- 保留旧的 `"light": { ... }`，用于旧 demo。
- 新场景优先使用 `"lights": []`。
- 如果没有 `"renderer"`，使用当前 C++ 默认 `LightingSettings`。

## 5. Light 数据模型

短期保留：

```cpp
struct DirectionalLight {
    glm::vec3 direction;
    glm::vec3 color;
    float intensity;
};

struct PointLight {
    glm::vec3 position;
    glm::vec3 color;
    float intensity;
    float range;
};
```

建议补充 render control 字段：

```cpp
struct LightRenderFlags {
    bool castsShadow = false;
    bool visualize = false;
};
```

或者直接放进各 light：

```cpp
bool castsShadow = false;
bool visualize = false;
```

CSM 第一版只处理一个主方向光：

```text
main directional light
  -> castsShadow = true
  -> CSM source
```

点光源暂时只参与 light marker / 后续 direct lighting，不做 cube shadow。

当前单张 ShadowMap 的过渡策略：

```text
if directionalLight.castsShadow:
    use directional shadow camera
else:
    use first point light with castsShadow
```

方向光本身没有真实位置，但 shadow map 渲染需要一个 light view。做法是构造虚拟 eye：

```text
center = scene bounds center
direction = directional light travel direction
eye = center - direction * distance
view = lookAt(eye, center)
proj = ortho(-radius, radius, -radius, radius, near, far)
```

这个虚拟位置只服务于 shadow map 投影，不表示方向光真的有世界空间位置。CSM 后续也是同一个思想，只是每个 cascade 用自己的 frustum bounds 来求 ortho。

## 6. Light Visual 规则

Light marker 不是普通 geometry。它应该走独立 pass：

```text
LightVisualizePass
  reads SceneColorHDR
  writes SceneColorHDR
  draws unlit marker mesh
```

规则：

- 不进入 GBuffer。
- 不进入 ShadowMap / CSM caster list。
- 不经过 LightingPass。
- 不接受 shadow。
- 不投射 shadow。

当前 `LightVisualizePass` 的方向是对的：它已经是 unlit overlay，并且写在 `SceneColorHDR` 上。

后续可以加两种模式：

```text
Overlay:
  depthTest = false
  永远显示，适合 debug

Scene Gizmo:
  depthTest = true
  可被场景遮挡，适合 editor-like 显示
```

无论哪种，light marker 都不应该进入主 lighting/shadow 逻辑。

## 7. Lighting Shader 拆分策略

### 7.1 短期：单文件分区

如果现在 shader 编译链不支持 include，先把 `lighting.frag` 整理成明确 section：

```glsl
// ----------------------------------------------------------------------------
// Constants / Push Constants
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// GBuffer Decode
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// BRDF
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// Shadow
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// CSM
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// Debug Views
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------------
```

这个阶段只做整理，不改变 shader 行为。

### 7.2 中期：shader include / pre-concat

CSM 完成后建议引入 shader include：

```text
shaders/common/gbuffer.glsl
shaders/common/brdf.glsl
shaders/common/shadow.glsl
shaders/common/csm.glsl
shaders/common/debug.glsl
```

`lighting.frag` 变成组合入口：

```glsl
#version 450

#include "common/gbuffer.glsl"
#include "common/brdf.glsl"
#include "common/shadow.glsl"
#include "common/csm.glsl"

void main() {
    ...
}
```

如果 `glslangValidator` 的 include 支持不稳定，可以在 CMake 里预拼接：

```text
lighting.frag.glsl
  -> preprocess
  -> build/generated/lighting.frag
  -> glslangValidator
```

不要为了拆 shader 而拆 pass。pass 是 GPU 阶段，shader include 是代码组织手段。

## 8. CSM 接入边界

## 8.1 PBR Texture Maps 当前状态

在进入 CSM 之前，材质输入已经从单一 albedo/标量参数扩展到基础 PBR texture set：

```text
Albedo/BaseColor:      sRGB
Normal:                linear
MetallicRoughness:     linear, glTF convention: G=roughness, B=metallic
AO:                    linear, R=ao
```

这部分仍然属于 GBuffer/Material 边界，不应该和 ShadowMap/CSM 混在一起：

```text
Scene / Model loader
  -> resolve texture paths and material factors

GBufferPass
  -> sample material textures
  -> write base color / world normal / metallic / roughness / ao

ShadowMap / CSM
  -> only need depth caster geometry
  -> do not need PBR texture set

LightingPass
  -> read GBuffer
  -> apply BRDF + shadow visibility
```

因此后续做 CSM 时，优先保持这个边界：

- 不要让 ShadowMapPass 依赖 albedo/normal/MR/AO 贴图。
- 不要把 shadow filter、cascade selection 写进 GBuffer。
- Light visualizer 仍然走 unlit overlay，不进入 GBuffer/ShadowMap。
- Metallic 材质在没有 IBL 前仍然会偏暗；当前 `lighting.frag` 里的 ambient specular approximation 只是过渡方案，05-03 IBL 后应替换。

CSM 的正确位置：

```text
CSMShadowPass / ShadowMapPass
  input: Scene geometry + main DirectionalLight + Camera frustum
  output: cascade shadow depth textures

LightingPass
  input: GBuffer + cascade shadow textures + cascade matrices
  behavior: choose cascade, sample shadow, evaluate lighting
```

不要做：

- 不要新增 `CSMLightingPass`。
- 不要把 cascade split 计算塞进 fragment shader。
- 不要让 point light 的 perspective shadow 和 directional CSM 混成同一套临时逻辑。
- 不要让 light marker 进入 CSM shadow caster。

CSM 第一版只需要：

- 方向光。
- 4 cascades 或可配置 cascade count。
- 每个 cascade 一个 light view/proj。
- texture array 或多张 shadow map。
- Lighting shader 根据 view-space depth 选择 cascade。
- cascade debug view。

稳定性优化可以后置：

- stable CSM
- snap to texel
- cascade blending
- slope-scale bias

## 9. 推荐实施任务

### 05-00d-1 Config 解析

目标：

- `Scene` 解析 `renderer`。
- `DeferredShading` 从 scene 获取初始 `LightingSettings`。
- UI 只覆盖运行时设置。

验证：

- scene JSON 中 `"lightingModel": "lambert"` 能默认切到 Lambert。
- scene JSON 中 `"shadow.mode": "none"` 能默认关闭阴影。

### 05-00d-2 Light flags

目标：

- `DirectionalLight` / `PointLight` 支持 `castsShadow` / `visualize`。
- `LightVisualizePass` 只画 `visualize = true` 的 light marker。
- Shadow pass 只处理 `castsShadow = true` 的主光源。

验证：

- light marker 不被阴影切黑。
- 关闭 visualize 后不显示 marker。

### 05-00d-3 Shader section cleanup

目标：

- 不改变渲染结果，只整理 `lighting.frag` 结构。
- 为 CSM section 预留位置。

验证：

- 构建通过。
- PBR / shadow / debug view 行为不变。

### 05-02a CSM

目标：

- 在上述边界清晰后实现 cascade shadow。

验证：

- cascade debug view 正常。
- 近处阴影清晰，远处阴影可接受。
- light marker 不参与 shadow。

## 10. 最终判断

现在不要直接冲 CSM 代码。先做很小的一轮 05-00d 前置整理：

```text
配置接管默认 lighting/shadow settings
Light 数据补 flags
Lighting shader 分区
```

完成这三件事后，CSM 的改动会集中在 `ShadowMapPass / LightingPass / lighting.frag` 的明确位置，而不是继续把临时状态散落到 UI、Scene、Technique 和 shader 里。
