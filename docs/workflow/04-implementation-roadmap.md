# KazuEngine 实现路径与求职规划

> 本文档结合秋招求职目标、现有 CUDA Path Tracer 项目、以及 2-3 个月时间线，制定最优实现策略。

---

## 1. 现有能力分析（CUDA Path Tracer）

你的 CUDA 项目已经覆盖了以下技术：

| 领域 | 已实现 | 简历价值 |
|------|--------|---------|
| **路径追踪** | Wavefront PT + Stream Compaction | ⭐⭐⭐⭐⭐ |
| **加速结构** | SAH BVH（CPU 构建 + GPU 遍历） | ⭐⭐⭐⭐⭐ |
| **材质系统** | PBR（GGX + Schlick + Smith）+ Diffuse + Dielectric + SSS | ⭐⭐⭐⭐⭐ |
| **光照采样** | NEE + MIS（Power Heuristic）+ HDR 2D-CDF | ⭐⭐⭐⭐⭐ |
| **降噪** | SVGF（Temporal + Variance + A-Trous） | ⭐⭐⭐⭐⭐ |
| **GBuffer** | OpenGL MRT（Position/Normal/Barycentric/MotionVector） | ⭐⭐⭐⭐ |
| **后处理** | Gamma 映射 | ⭐⭐ |
| **UI** | ImGui + GLFW | ⭐⭐⭐ |

**总结**：你已经有**完整的离线渲染器**，这在秋招中是非常硬核的项目。但缺少：
- ❌ 现代图形 API（Vulkan/DirectX）经验
- ❌ 实时渲染管线（60fps 设计哲学）
- ❌ 现代实时 GI（DDGI、SSR 等）
- ❌ 渲染器架构设计（RenderGraph 等）

**Vulkan 项目的核心目标**：补足上述短板，与 CUDA 项目形成"双轨互补"。

---

## 2. 秋招简历策略：双轨渲染能力

### 2.1 简历写法建议

不要写两个独立的项目，而是突出**"你对渲染管线的完整理解"**：

**主项目：CUDA Path Tracer（离线渲染）**
```
• 基于 CUDA 实现 Wavefront Path Tracing，支持 Stream Compaction 动态剔除无效光线
• 自研 SAH BVH 加速结构，CPU 构建 + GPU 遍历，支持最近子节点优先优化
• 实现 PBR 材质系统（GGX NDF + Schlick Fresnel + Smith GGX），支持 Specular+Diffuse 混合采样
• 实现 NEE + MIS（Power Heuristic），支持 HDR 环境光 2D-CDF 重要性采样
• 实现 SVGF 降噪管线：Temporal Accumulation + Variance Estimation + A-Trous Wavelet
• 实现 Burley Diffusion Profile 次表面散射，支持双向 Probe 出口点探测
```

**次项目：KazuEngine（实时渲染）**
```
• 基于 Vulkan 的实时 PBR 渲染器，采用 RenderGraph 声明式架构管理多 Pass 渲染流程
• 实现 Deferred Shading 管线：GBuffer + Lighting + Post-processing
• 实现 Hi-Z SSR 屏幕空间反射，基于 Depth Pyramid 层次追踪，支持 Roughness 自适应步长
• 实现 DDGI（Dynamic Diffuse Global Illumination）实时全局光照：Probe 放置 + SH 编码 + 滚动更新
• 集成 Tracy GPU Profiler + ImGui 调试面板 + Shader 热重载，支持 Pass 级性能分析
• [可选] 实现 ReSTIR DI：Reservoir 采样 + 时空复用，提升多光源场景渲染效率
```

### 2.2 面试话术准备

面试官看到这两个项目后，最可能问的问题：

**Q: 为什么同时做离线 PT 和实时渲染器？**
> A: 离线渲染让我深入理解了光线追踪的物理正确性（BRDF、MIS、Path Integral），
> 实时渲染让我学会了在 16ms 约束下做近似和优化（Screen-Space Techniques、Probe GI、Temporal Stability）。
> 两者的结合让我对渲染有完整的认知：什么时候该追求物理正确，什么时候该做聪明的近似。

