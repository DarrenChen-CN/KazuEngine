# Nano-Feature 1: 脏代码 MVP —— Vulkan 初始化 + 第一个三角形

> 目标：不封装任何类，直接调用 Vulkan API，在窗口中渲染一个彩色三角形。
> 时间预估：阅读 30 分钟 + 实现 2-3 小时。

---

## 1. 这个 Nano-Feature 要学什么？

这不是"写一个三角形"这么简单。这个 Nano-Feature 的目标是让**整个 Vulkan 初始化到呈现的链路跑通一次**，建立完整的心智模型。

你将亲手创建以下对象（按依赖顺序）：

```
GLFW Window
    ↓
VkInstance（Vulkan 入口）
    ↓
VkDebugUtilsMessengerEXT（Validation Layer 回调）
    ↓
VkPhysicalDevice（选择 GPU）
    ↓
VkDevice（逻辑设备）+ VkQueue（图形队列）
    ↓
VkSurfaceKHR（窗口表面）
    ↓
VkSwapchainKHR（交换链）
    ↓
VkRenderPass（渲染流程定义）
    ↓
VkFramebuffer（RenderPass + ImageView）
    ↓
VkShaderModule（SPIR-V 二进制）
    ↓
VkPipelineLayout（资源布局）
    ↓
VkPipeline（预编译的渲染状态）
    ↓
VkCommandPool / VkCommandBuffer（命令录制）
    ↓
VkFence / VkSemaphore（同步）
    ↓
【窗口中出现三角形】
```

**核心学习点**：
1. **Instance → Device → Queue** 的层级关系
2. **Swapchain 的工作机制**（Acquire → Render → Present）
3. **Graphics Pipeline 的完全预编译特性**（所有状态打包到一个对象）
4. **Command Buffer 的录制-提交模型**
5. **Fence（CPU-GPU）和 Semaphore（GPU-GPU）的同步**

---

## 2. 代码结构预览

由于是"脏代码"，所有逻辑放在 `main.cpp` 中，按功能分块：

```cpp
// ===== 1. 窗口 =====
GLFWwindow* window = glfwCreateWindow(800, 600, "KazuEngine", nullptr, nullptr);

// ===== 2. Vulkan 初始化 =====
VkInstance instance = createInstance();
VkDebugUtilsMessengerEXT debugMessenger = setupDebugMessenger(instance);
VkPhysicalDevice physicalDevice = pickPhysicalDevice(instance);
VkDevice device = createLogicalDevice(physicalDevice);
VkQueue graphicsQueue = getGraphicsQueue(device);

// ===== 3. 窗口呈现 =====
VkSurfaceKHR surface = createSurface(instance, window);
VkSwapchainKHR swapchain = createSwapchain(physicalDevice, device, surface);
std::vector<VkImageView> swapchainImageViews = createImageViews(device, swapchain);

// ===== 4. 渲染管线 =====
VkRenderPass renderPass = createRenderPass(device, swapchainImageFormat);
VkPipelineLayout pipelineLayout = createPipelineLayout(device);
VkPipeline graphicsPipeline = createGraphicsPipeline(device, renderPass, pipelineLayout);
std::vector<VkFramebuffer> framebuffers = createFramebuffers(device, renderPass, swapchainImageViews);

// ===== 5. 命令与同步 =====
VkCommandPool commandPool = createCommandPool(device);
VkCommandBuffer commandBuffer = allocateCommandBuffer(device, commandPool);
VkSemaphore imageAvailableSemaphore = createSemaphore(device);
VkSemaphore renderFinishedSemaphore = createSemaphore(device);
VkFence inFlightFence = createFence(device);

// ===== 6. 主循环 =====
while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    drawFrame(device, swapchain, graphicsQueue, commandBuffer,
              imageAvailableSemaphore, renderFinishedSemaphore, inFlightFence);
}

// ===== 7. 清理 =====
// vkDestroy* 所有对象（顺序：与创建相反）
```

**代码量预估**：约 600-800 行（main.cpp 一个文件），这是整个项目中唯一会超过 300 行限制的 Nano-Feature。原因：脏代码阶段故意不拆分，让你看到"如果不封装，会多冗长"。

