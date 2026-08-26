# Platform — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 职责边界：给 RHI 提供**原生表面 + 事件**——桌面窗口（GLFW）与 Linux 无头上下文（EGL pbuffer）。窗口语义藏在 `IPlatform::GetNativeWindow()` 单访问器后——无窗口平台（headless / Android surface / iOS view）不暴露窗口概念。
- 依赖只走 `.cplugin` `Dependencies`（当前为 `[]`，引擎核心经 codegen 自动 link），include `<Platform.h>`，不跨目录相对 include。
- 实现要点：
  - `TSingleton` + `IPlugin<IInit, IShutdown>`，`Get()` 定义在 `Private/Platform.cpp`（Platform.dll 内进程唯一，Meyers 单例）。
  - backend 按平台选：`CreateWindowBackend`（`!MAHO_HEADLESS && (_WIN32 || __linux__)` → `FGlfwWindow`）/ `CreateHeadlessBackend`（`__linux__` → `FEGLHeadlessWindow`）。非目标平台返回空 backend——调用方勿假定一定有表面，`GetNativeWindow()` 可为 `nullptr`。
  - `Initialize` 空启动（backend 惰性创建）；`DestroyWindow` 可切换到无头；`PollEvents` 显式泵送（引擎无隐式 stage 循环）。
  - 无头构建开关：`-DMAHO_HEADLESS=ON` 时不拉 GLFW（`Platform.cmake`），头文件/cpp 按宏分支；EGL（Linux）两种模式都可用。
  - backend 细节（`FGlfwWindow` / `FEGLHeadlessWindow` / `FPlatformBackend`）全在 cpp 匿名命名空间，头文件只暴露 `IPlatform` 抽象。
  - Win32 宏污染：`Windows.h`（经 GLFW 拉入）`#define CreateWindow`，头文件与 cpp 顶部均有 `#undef CreateWindow` 保护，勿删除。
- 遵循根 [AGENTS.md](../../../../AGENTS.md)。