**Q: RenderGraph 解决了什么问题？**
> A: 传统手写的渲染管线中，Resource Barrier 和 Image Layout 转换非常容易出错。
> RenderGraph 通过声明式 API 让开发者描述"读什么、写什么"，框架自动推导最优的 Barrier 插入点和内存复用策略。
> 参考了 Frostbite FrameGraph 和 Doom Eternal RenderGraph 的设计。

**Q: CUDA PT 和 Vulkan 渲染器有什么技术关联？**
> A: 算法层面直接迁移——PBR BRDF 公式、重要性采样策略、GBuffer 结构、Temporal Accumulation 逻辑。
> 但架构层面完全不同：CUDA 是 SIMT 计算模型，Vulkan 是显式 Command Buffer + 状态预编译 Pipeline。
> 最大的学习点是同步：CUDA 的同步相对隐式，Vulkan 要求你精确控制 Fence/Semaphore/Barrier。

---

## 3. 技术优先级（求职导向）

### Tier 1: 必须实现（简历核心亮点）

| 技术 | 优先级 | 理由 |
|------|--------|------|
| **RenderGraph 架构** | P0 | 工业界热点（Frostbite/UE5/Doom），体现架构能力 |
| **Deferred Shading + PBR** | P0 | 实时渲染基础，证明掌握现代管线 |
| **Hi-Z SSR** | P0 | 屏幕空间技术经典组合，体现性能意识 |
| **DDGI** | P0 | 当前工业界主流 GI（UE5 Lumen 的 fallback） |

### Tier 2: 强烈推荐（显著加分）

| 技术 | 优先级 | 理由 |
|------|--------|------|
| **ReSTIR DI** | P1 | NVIDIA 主推，工业界正在采用，技术深度高 |
| **Shadow Mapping（PCF/CSM）** | P1 | 基础但重要，DDGI 需要 Shadow 作为对比 |
| **IBL（Diffuse + Specular）** | P1 | PBR 完整性的必要部分 |
| **Shader 热重载 + ImGui** | P1 | 工程化能力，面试加分项 |
| **Tracy GPU Profiler** | P1 | 性能分析是工业界必备技能 |

### Tier 3: 有时间再做（补充广度）

| 技术 | 优先级 | 理由 |
|------|--------|------|
| **SSAO/GTAO** | P2 | 技术深度一般，DDGI 可以替代 AO |
| **SVOGI / LPV** | P2 | 体素 GI，第三个月补充 |
| **ReSTIR GI** | P2 | 比 ReSTIR DI 复杂很多 |
| **HSGI 整合** | P2 | 长期目标，可作为"未来工作" |
| **Volumetric / Cloud** | P3 | 与当前目标无关 |

### 为什么不优先做 SSAO/GTAO？

1. 你的 CUDA PT 已经有完整的全局光照（虽然离线），SSAO 这种"近似 AO"技术深度不够
2. DDGI 可以直接提供 Diffuse GI + AO 效果
3. 面试时间有限，面试官更可能问 DDGI 而不是 SSAO

---

## 4. 时间线规划（9 周 / 2 个月）

> 假设每周可投入 10-15 小时（每天 1.5-2 小时）。
> 你的算法基础很好，所以算法实现相对快，重点在 Vulkan API + 架构搭建。

### Month 1: 基础框架 + 基础渲染

#### Week 1: 脏代码 MVP —— "先让窗口亮起来"

**目标**：不封装、直接调 API，渲染一个带贴图的旋转立方体

**Nano-Features**:
1. Instance + Validation Layer + PhysicalDevice + Device
2. Surface + Swapchain + RenderPass + Framebuffer
3. Graphics Pipeline（Vertex/Fragment Shader）
4. Vertex Buffer + Index Buffer + Uniform Buffer
5. Texture Loading + Sampler
6. Depth Buffer
7. 简单 Camera + 旋转动画

**交付标准**：
- 窗口中有一个带 Diffuse 贴图的立方体在旋转
- 一个方向光源 + Diffuse Lambert 着色
- 无 Validation Layer 报错

**为什么先做脏代码？**
- 验证工具链（Vulkan SDK / GLFW / CMake / 编译器）
- 快速建立 Vulkan API 的心智模型
- 有个能跑的东西，保持动力
- 为后续的"干净架构重写"提供对比参考

#### Week 2: Core Layer 封装 —— "给脏代码洗澡"

**目标**：把 Week 1 的脏代码按设计文档的 Core Layer 规范重写

