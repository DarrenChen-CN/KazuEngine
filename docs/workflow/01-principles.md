# KazuEngine 开发哲学与工作流约定

> 本文档记录 KazuEngine 项目的开发原则、工作流约定和协作规范。
> 任何变更都需要经过讨论并更新此文档。

---

## 1. 核心哲学：理解优先于速度

**VibeCoding 不是"让 AI 写你不懂的代码"，而是"让 AI 帮你把理解转化为代码"。**

Vulkan 是一个 API 密集且调试困难的图形 API。如果某段代码的原理你没有理解：
- 你将在 Validation Layer 报错时束手无策
- 你将在 `VK_ERROR_DEVICE_LOST` 时无法定位问题
- 你将无法对这个引擎进行任何有意义的扩展

因此：**所有代码必须经过作者的 Review 和理解，才能合并到主干。**

---

## 2. 迭代粒度：Nano-Feature

借鉴设计方案中的 Phase 划分，但进一步细化到"半天到一天能 Review 完"的粒度。

| 层级 | 时间规模 | 内容示例 |
|------|---------|---------|
| **Phase** | 2-4 周 | Phase 1: 基础框架 |
| **Milestone** | 3-7 天 | Swapchain 完整封装 + 窗口 Resize |
| **Nano-Feature** | 0.5-1 天 | `class Buffer` 的 RAII 封装 + upload/flush |

### 2.1 单次对话的交付上限

每次对话只完成 **1 个 Nano-Feature**，最多附带 **1 个直接依赖它的 Nano-Feature**。

**硬约束：**
- 新增/修改的代码行数不超过 **300 行**（头文件 + 实现 + 示例）
- 涉及的文件数不超过 **5 个**
- 如果一个功能超过这个规模，必须拆分成多个 Nano-Feature

### 2.2 为什么要这么细？

1. **Review 负担**：300 行代码你可以在 15-30 分钟内理解并 Review
2. **调试范围**：出错时，你只需检查这 300 行，而不是 3000 行
3. **上下文压缩**：AI 的上下文窗口有限，小粒度意味着每次对话都有充足的上下文空间
4. **成就感**：每天都能看到一个可运行的进展，而不是一周后才能编译

---

## 3. 交付标准：文档 + 代码 + 验证

每个 Nano-Feature 交付时，必须同时提供以下四部分内容：

### 3.1 What（这是什么）

50 字以内描述这个功能是什么。

```markdown
**What**: 实现 Buffer 的 RAII 封装，支持 GPU-only 和 CPU-visible 两种类型，
集成 VMA 进行内存分配。
```

### 3.2 Why（为什么这样设计）

100 字以内解释设计决策的动机（Vulkan 约束、性能考量、可扩展性等）。

```markdown
**Why**: Vulkan 的内存管理复杂（内存类型、对齐要求、映射规则）。
使用 VMA 可以自动化这些细节，同时我们保留 `handle()` 方法暴露原始 VkBuffer，
不隐藏底层细节以便后续高级操作（如 Device Address）。
```

### 3.3 How（实现方式）

代码本身 + 关键位置的注释。

注释规范：
- **API 级注释**：解释"为什么调用这个 Vulkan API"（不是解释 API 本身做什么）
- **设计级注释**：解释这个类/函数在整个架构中的位置
- **陷阱注释**：标记已知的问题或限制

```cpp
// 好注释：解释为什么需要这个屏障
// 注意：VMA 分配的内存可能已经是最优内存类型，
// 但我们需要确保 CPU write 对 GPU visible
buffer.flush();

// 坏注释：重复 API 文档
// 调用 vkFlushMappedMemoryRanges 刷新内存范围
```

### 3.4 Trade-offs（放弃的替代方案）

明确说明本实现放弃了哪些替代方案，以及原因。

```markdown
**Trade-offs**:
- ❌ 不使用 `std::unique_ptr<VkBuffer_T*>`：Vulkan 句柄不是指针，且需要自定义删除器
- ❌ 不封装为仅 GPU 可见的 Buffer：保留了 staging upload 的能力，更灵活
- ✅ 使用 VMA 而非手动分配：牺牲少量控制权，换取开发效率
```

### 3.5 验证步骤

提供可执行的验证命令或步骤：

```markdown
**验证**:
1. 编译：`cmake --build build --target KazuEngine`
2. 运行测试：`./build/tests/test_buffer`
3. 检查：程序应正常退出，无 Validation Layer 报错
```

---

## 4. 代码 Review Checklist

Review 时对照以下清单检查：

```
□ 代码量 < 300 行（头文件 + 实现 + 测试/示例）
□ 所有 Vulkan 对象生命周期明确（谁创建、谁销毁）
□ 没有隐藏的 GPU 内存分配（显式使用 VMA 或自定义分配器）
□ 错误处理路径覆盖（至少检查 VK_SUCCESS）
□ 提供了编译/运行验证步骤
□ 文档解释了"为什么这样设计"
□ 没有过度封装（原始 Vk* 句柄可访问）
□ Debug 模式下有合理的日志/assert
```

---

## 5. 上下文管理协议

这是本项目的核心挑战。Vulkan 引擎代码量大、依赖复杂，AI 上下文窗口会被迅速压垮。

### 5.1 层级隔离

每次对话只聚焦 **一个架构层级**，不跨层修改代码：

