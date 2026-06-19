# 05-01 PBR Direct Lighting

## What

把 Week 4 的 deferred lighting 从 Lambert/占位材质推进到第一版实时 PBR：

```text
GBufferPass
  -> Albedo:  base color
  -> Normal:  world normal
  -> Material: metallic / roughness / ao

LightingPass
  -> read GBuffer
  -> reconstruct world position from depth
  -> evaluate Lambert or GGX PBR
  -> apply shadow visibility
  -> write final lighting
```

本 nano 的目标不是完整 IBL PBR，而是先让 direct lighting 下的 metallic-roughness BRDF 成立，并让场景里可以用明确的材质参数验证效果。

## Why

ShadowMap 已经接入后，LightingPass 继续停留在 Lambert 会让材质验证失真：

- 金属度和粗糙度没有真实含义。
- 高光形状和能量分布不稳定。
- 后续 IBL、Bloom、Tone Mapping 都需要一个合理的 HDR/PBR 输入。

所以 05-01 先完成 direct PBR 的最小闭环：GBuffer 写入 material 参数，LightingPass 根据参数选择 Lambert 或 PBR。

## Scope

涉及文件：

- `shaders/gbuffer.frag`
- `shaders/triangle.vert`
- `shaders/lighting.frag`
- `src/pass/GBufferPass.cpp`
- `src/pass/LightingPass.h`
- `src/pass/LightingPass.cpp`
- `src/technique/DeferredShading.cpp`
- `src/scene/Scene.h`
- `src/scene/Scene.cpp`
- `src/rhi/Mesh.cpp`

功能：

- GBuffer push constant 增加 `baseColorFactor` 和 `materialParams`。
- `gbuffer.frag` 输出：
  - `Material.R = metallic`
  - `Material.G = roughness`
  - `Material.B = ao`
- `LightingPass` 读取 material attachment。
- `LightingSettings` 增加 lighting model 选择：
  - `Lambert`
  - `PBR`
- `lighting.frag` 实现 GGX direct BRDF：
  - GGX NDF
  - Schlick Fresnel
  - Smith Geometry
  - metallic-roughness diffuse/specular 混合
- Scene JSON 的 OBJ model 支持：
  - `baseColor`
  - `metallic`
  - `roughness`
  - `texture`
- OBJ loader 支持负索引 face，例如 Mitsuba 的 `f -1985/-2078/-1473 ...`。
- 新增本地 PBR 验证场景：
  - `assets/scenes/pbr-mitsuba-cerberus.json`
  - Mitsuba sphere + Cerberus + ground plane

## Design Notes

### PBR 是 LightingPass 的模型选项

PBR 不单独做成一个 pass。它和 Lambert 一样，都是 LightingPass 内部的 direct lighting model：

```cpp
enum LightingModel : int {
    LightingModel_Lambert = 0,
    LightingModel_PBR = 1,
};
```

这样后续 UI / Technique 只需要修改 `LightingSettings`，不需要切换整条渲染管线。

### Material attachment 承载最小 PBR 参数

当前 GBuffer 的 material attachment 先采用最小布局：

```text
R: metallic
G: roughness
B: ao
A: unused
```

这足够支撑 direct PBR、SSAO 乘算和后续 IBL。更复杂的材质参数暂时不进入本阶段，避免过早重构 Material 系统。

### OBJ 材质先走 scene override

OBJ/MTL 的材质格式不稳定，不适合作为 PBR 主路径。当前阶段允许 scene JSON 直接覆盖材质参数：

```json
{
  "path": "assets/models/Mitsuba/mitsuba-sphere.obj",
  "format": "obj",
  "texture": "assets/textures/white.png",
  "metallic": 0.0,
  "roughness": 0.35,
  "baseColor": [0.95, 0.88, 0.72, 1.0]
}
```

这让 PBR 验证关注 BRDF、GBuffer 和 LightingPass，而不是被 OBJ/MTL 贴图规则牵着跑。

### Mitsuba 需要负索引 OBJ 支持

`mitsuba-sphere.obj` 使用 OBJ 允许的负索引 face：

```text
f -1985/-2078/-1473 -1984/-2077/-1472 -1983/-2076/-1471
```

负索引表示“相对当前列表末尾的索引”。原 loader 使用 `std::stoul`，只适合正索引 OBJ，会导致 Mitsuba 几何解析错误。现在 loader 会把负索引解析成正确的 1-based positive index。

## Current Pipeline

```text
Application
  -> load scene json
  -> Scene creates model instances and material parameters

DeferredShading
  -> GBufferPass declares albedo / normal / material / depth
  -> ShadowMapPass writes shadow depth
  -> LightingPass reads GBuffer + shadow map

LightingPass
  -> push LightingSettings
  -> lighting.frag selects Lambert or PBR
```

运行 PBR 验证场景：

```powershell
.\build\Debug\KazuEngine.exe assets/scenes/pbr-mitsuba-cerberus.json
```

## Verification

- [x] `cmake --build build --config Debug` 通过。
- [x] Lighting Model UI 可以在 `Lambert` / `PBR` 之间切换。
- [x] Shadow Mode 可以关闭，便于单独检查 direct PBR。
- [x] Material attachment 被 LightingPass 读取。
- [x] Mitsuba sphere 使用负索引 OBJ 后可以正确加载。
- [x] PBR 验证场景不覆盖默认 `sample-scene.json`。
- [x] `assets/` 不进入 git 追踪，只作为本地验证资产。

## 05-01 Update: PBR Texture Maps

当前 PBR 材质已从“albedo 贴图 + metallic/roughness 标量”扩展为基础 PBR texture set：

```text
binding 0: albedo/baseColor      sRGB
binding 1: normal                linear
binding 2: metallicRoughness     linear, glTF convention: G=roughness, B=metallic
binding 3: AO                    linear, R=ao
```

Scene JSON 对 OBJ 支持：

```json
{
  "albedoTexture": "albedo.png",
  "normalTexture": "normal.png",
  "metallicRoughnessTexture": "metallicRoughness.png",
  "aoTexture": "ao.png",
  "flipV": true
}
```

兼容旧字段：

```json
{
  "texture": "albedo.png"
}
```

glTF loader 会自动读取：

- `pbrMetallicRoughness.baseColorTexture`
- `normalTexture`
- `pbrMetallicRoughness.metallicRoughnessTexture`
- `occlusionTexture`

`flipV` 规则：

- OBJ 默认 `true`
- glTF 默认 `false`

验证场景：

```powershell
.\build\Debug\KazuEngine.exe assets\scenes\pbr-damaged-helmet.json
```

DamagedHelmet 来自 Khronos glTF Sample Models。该模型适合本地验证 PBR texture pipeline；原资产为 CC BY-NC，不提交到 git。

当前没有真正 IBL，因此高 metallic 材质仍然只是 direct-PBR 近似。`lighting.frag` 里有一个临时 ambient specular approximation，用来避免金属在没有环境反射时完全变黑；05-03 IBL 接入后应替换掉它。

## Follow-up

- 05-02a：把当前单张 shadow map 升级为 CSM。
- 05-03：接入 IBL，补齐 indirect diffuse/specular。
- 05-05 / 05-06：LightingPass 输出 HDR SceneColor，PresentPass 做 Bloom / Tone Mapping / Gamma。
- 05-07：引入 TAA，处理 shadow/specular/post-process 的时域稳定性。