**Nano-Features**:
1. `Context`：Instance + PhysicalDevice + Device + Queues + Validation Layer
2. `Swapchain`：支持 Resize、多缓冲、Present Mode 选择
3. `Buffer`：RAII + VMA + upload/flush/mapped
4. `Image` + `ImageView`：RAII + VMA + Layout 转换
5. `CommandPool` + `CommandBuffer`：录制辅助函数
6. `SyncObjects`：Fence + Semaphore 管理

**交付标准**：
- Week 1 的旋转立方体用封装后的 Core Layer 实现
- 代码量减少 30%（封装带来的简洁性）
- 窗口 Resize 时正确重建 Swapchain

#### Week 3: RHI + 基础渲染 —— "能加载模型了"

**目标**：资源管理 + Pipeline 封装 + 模型加载

**Nano-Features**:
1. `DescriptorSetLayoutCache`：基于哈希的 Layout 缓存
2. `PipelineCache` / `PipelineBuilder`：简化 Graphics Pipeline 创建
3. `ShaderLibrary`：SPIR-V 加载 + `shaderc` 运行时编译
4. `Mesh` / `Model`：加载 GLTF（fastgltf）或 OBJ
5. `Camera`：轨道相机 + Projection/View 矩阵
6. `Transform`：TRS 矩阵

**交付标准**：
- 加载一个 GLTF 模型（如 Damaged Helmet）
- 可以鼠标旋转、缩放查看模型
- Diffuse 贴图正常显示

#### Week 4: RenderGraph + Deferred Shading —— "架构的第一次飞跃"

**目标**：实现 RenderGraph + GBuffer + Lighting

**Nano-Features**:
1. RenderGraph 核心：`addPass` / `compile` / `execute`
2. Resource 系统：Transient Resource + 内存复用
3. GBuffer Pass：Albedo / Normal / Depth / Material / Velocity
4. Lighting Pass：Directional Light + Point Light（Blinn-Phong 先跑通）
5. ImGui 集成：参数面板 + 帧率显示

**交付标准**：
- 渲染一个场景，GBuffer 每个 Attachment 都可用 ImGui 查看
- Lighting Pass 输出正确
- 自动 Barrier 无 Validation Layer 报错

### Month 2: GI 技术 + 完善

#### Week 5: PBR + Shadow + IBL —— "光照管线的完整性"

**目标**：替换 Blinn-Phong 为 PBR + 添加 Shadow + IBL

**Nano-Features**:
1. PBR Material：Metallic-Roughness 工作流（利用 CUDA 项目的 BRDF 知识）
2. Image-Based Lighting：
   - Diffuse：Irradiance Map（预计算或实时卷积）
   - Specular：Prefiltered Environment Map + BRDF LUT
3. Shadow Mapping：Simple Shadow Map + PCF
4. Tone Mapping：Reinhard 或 ACES
5. Gamma 校正

**交付标准**：
- 模型有金属感（Metallic = 1 时像镜子）
- 有环境反射（IBL Specular）
- 有阴影（Shadow Map）
- 整体色调正确（Tone Mapping + Gamma）

**为什么 Week 5 才做 PBR？**
因为 RenderGraph 的架构比 PBR 更重要。先跑通 Deferred 管线，再替换材质逻辑。

#### Week 6: SSR + Hi-Z —— "屏幕空间技术的核心"

**目标**：实现 SSR + Hi-Z Buffer

**Nano-Features**:
1. Depth Pyramid（Hi-Z）：Compute Shader 生成 Mip Chain
2. SSR Ray Marching：基础屏幕空间反射
3. Hi-Z Tracing：利用 Depth Pyramid 做层次步进
4. Roughness 自适应：Roughness 越高，采样越分散
5. Fallback：无命中时回退到 Environment Map

**交付标准**：
- 光滑地面能反射场景
- ImGui 可以开关 SSR、调节 Roughness
- Hi-Z 的每个 Mip Level 可以用 ImGui 查看

#### Week 7-8: DDGI —— "简历上的王炸"

**目标**：实现 DDGI（简化版）

