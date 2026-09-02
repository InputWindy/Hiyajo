# Platform — API 文档

Platform 插件 = 原生表面 + 事件服务层，作为引擎 10 阶段帧层挂载。窗口语义藏在 `IPlatform` 后端后，`FNativeSurface` 是不透明句柄，RHI 经 `GetPlatform()` 读取原生窗口。无独立 PlatformApi.h——导出用 Core 的 `MAHO_API`。

## Platform.h

### FNativeSurface <alias>

原生窗口 / 表面句柄——不透明 `void*`（GLFW 窗口、EGL 上下文、ANativeWindow、UIView……）。无窗口平台（headless / Android surface / iOS view）不需要暴露"窗口"概念。

#### 说明

| 签名 | 说明 |
|------|------|
| `using FNativeSurface = void*` | 原生表面句柄，RHI 创建设备时使用 |

### IPlatform <class>

最小平台接口——只暴露 RHI 需要的原生表面。**不是每类平台都有"窗口"**（headless、Android surface、iOS view），窗口语义就藏在这个单一访问器后面。

#### 接口

| 签名 | 说明 |
|------|------|
| `virtual ~IPlatform()` | 虚析构 |
| `[[nodiscard]] virtual FNativeSurface GetNativeWindow() const = 0` | 原生窗口/表面句柄；headless 或创建失败时返回 nullptr |

### FPlatform <class>

平台系统——原生表面 + 事件（引擎层 feature）。构造时声明运行时依赖：`IInit` 阶段依赖 `"FConfig"` 的 `IInit`（窗口尺寸来自 Config 层推入的 CVar，Config 必须先初始化）。

#### 接口

| 签名 | 说明 |
|------|------|
| `FPlatform()` | 构造；`AddDependency(IInit, "FConfig", IInit)` |
| `~FPlatform() override` | 析构 |
| `bool CreateWindow(int Width, int Height, std::string_view Title)` | 创建窗口（按当前平台选后端）；失败返回 false |
| `bool CreateHeadlessContext(int Width, int Height)` | 创建 headless 渲染上下文（Linux 上 EGL pbuffer） |
| `void DestroyWindow()` | 销毁后端（可切换到 headless） |
| `void PollEvents()` | 泵平台事件（由 Tick 调用） |
| `[[nodiscard]] FNativeSurface GetNativeWindow() const` | 原生句柄；headless 或创建失败时 nullptr |
| `[[nodiscard]] bool IsHeadless() const` | `Surface == nullptr` |
| `[[nodiscard]] bool ShouldClose() const` | 窗口关闭请求（headless 或无事件时 false） |
| `[[nodiscard]] std::uint32_t GetWindowWidth() const` | 创建的窗口宽度（来自 Config 层 CVar） |
| `[[nodiscard]] std::uint32_t GetWindowHeight() const` | 创建的窗口高度 |

#### 生命周期阶段（10 个 stage）

| 阶段 | 方法 | 行为 |
|------|------|------|
| `IPreInit` | `PreInitialize(FEngineBase&)` | no-op |
| `IInit` | `Initialize(FEngineBase&)` | 从 CVar 读窗口尺寸；桌面平台创建窗口；发布 `GPlatform` |
| `IPostInit` | `PostInitialize(FEngineBase&)` | no-op |
| `IBeginFrame` | `BeginFrame(FEngineBase&)` | no-op |
| `ITick` | `Tick(FEngineBase&)` | `PollEvents()` |
| `IEndFrame` | `EndFrame(FEngineBase&)` | no-op |
| `IExit` | `RequestExit(FEngineBase&)` | `ShouldClose()` → `Engine.RequestExit()` |
| `IPreShutdown` | `PreShutdown(FEngineBase&)` | no-op |
| `IShutdown` | `Shutdown(FEngineBase&)` | 清 `GPlatform` + `DestroyWindow()` |
| `IPostShutdown` | `PostShutdown(FEngineBase&)` | no-op |

#### 后端（private）

`CreateWindowBackend` 选桌面 GLFW 后端（Windows 上经 `glfwGetWin32Window` 取真实 HWND）；`CreateHeadlessBackend` 选 Linux EGL pbuffer。后端状态封装在 `FPlatformBackend`（`unique_ptr<IPlatform>` + PollEvents/ShouldClose 闭包）。

### GetPlatform <自由函数>

全局平台实例访问器（经函数跨 DLL，避免裸变量导出）。

#### 接口

| 签名 | 说明 |
|------|------|
| `MAHO_API FPlatform* GetPlatform()` | 返回全局 `FPlatform*`；`Initialize` 后非空，`Shutdown` 后置空 |

- [Platform.md](Platform.md) — 概念 · [实现字典](ImplAPI.md) — 算法
