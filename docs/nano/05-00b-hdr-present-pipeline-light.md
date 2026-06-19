# 05-00b HDR Present Pipeline + Light Boundary

## What

把 deferred pipeline 从“Lighting 直接写 swapchain”改为：

```text
GBuffer
ShadowMap
Lighting -> SceneColorHDR
LightVisualize -> SceneColorHDR
Present -> Swapchain
AppUI -> Swapchain
```

同时明确 Week 5 后续继续扩展前的几个边界：

- `LightingPass` 输出 HDR 中间纹理，不直接接触 swapchain。
- `PresentPass` 负责最终上屏，后续 Tone Mapping / Gamma / Bloom composite 都应放在这里或它之前。
- Light 数据属于 `Scene`，而不是散落在 pass / shader / UI 里。
- Light 可视化是 debug overlay，不应该被主光照和阴影系统影响。
- Lighting shader 需要开始模块化，否则 CSM / IBL / SSAO / TAA 接入后会快速膨胀。

## Why

Week 5 的后续功能都依赖一个可采样的 HDR scene color：

- Bloom 需要在 tone mapping 前读取 HDR 高亮区域。
- Tone Mapping / Gamma 应该在最终 Present 阶段统一处理。
- TAA 需要读取当前帧 color、history、depth/motion，而不是直接接 swapchain。
- IBL / SSAO / CSM 都会继续扩大 LightingPass 输入，必须提前约束边界。

如果 LightingPass 继续直接写 swapchain，后处理链会被卡死；如果 light visual 作为普通模型进入 GBuffer，它会被自己的 lighting/shadow 污染，不适合作为光源标记。

## Implemented

涉及文件：

- `src/pass/LightingPass.h`
- `src/pass/LightingPass.cpp`
- `src/pass/PresentPass.h`
- `src/pass/PresentPass.cpp`
- `src/pass/LightVisualizePass.h`
- `src/pass/LightVisualizePass.cpp`
- `src/scene/Light.h`
- `src/scene/Scene.h`
- `src/scene/Scene.cpp`
- `src/technique/DeferredShading.h`
- `src/technique/DeferredShading.cpp`
- `shaders/lighting.frag`
- `shaders/present.frag`
- `shaders/lightvisualize.vert`
- `shaders/lightvisualize.frag`

当前状态：

- `LightingPass` 声明 `SceneColorHDR` transient texture：
  - format: `VK_FORMAT_R16G16B16A16_SFLOAT`
  - usage: `COLOR_ATTACHMENT | SAMPLED`
- `LightingPass` 写 `SceneColorHDR`。
- `PresentPass` 读 `SceneColorHDR`，写 imported swapchain。
- `present.frag` 当前只做 clamp，不提前实现 ACES / Gamma。
- `Scene` 已有 `DirectionalLight` / `PointLight` 数据。
- `LightVisualizePass` 用 unlit shader 把 light marker 叠到 `SceneColorHDR`。

当前 RenderGraph 形态：

```text
GBuffer:
  writes Albedo / Normal / Material / Depth

ShadowMap:
  writes ShadowDepth

Lighting:
  reads Albedo / Normal / Material / Depth / ShadowDepth
  writes SceneColorHDR

LightVisualize:
  reads SceneColorHDR
  writes SceneColorHDR

Present:
  reads SceneColorHDR
  writes Swapchain
```

## Lighting Shader Boundary

`lighting.frag` 现在已经承担了：

- GBuffer unpack
- world position reconstruction
- Lambert / PBR direct lighting
- shadow map projection
- Hard / PCF / PCSS
- debug view

这在 05-01/05-02 还能接受，但 CSM / IBL / SSAO 继续塞进去会变得很难维护。下一步不急着拆 C++ pass，但 shader 侧应该先按功能拆 include。

推荐结构：