---

## 3. 核心概念详解（精简版）

### 3.1 Validation Layer：你的救生圈

```cpp
const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};
```

**作用**：Vulkan 驱动不检查你的错误（为了性能）。Validation Layer 是一个可选的拦截层，会在你做错时输出详细错误。

**常见捕获的错误**：
- 忘记在 RenderPass 开始时 Clear Attachment
- Image Layout 转换错误（如从 `UNDEFINED` 直接读）
- Memory Barrier 缺失（数据竞争）
- 对象销毁顺序错误

**⚠️ 必须开启**：开发阶段绝不关闭，否则调试时间 ×10。

**与 CUDA 对比**：CUDA 的 `cuda-memcheck` 是离线工具，Validation Layer 是实时的、每帧检查的。

### 3.2 PhysicalDevice 选择逻辑

```cpp
// 评分系统：独立显卡加分，不支持必要扩展淘汰
int score = 0;
if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 1000;
if (!supportsRequiredExtensions(device)) score = -1;
if (!queueFamilies.graphicsFamily.has_value()) score = -1;
```

**RTX 4060 的特性**：
- `deviceType = DISCRETE_GPU`（独立显卡）
- 支持 `VK_KHR_swapchain`（必须）
- 支持 `VK_KHR_ray_query`（可选，后续可用）
- `VkPhysicalDeviceLimits` 中的 `maxImageDimension2D` 通常 ≥ 16384

### 3.3 Swapchain：双缓冲的显式版

```cpp
// 获取 Swapchain Image（Acquire）
uint32_t imageIndex;
vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

// 渲染到 framebuffer[imageIndex]
// ...

// 呈现（Present）
vkQueuePresentKHR(presentQueue, &presentInfo);
```

**关键参数**：
- `minImageCount`：通常设为 `capabilities.minImageCount + 1`（双缓冲）或 +2（三缓冲）
- `presentMode`：
  - `FIFO_KHR` = VSync（固定刷新率，最省电）
  - `MAILBOX_KHR` = 单缓冲 + 替换（低延迟，推荐游戏）
  - `IMMEDIATE_KHR` = 立即呈现（可能撕裂）
- `imageFormat`：通常 `B8G8R8A8_UNORM`（Windows 标准）

**与 OpenGL 对比**：OpenGL 的 `glfwSwapBuffers` 帮你隐藏了这些细节。Vulkan 要求你显式管理 Swapchain Image 的生命周期。

### 3.4 Graphics Pipeline：状态机的终极预编译

这是 Vulkan 与 CUDA 最不同的地方。CUDA 没有 Graphics Pipeline 的概念（只有 Compute Kernel），OpenGL 是运行时状态机。

Vulkan 要求你在创建 Pipeline 时就确定**所有状态**：

```cpp
// Shader Stage（顶点/片元着色器）
// Vertex Input（顶点格式）
// Input Assembly（图元类型：Triangle List/Strip）
// Viewport & Scissor
// Rasterizer（填充模式、Cull 面、Front Face 方向）
// Multisampling
// Depth/Stencil
// Color Blending（是否混合、混合公式）
// Dynamic State（哪些状态可以在录制 Command Buffer 时修改）
```

**为什么这样设计？**
- GPU 驱动可以在创建 Pipeline 时做全部优化（Shader 编译、状态排序）
- 运行时切换 Pipeline 的开销是明确且可预测的
- **代价**：切换渲染状态 = 切换 Pipeline，不能像在 OpenGL 中那样随时改一个 `glEnable`

**我们的三角形 Pipeline**：
- Shader：最简单的 Vertex/Fragment（输出顶点位置 + 顶点颜色）
- Vertex Input：无（顶点硬编码在 Shader 中，简化代码）
- Rasterizer：`FILL` 模式，`BACK` Cull
- Color Blending：禁用（直接覆盖）

### 3.5 同步：一帧的完整时序

