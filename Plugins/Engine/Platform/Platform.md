# Platform

## Code files

- [Platform.h](Public/Platform.h) — 原生表面 + 事件服务层（`FPlatform` / `IPlatform` / `FNativeSurface`）
- [Platform.cpp](Private/Platform.cpp) — GLFW / EGL 后端 + 生命周期实现 + `CreateLayer` 导出

## Concept - Native Surface + Events

Platform 把"原生窗口/上下文句柄"与"事件泵"收敛成一个引擎层。`FNativeSurface` 是 `void*` 不透明句柄（`GLFWwindow*` / `EGLContext` / `ANativeWindow*` / `UIView*`……）；桌面（Win32/Linux）用 **GLFW 窗口**，Linux 可用 **EGL pbuffer headless 上下文**（无 OS 窗口）。窗口语义藏在 `IPlatform::GetNativeWindow()` 单一访问器后——无窗口平台（headless / Android surface / iOS view）不需要暴露"窗口"概念。

### FPlatform - 服务层 + 帧阶段

`FPlatform : FLayer<IPreInit, IInit, IPostInit, IBeginFrame, ITick, IEndFrame, IExit, IPreShutdown, IShutdown, IPostShutdown>`，不是单例——宿主引擎持有层实例，`GetPlatform()` 只是全局访问器。构造时声明 `IInit` 依赖 `"FConfig"` 的 `IInit`（窗口尺寸从 Config 层推入的 CVar 读，Config 必须先初始化）。

- **Initialize**：从 `r.Window.Width/Height/Title` CVar 读配置；桌面平台 `CreateWindow`；发布 `GPlatform`。
- **CreateWindow(W, H, Title)**：按当前平台选后端（桌面 GLFW 窗口），成功返回 true（Surface 和原生句柄都非空）。
- **CreateHeadlessContext(W, H)**：Linux 上 EGL pbuffer headless 上下文（OpenGL ES 2.0）。
- **DestroyWindow**：销毁后端（重置 Surface + 事件闭包）——可切换到 headless。
- **PollEvents**：显式泵一次平台事件（每帧）——引擎没有隐式事件循环；无后端时 no-op。
- **GetNativeWindow**：原生句柄；headless 或创建失败时 nullptr。
- **IsHeadless**：`Surface == nullptr`。
- **ShouldClose**：窗口关闭请求（`glfwWindowShouldClose`）；headless 或无事件时恒 false。
- **GetWindowWidth/Height**：创建的窗口尺寸。

引擎循环驱动生命周期：

```cpp
Platform::FPlatform Platform;
if (!Platform.CreateWindow(1280, 720, "MyGame")) { /* 创建失败 */ }
Engine.Install(&Platform);   // 引擎调度器驱动 Initialize / Tick(PollEvents) / RequestExit / Shutdown
```

## Third-party dependencies

- **GLFW 3.4**（`Platform.cmake` FetchContent，桌面窗口后端；`-DMAHO_HEADLESS=ON` 时不抓取）
- **EGL**（Linux，headless 后端）
- 其他插件：`ConsoleVariable`（窗口尺寸/标题 CVar）、`Log`（日志）——`.cplugin` Dependencies = `["ConsoleVariable", "Log"]`

## Related docs

- [API.md](API.md) - API documentation