**Nano-Features**:
1. Probe 网格放置：规则 3D 网格，基于 Camera 的局部网格
2. Probe Ray Tracing：每个 Probe 发射 N 条光线（可用 Rasterization 或 Compute）
3. SH 编码：L1 Spherical Harmonics（4 个系数）
4. Probe 更新：Rolling Update（每帧更新部分 Probe）
5. Probe 可视化：ImGui 显示 Probe 位置和颜色
6. Shading：Scene 中采样最近 Probe 的 SH 进行光照计算

**交付标准**：
- 场景有间接光（角落变暗、颜色渗透）
- Probe 位置可视化
- 可以开关 DDGI，对比直接光 vs 全光照
- 30fps+（Probe 数量控制在合理范围）

**DDGI 简化策略**：
- 先用规则网格（不做自适应放置）
- L1 SH 足够（4 系数 vs 9 系数，性能更好）
- Probe 光线用 Rasterization 代替 Ray Tracing（不需要 VK_KHR_ray_query）
- 不做 Probe Visibility（backface culling），先跑通基础版本

#### Week 9: ReSTIR DI + 完善 —— "如果有余力"

**目标**：如果时间够，实现 ReSTIR DI；否则用于完善和调试

**Nano-Features**:
1. Reservoir 数据结构（GPU Buffer 存储）
2. WIS（Weighted Importance Sampling）
3. Initial Sampling：光源采样 + BRDF 采样
4. Temporal Reuse：复用上一帧的 Reservoir
5. [可选] Spatial Reuse：邻近像素复用

**交付标准**：
- 多光源场景（100+ 点光源）渲染效率显著提升
- 可以开关 ReSTIR，对比帧时间和质量

**如果没时间**：Week 9 用于：
- 修复 Bug
- 优化性能（减少 Barrier、优化 DescriptorSet 更新）
- 完善 ImGui 调试面板
- 截图/录视频用于简历

### Month 3: 补充与完善（非求职刚需）

#### Week 10+: 选做内容

| 内容 | 优先级 | 说明 |
|------|--------|------|
| **ReSTIR GI** | 高 | 如果 Week 9 没做，优先补这个 |
| **HSGI 整合** | 中 | 长期目标，整合 Hi-Z + DDGI |
| **SSAO/GTAO** | 低 | DDGI 已提供 AO 效果 |
| **SVOGI / LPV** | 低 | 体素 GI，补充广度 |
| **性能优化** | 高 | Barrier 优化、Command Buffer 复用、内存优化 |
| **文档整理** | 高 | 项目 README、技术博客 |

---

## 5. 每周的节奏建议

| 时间 | 活动 |
|------|------|
| **周一~周二** | AI 提供知识文档（你阅读 + 思考） |
| **周三** | 你确认理解，提出设计建议 |
| **周四~周五** | AI 实现代码（Nano-Feature） |
| **周六** | 你 Review 代码 |
| **周日** | 修正 + 验证 + 更新 Session Log |

**知识先行的时间占比**：约 30%（周一~周二）
**代码实现的时间占比**：约 50%（周四~周五 + 周日修正）
**Review + 验证的时间占比**：约 20%（周六~周日）

---

## 6. 风险与应对

| 风险 | 概率 | 应对策略 |
|------|------|---------|
| **RenderGraph 实现困难** | 中 | 先简化：不做 transient resource 内存复用，只做 Pass 管理和自动 Barrier |
| **DDGI 性能不达标** | 中 | 减少 Probe 数量（8x8x4 = 256）、减少 Ray 数量（64→32）、用 L1 SH |
| **ReSTIR 来不及** | 高 | 作为 P1 而非 P0，简历中可以写"正在进行" |
| **Validation Layer 报错难以解决** | 中 | 每实现一个 Nano-Feature 就验证，不堆积问题 |
| **秋招时间提前** | 低 | 优先保证 Tier 1 完成，Tier 2 选做 |

---

## 7. 立即行动项

1. ✅ 阅读 `docs/knowledge/00-vulkan-pipeline-overview.md`（本文档的配套知识）
2. ⬜ 确认开发环境：Vulkan SDK 版本、编译器（MSVC/Clang）、CMake 版本
3. ⬜ 创建第一个脏代码 Nano-Feature：Instance + Device + 三角形
4. ⬜ 更新 `docs/workflow/03-session-log.md` 记录进度

---

## 8. 变更记录

| 日期 | 变更内容 |
|------|---------|
| 2026-04-29 | 初始版本：基于 CUDA 项目分析 + 求职导向规划 |