```
CPU 侧                          GPU 侧
─────────────────────────────────────────────────
Frame N 开始
│
├─ vkWaitForFences(inFlightFence)  ← 等待上一帧 GPU 完成
│
├─ vkAcquireNextImageKHR()         →  GPU: 获取 Swapchain Image
│      │                            (发出 imageAvailableSemaphore)
│      │
│      └─ imageAvailableSemaphore ──┐
│                                   ▼
├─ 录制 Command Buffer              GPU 开始执行 RenderPass
│   (BindPipeline → Draw)           (等待 Semaphore)
│                                   │
├─ vkQueueSubmit()                 →  GPU 执行绘制
│      │                            (发出 renderFinishedSemaphore)
│      │                            │
│      └─ renderFinishedSemaphore ──┼──┐
│                                   ▼  ▼
├─ vkQueuePresentKHR()             →  GPU 呈现到屏幕
│                                   (等待 Semaphore)
│
├─ vkResetFences(inFlightFence)     ← 重置 Fence，准备下一帧
│
Frame N+1 开始
```

**三者的区别**：
| 同步原语 | 方向 | 用途 |
|---------|------|------|
| **Fence** | GPU → CPU | CPU 等待 GPU 完成一帧（防止资源冲突） |
| **Semaphore** | GPU → GPU | 控制 GPU 命令的执行顺序（Acquire → Render → Present） |
| **Barrier** | GPU 内部 | 同一 Command Buffer 内的资源状态转换 |

**与 CUDA 对比**：CUDA 的 `cudaDeviceSynchronize` 类似于 Fence，但没有 Semaphore 的概念（Stream 间的依赖用 `cudaStreamWaitEvent`）。

---

## 4. Shader 设计：最简单的彩色三角形

### triangle.vert

```glsl
#version 450

// 硬编码的三角形顶点（NDC 空间，无需 MVP 矩阵）
vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);

vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0)
);

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragColor = colors[gl_VertexIndex];
}
```

**关键点**：
- 使用 `gl_VertexIndex`（内置变量）索引硬编码数组，无需 Vertex Buffer
- `gl_Position` 输出裁剪空间坐标（NDC：x/y/z 都在 [-1, 1]）

### triangle.frag

```glsl
#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragColor, 1.0);
}
```

### 编译为 SPIR-V

```bash
# 使用 Vulkan SDK 自带的 glslangValidator
%VULKAN_SDK%\Bin\glslangValidator.exe -V triangle.vert -o triangle.vert.spv
%VULKAN_SDK%\Bin\glslangValidator.exe -V triangle.frag -o triangle.frag.spv
```

CMake 中可以用 `add_custom_command` 自动编译 Shader。

---

## 5. 常见陷阱（脏代码阶段特别容易踩）

### 陷阱 1：Validation Layer 未开启但出现奇怪崩溃

**现象**：程序随机崩溃，没有错误信息。
**原因**：Vulkan 驱动在 Release 模式下不做任何检查，内存越界、use-after-free 直接崩溃。
**解决**：确保 Validation Layer 正确加载，且 Debug Messenger 回调函数正确注册。

### 陷阱 2：Swapchain Image 获取失败

**现象**：`vkAcquireNextImageKHR` 返回 `VK_ERROR_OUT_OF_DATE_KHR`。
**原因**：窗口 Resize 后，Swapchain 的 Image 尺寸与窗口不匹配。
**解决**：捕获这个错误，重建 Swapchain（脏代码阶段可以先不支持 Resize，直接 assert）。

### 陷阱 3：Pipeline 创建失败

**现象**：`vkCreateGraphicsPipelines` 返回 `VK_ERROR_INVALID_SHADER_NV`。
**原因**：SPIR-V 文件未正确加载（路径错误、文件未生成）。
**解决**：检查 SPIR-V 文件是否存在，大小是否合理（> 100 bytes）。

### 陷阱 4：Fence 未重置导致死锁

**现象**：程序卡住，CPU 100% 占用（在 `vkWaitForFences` 处）。
**原因**：上一帧的 Fence 在 `vkQueueSubmit` 时被触发，但下一帧开始前没有 `vkResetFences`。
**解决**：确保每帧的顺序是：`vkWaitForFences` → `vkQueueSubmit` → `vkQueuePresentKHR` → `vkResetFences`。