```text
shaders/common/fullscreen.glsl
shaders/common/brdf.glsl
shaders/common/gbuffer.glsl
shaders/common/shadow.glsl
shaders/common/csm.glsl
shaders/common/lighting_settings.glsl

shaders/lighting.frag
  #include common/gbuffer.glsl
  #include common/brdf.glsl
  #include common/shadow.glsl
  #include common/csm.glsl
```

如果当前 shader compiler 暂不支持 `#include`，可以先做轻量方案：

- 在 `lighting.frag` 内用 section 分区。
- CSM 完成后再引入 `ShaderPreprocessor` 或 CMake 预拼接。

不要为每个 lighting feature 新建一个 pass：

- PBR / Lambert 是 LightingPass 内的 lighting model。
- PCF / PCSS / CSM sampling 是 shadow evaluation。
- IBL 是 LightingPass 的 indirect lighting 输入。
- SSAO 是独立 pass 产出 AO texture，LightingPass 消费它。

## Light Visualization Rule

光源标记球不应该作为普通 lit geometry 进入 GBuffer，否则会出现：

- 被自己的光照影响。
- 被 shadow map 判定成半亮半暗。
- 参与投影，污染真实场景阴影。
- debug marker 和真实 renderable 的语义混在一起。

正确规则：

```text
Light marker = debug visual
  -> 不进入 GBuffer
  -> 不进入 ShadowMap caster list
  -> 不经过 LightingPass
  -> 由 LightVisualizePass 以 unlit/emissive 方式画到 SceneColorHDR
```

当前 `LightVisualizePass` 已经接近这个设计：它读取并写回 `SceneColorHDR`，使用独立 unlit shader。

后续可以补两个选项：

- `depthTest = false`：永远显示，适合 debug。
- `depthTest = true + read scene depth`：被场景遮挡，适合 editor gizmo。

但无论哪种，都不应该让 light marker 接受 shadow。

## Config-Driven Direction

下一步 CSM 前，配置文件应该开始接管 render technique / light / lighting settings，而不是把选项写死在 UI 或 C++ 默认值里。

建议 scene JSON 逐步演化为：

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
      "pcfSamples": 9
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
      "shadow": true
    },
    {
      "type": "point",
      "position": [1.5, 2.0, 1.0],
      "color": [1.0, 0.8, 0.6],
      "intensity": 5.0,
      "range": 8.0,
      "visualize": true
    }
  ]
}
```

短期落地方式：

- `Scene` 解析 light object。
- `Technique` 解析 renderer / lighting settings。
- UI 仍然可以覆盖运行时 settings，但默认值来自配置文件。
- nano / demo 场景必须把使用的光照技术写清楚。

## CSM Prep

05-02a CSM 不应该改变总体 pass 边界：

```text
ShadowMapPass / CSMShadowPass
  -> produces cascade shadow texture(s)

LightingPass
  -> chooses cascade by view depth
  -> samples shadow visibility
  -> combines with direct PBR lighting
```

CSM 需要新增或调整：

- Directional light 作为主 CSM 光源。
- Camera frustum split。
- 每个 cascade 的 light view/proj。
- shadow texture array 或多张 shadow texture。
- Lighting shader 的 cascade selection。
- Debug view：cascade index / shadow map layer / split distance。

不建议做：

- 不要把 CSM 做成新的 LightingPass。
- 不要让 point light 的临时 perspective shadow 继续混在 directional CSM 里。
- 不要让 light marker 进入 shadow caster list。

## Verification

已验证：

- `cmake --build build --config Debug` 通过。
- `LightingPass` 输出 `SceneColorHDR`。
- `PresentPass` 正常写 swapchain。
- RenderGraph 中存在 `Lighting -> Present` 链路。
- Light visual 不经过 LightingPass。

CSM 前需要再次确认：

- [ ] `lighting.frag` 的 BRDF / shadow / debug section 足够清晰。
- [ ] scene JSON 可以指定 directional light。
- [ ] renderer settings 可以指定 shadow mode。
- [ ] Light visual marker 不进入 GBuffer 和 ShadowMap。
- [ ] AppUI 仍能在 PresentPass 后正常叠加。
