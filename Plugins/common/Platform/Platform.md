# Platform

## 代码文件

- [Platform.h](Platform.h) — 原生表面服务（`FNativeSurface` / `IPlatform` / `FPlatformSystem`）

## 概念——原生表面 + 事件

原生表面单例服务——给 RHI 提供**原生窗口/上下文句柄**：`FNativeSurface` 是 `void*` 不透明指针（`GLFWwindow*` / `EGLContext` / `ANativeWindow*` / `UIView*` ...）。桌面端（Win32/Linux）走 **GLFW 窗口**；Linux 可走 **EGL pbuffer 无头上下文**（无 OS 窗口）。窗口语义藏在 `IPlatform::GetNativeWindow()` 单一访问器后面——无窗口平台（headless / Android surface / iOS view）不必暴露窗口概念。

### FPlatformSystem —— 表面服务（单例）

`TSingleton<FPlatformSystem>` + `IPlugin<IInit, IShutdown>`：

- **Initialize(Argc, Argv)**：空启动——backend 由 `CreateWindow` / `CreateHeadlessContext` **惰性创建**。
- **CreateWindow(W, H, Title)**：按当前平台选 backend（桌面 GLFW 窗口），成功返回 true（Surface 且原生句柄非空）。
- **CreateHeadlessContext(W, H)**：Linux 下 EGL pbuffer 无头上下文（OpenGL ES 2.0）。
- **DestroyWindow**：销毁 backend（reset 表面 + 事件闭包）——可切换到无头。
- **PollEvents**：显式泵送平台事件（每帧一次）——引擎无隐式 stage 循环；无 backend 时为空操作。
- **GetNativeWindow**：原生句柄；无头或创建失败为 `nullptr`。
- **IsHeadless**：`Surface == nullptr`。
- **ShouldClose**：窗口关闭请求（`glfwWindowShouldClose`）；无头或无事件恒 false。

宿主驱动固定生命周期：

```cpp
Platform::FPlatformSystem::Get().Initialize(0, nullptr);
Platform::FPlatformSystem::Get().CreateWindow(1280, 720, "MyGame");
while (!Platform::FPlatformSystem::Get().ShouldClose())
{
    Platform::FPlatformSystem::Get().PollEvents();
    // render frame...
}
Platform::FPlatformSystem::Get().Shutdown();
```

## 三方依赖

- **GLFW 3.4**（`Platform.cmake` FetchContent，桌面窗口 backend；`-DMAHO_HEADLESS=ON` 时不拉取）。
- **EGL**（Linux，无头 backend，两种模式都可用）。
- 其他插件：无——`.cplugin` Dependencies = `[]`（引擎核心经 codegen 自动 link）。

## 相关文档

- [API.html](API.html) — API 文档（公开签名）
- [ImplAPI.html](ImplAPI.html) — 实现算法字典（cpp 函数伪代码）