| 层级 | 职责 | 依赖方向 |
|------|------|---------|
| **Core** | Vulkan 对象封装（Buffer, Image, Swapchain） | 无 |
| **RHI** | 资源管理（DescriptorSetLayoutCache, PipelineCache） | Core |
| **RenderGraph** | 渲染流程声明与执行 | Core + RHI |
| **Technique** | 具体渲染技术（Deferred, SSAO, HSGI） | 以上全部 |
| **Application** | 窗口、主循环、ImGui | 以上全部 |

**规则**：修改 Core 时，不修改 Technique；修改 Technique 时，可以引用 Core 的接口但不修改它。

### 5.2 接口契约化

层与层之间通过明确接口交互，接口一旦在文档中约定，短期内不变：

```cpp
// Core/Buffer.h - 契约接口示例
class Buffer {
public:
    VkBuffer handle() const;           // 永远暴露原始句柄
    void upload(const void* data, size_t size, size_t offset = 0);
    void* mapped() const;              // nullptr if GPU-only
private:
    // 实现细节可以变，但 public 接口尽量稳定
};
```

### 5.3 增量加载

对话开始时，AI 只加载当前 Nano-Feature 相关的文件，不加载整个项目。

加载优先级：
1. 当前要修改的文件
2. 当前文件的直接依赖（如基类、使用的工具类）
3. 接口契约文档（`docs/knowledge/`）

### 5.4 外置记忆（Knowledge Base）

将已稳定的设计决策、接口定义写入 `docs/knowledge/` 下的 Markdown，作为"外置记忆"。

当上下文被压缩时，可以从这些文件中恢复关键信息。

---

## 6. 工程实践

### 6.1 版本控制策略

**主分支保护**：`main` 分支永远是可编译、可运行的。

**开发流程**：
```
讨论 Nano-Feature → AI 实现 → Review → 修正 → 合并到 main
```

由于我们是 1v1 协作，可以简化：直接在 main 上开发，但每个 Nano-Feature 完成后做一次"逻辑提交"（即使不实际 git commit，也记录状态）。

### 6.2 错误处理哲学："提前崩溃"

Vulkan 的错误信息往往晦涩（如 `VK_ERROR_DEVICE_LOST`）。Debug 模式下必须：

1. **开启 Validation Layer**（项目第一天就要做）
2. **VK_CHECK 宏**：对每个 `vk*` 调用做宏封装，失败时立刻 `assert` + 打印位置
3. **关键资源日志**：DescriptorSet、Pipeline、RenderPass 创建时打印日志

```cpp
#define VK_CHECK(x)                                                 \
    do {                                                            \
        VkResult err = x;                                           \
        if (err != VK_SUCCESS) {                                    \
            LOG_ERROR("Vulkan error: {} at {}:{}",                  \
                      string_VkResult(err), __FILE__, __LINE__);    \
            assert(false);                                          \
        }                                                           \
    } while(0)
```

**"提前崩溃"比"静默错误"好调试一万倍。**

### 6.3 最小可运行版本（MVP）优先

设计文档的 5 层架构很完善，但直接从头写，前几周都看不到窗口里的三角形，容易失去动力。

**建议的替代路径**：

| 阶段 | 目标 | 代码质量 |
|------|------|---------|
| **Week 1** | "脏"版本：直接调 Vulkan API，渲染旋转三角形 | 不封装，能跑就行 |
| **Week 2+** | 在设计文档架构下重写，脏版本作为验证参考 | 按架构规范来 |

好处：
- 始终有一个能跑的东西
- 通过对比"脏代码"和"干净架构"，更深刻理解设计决策
- 验证环境配置（Vulkan SDK、GLFW、编译器）是否正确

---

## 7. 技术实现建议

### 7.1 Shader 先行策略

对于图形渲染项目，Shader 是技术实现的"灵魂"。建议每个技术点都先写 Shader 伪代码，再写 C++ 封装。

```
流程：算法理解 → Shader 草稿 → C++ 封装 → 集成到 RenderGraph
```

### 7.2 Ground Truth 对比文化

设计文档提到了 CUDA Path Tracer 迁移作为 Ground Truth，建议扩展到每个技术点：

| 技术 | Ground Truth |
|------|-------------|
| SSAO | 对比无 AO 的画面 |
| SSR | 对比 CubeMap Fallback |
| DDGI | 对比只加直接光的画面 |

**肉眼验证**比复杂的 GPU 单元测试更实用。

### 7.3 性能基线从第一天记录

即使只是渲染三角形，也记录：
- Frame Time (ms)
- GPU Memory Usage
- Draw Call Count

每增加一个功能，立刻看到性能代价。

---

## 8. 工作流文档清单

本文档是工作流的核心，配套以下文件：

| 文件 | 用途 |
|------|------|
| `docs/workflow/01-principles.md` | 本文件：开发哲学与工作流约定 |
| `docs/workflow/02-checklist.md` | Review Checklist（快速对照） |
| `docs/workflow/03-session-log.md` | 每次对话的记录（上下文摘要） |
| `docs/knowledge/vulkan-core.md` | Core Layer 接口约定（外置记忆） |
| `docs/knowledge/rhi-resources.md` | Resource Layer 约定（外置记忆） |
| `docs/knowledge/rendergraph.md` | RenderGraph 设计决策（外置记忆） |

---

## 9. 变更记录

| 日期 | 变更内容 | 变更人 |
|------|---------|--------|
| 2026-04-29 | 初始版本 | AI Assistant |
