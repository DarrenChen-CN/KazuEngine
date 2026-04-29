# Session Log（对话记录）

> 记录每次对话的内容摘要、决策和状态，作为外置记忆。
> 每次对话结束后，更新此文件。

---

## 格式模板

### Session [编号] - [日期]

**目标 Nano-Feature**: 

**涉及文件**:
- `src/xxx/xxx.h`
- `src/xxx/xxx.cpp`

**关键决策**:
- 决策 1（原因）
- 决策 2（原因）

**交付状态**: □ 完成 / □ 进行中 / □ 阻塞

**已知问题/待办**:
- [ ] 问题 1

**上下文摘要**（用于恢复对话）:
```
[用 200 字描述当前项目状态，帮助恢复上下文]
```

---

## 记录

### Session 0 - 2026-04-29

**目标 Nano-Feature**: 工作流约定文档制定

**涉及文件**:
- `docs/workflow/01-principles.md`
- `docs/workflow/02-checklist.md`
- `docs/workflow/03-session-log.md`

**关键决策**:
- 采用 Nano-Feature 粒度（单次 < 300 行）
- 每次交付必须包含 What/Why/How/Trade-offs/验证
- 使用外置记忆（docs/knowledge/）应对上下文压缩
- 先做"脏版本" MVP，再按架构重写
- 采用"提前崩溃"的错误处理哲学

**交付状态**: ✅ 完成

**已知问题/待办**:
- [ ] 确认 Vulkan SDK 版本和开发环境
- [ ] 搭建基础项目结构（CMake + GLFW）
- [ ] 创建第一个 Nano-Feature：Instance + Device 初始化

**上下文摘要**:
```
项目刚启动，已完成工作流约定。环境已确认：Vulkan 1.3 + MSVC + Windows
（保留跨平台能力：GLFW + CMake）。GPU 为 RTX 4060 Laptop，支持光追扩展。
下一步：Nano-Feature 1 - 脏代码 MVP：Instance/Device/ValidationLayer +
Surface/Swapchain + 第一个三角形。先提供知识文档，用户 Review 后再写代码。
```

---

### Session 1 - 2026-04-29

**目标 Nano-Feature**: 脏代码 MVP - Vulkan 初始化 + 第一个三角形

**涉及文件**:
- `CMakeLists.txt`
- `src/main.cpp`
- `shaders/triangle.vert`
- `shaders/triangle.frag`

**关键决策**:
- 使用 GLFW 保持跨平台能力
- 使用 Vulkan 1.3 Core（不依赖特定扩展）
- 开发模式强制开启 Validation Layer
- RTX 4060 支持所有光追扩展，后续可选 VK_KHR_ray_query

**交付状态**: 🔄 进行中（知识文档阶段）

**已知问题/待办**:
- [ ] 确认 GLFW + Vulkan SDK 的 CMake 配置
- [ ] 用户 Review 知识文档后确认设计思路
- [ ] 实现 Instance + Device + Surface + Swapchain
- [ ] 实现 Graphics Pipeline + 渲染三角形

**上下文摘要**:
```
开始第一个 Nano-Feature：脏代码 MVP。目标是不封装任何类，直接调用 Vulkan API，
在窗口中渲染一个彩色三角形。核心学习点：Instance/Device/Swapchain 创建流程、
Graphics Pipeline 状态预编译、Command Buffer 录制与提交、Fence/Semaphore 同步。
```