### 陷阱 5：销毁顺序错误

**现象**：程序退出时崩溃。
**原因**：先销毁了 Device，再销毁依赖 Device 的对象（如 Pipeline）。
**解决**：销毁顺序与创建顺序**严格相反**：
```
创建：Instance → Device → Pipeline → CommandBuffer
销毁：CommandBuffer → Pipeline → Device → Instance
```

---

## 6. 验证方法

完成代码后，按以下步骤验证：

1. **编译**：`cmake --build build`
2. **运行**：`./build/KazuEngine.exe`
3. **视觉验证**：窗口中出现一个彩色三角形（红绿蓝顶点）
4. **Validation Layer 验证**：控制台无报错信息
5. **关闭验证**：窗口可以正常关闭，无崩溃

**如果 Validation Layer 报错**：
- 仔细阅读错误信息，通常会指出具体 API 调用和原因
- 常见格式：`Validation Error: [ VUID-vkCmdDraw-... ] Object 0: handle = ...`

---

## 7. 设计决策说明

### 为什么顶点硬编码在 Shader 中？

正常做法是用 Vertex Buffer 存储顶点数据。但脏代码阶段的目标是"最小链路跑通"，硬编码顶点可以省略：
- Buffer 创建
- 内存分配
- 数据上传
- Vertex Input State 配置

后续封装 Core Layer 时，会替换为 Vertex Buffer 方案。

### 为什么不支持窗口 Resize？

脏代码阶段先忽略 Resize，因为 Swapchain 重建涉及：
- 等待 GPU 空闲
- 销毁旧 Swapchain / Framebuffer / ImageView
- 创建新 Swapchain / Framebuffer / ImageView
- 更新 Viewport

这会显著增加代码量。Week 2 封装 Swapchain 类时会完整支持 Resize。

### 为什么不用 VMA？

脏代码阶段手动分配内存，目的是让你理解：
- `vkAllocateMemory` 的内存类型选择
- `vkBindBufferMemory` / `vkBindImageMemory` 的绑定机制

Week 2 封装时会集成 VMA，之后不再手动管理内存。

---

## 8. 与 CUDA 的关键映射

| Vulkan 概念 | CUDA 对应 | 核心差异 |
|------------|-----------|---------|
| `VkInstance` | `cuInit` + `cuDeviceGetCount` | Vulkan 多了 Extensions 和 Layers |
| `VkPhysicalDevice` | `cudaGetDeviceProperties` | Vulkan 要求显式查询每个能力 |
| `VkDevice` | `cuCtxCreate` | Vulkan 的 Context 更轻量 |
| `VkQueue` | CUDA Stream | Vulkan Queue 与硬件队列绑定 |
| `VkCommandBuffer` | Kernel Launch 序列 | 显式录制 vs 隐式提交 |
| `VkPipeline` | 无 | CUDA 没有 Graphics Pipeline |
| `vkCmdDraw` | 无 | CUDA 没有光栅化 |
| `VkFence` | `cudaEventSynchronize` | CPU 等待 GPU |
| `VkSemaphore` | `cudaStreamWaitEvent` | GPU 等待 GPU |

**你的优势**：你已经深刻理解 GPU 执行模型（异步、并行、内存层次），这些知识可以直接迁移。你需要补的只是 Vulkan 的显式 API 调用。

---

## 9. 参考资源

- **Vulkan Tutorial 第 1-15 章**：https://vulkan-tutorial.com/
  - 注意：本 Nano-Feature 的目标与 Vulkan Tutorial 的 "Drawing a triangle" 章节相同，但我们会更快跳过细节（如 Querying details of swap chain support），聚焦于核心链路。
- **SaschaWillems Triangle 示例**：https://github.com/SaschaWillems/Vulkan/tree/master/examples/triangle
  - 更紧凑的代码，适合参考结构。

---

## 10. 下一步

1. 阅读本文档，确认理解每个步骤
2. 提出你的设计思路（或确认直接按本文档实现）
3. 我开始写代码（CMake + main.cpp + shaders）
4. 你 Review 代码，确认理解每一行
5. 编译运行，验证彩色三角形
