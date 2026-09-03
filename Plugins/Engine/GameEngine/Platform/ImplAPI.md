# Platform（Private）— 实现算法字典

cpp 侧每个函数的算法伪代码解释。Public 侧 API 文档通过 `#fn-...` 锚点跳转落到这里。

## Platform.cpp

<a id="fn-platform-get"></a>
### GetPlatform()

← [公开 API](API.md) · `FPlatform*`

返回全局实例指针 `GPlatform`（`Initialize` 发布、`Shutdown` 清空）。

```text
GetPlatform():
1. return GPlatform
```

<a id="fn-platform-init"></a>
### FPlatform::Initialize(FEngineBase&)

← [公开 API](API.md) · `void`

从 CVar 读窗口尺寸，桌面平台创建窗口，发布全局实例。窗口尺寸来自 Config 层推入的 `r.Window.Width/Height` CVar。

```text
Initialize(Engine):
1. WindowWidth  = GCVarWindowWidth.GetValue()
2. WindowHeight = GCVarWindowHeight.GetValue()
3. #if !MAHO_HEADLESS
4.     bOk = CreateWindow(WindowWidth, WindowHeight, GCVarWindowTitle.GetValue())
5.     MAHO_LOG_CORE_INFO("FPlatform::Initialize - CreateWindow({}, {}) => {}", ...)
6. #endif
7. GPlatform = this
```

<a id="fn-platform-shutdown"></a>
### FPlatform::Shutdown(FEngineBase&)

← [公开 API](API.md) · `void`

```text
Shutdown(Engine):
1. GPlatform = nullptr
2. DestroyWindow()
```

<a id="fn-platform-createwindow"></a>
### FPlatform::CreateWindow(int Width, int Height, std::string_view Title)

← [公开 API](API.md) · `bool`

销毁旧后端，选择当前平台的后端（桌面 GLFW），保存 Surface + 事件闭包。

```text
CreateWindow(Width, Height, Title):
1. DestroyWindow()
2. Backend = CreateWindowBackend(Width, Height, Title)
3. Surface = move(Backend.Surface)
4. PollEventsFn = move(Backend.PollEvents)
5. QueryShouldClose = move(Backend.ShouldClose)
6. return Surface != nullptr && Surface->GetNativeWindow() != nullptr
```

<a id="fn-platform-headless"></a>
### FPlatform::CreateHeadlessContext(int Width, int Height)

← [公开 API](API.md) · `bool`

同上，但选 Linux EGL pbuffer 后端。

```text
CreateHeadlessContext(Width, Height):
1. DestroyWindow()
2. Backend = CreateHeadlessBackend(Width, Height)
3. 同 CreateWindow 保存三件套
4. return Surface != nullptr && Surface->GetNativeWindow() != nullptr
```

<a id="fn-platform-destroy"></a>
### FPlatform::DestroyWindow()

← [公开 API](API.md) · `void`

```text
DestroyWindow():
1. Surface.reset()
2. PollEventsFn = {}
3. QueryShouldClose = {}
```

<a id="fn-platform-poll"></a>
### FPlatform::PollEvents()

← [公开 API](API.md) · `void`

```text
PollEvents():
1. if PollEventsFn: PollEventsFn()
```

<a id="fn-platform-tick"></a>
### FPlatform::Tick(FEngineBase&) / FPlatform::RequestExit(FEngineBase&)

← [公开 API](API.md) · `void`

帧阶段：Tick 泵事件；RequestExit 把窗口关闭请求转发给宿主引擎。

```text
Tick(Engine):
1. PollEvents()

RequestExit(Engine):
1. if ShouldClose(): Engine.RequestExit()
```

<a id="fn-platform-backend"></a>
### CreateWindowBackend / CreateHeadlessBackend（内部）

匿名命名空间。按平台构建 `FPlatformBackend`（`unique_ptr<IPlatform>` + PollEvents/ShouldClose 闭包）；闭包捕获裸 `FGlfwWindow*` / `FEGLHeadlessWindow*`，生命周期由 `Surface` unique_ptr 持有。

```text
CreateWindowBackend(Width, Height, Title):
1. #if !MAHO_HEADLESS && (WIN32 || linux)
2.     Backend = make_unique<FGlfwWindow>(Width, Height, Title)
3.     Raw = Backend.get()
4.     return { move(Backend), [Raw]{ Raw->PollEvents(); }, [Raw]{ return Raw->ShouldClose(); } }
5. #else
6.     return {}
7. #endif

CreateHeadlessBackend(Width, Height):
1. #if linux
2.     Backend = make_unique<FEGLHeadlessWindow>(Width, Height)
3.     return { move(Backend), {}, {} }
4. #else
5.     return {}
6. #endif
```

<a id="fn-platform-export"></a>
### CreateLayer()（C 导出）

按符号名查找的动态安装导出——宿主加载 DLL 后经 `GetProcAs` 取 `CreateLayer` 实例化层。

```text
extern "C" CreateLayer():
1. return Maho::Platform::FPlatform::CreateLayer()
```

- [Platform.md](Platform.md) — 概念 · [公开 API](API.md) — 签名入口
