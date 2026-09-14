# Render

## Code files

- [Render.h](Public/Render.h) — 渲染子系统（`FRender` + 4 个渲染 stage 接口）
- [RenderApi.h](Public/RenderApi.h) — `MAHO_RENDER_API` 导出宏
- [RDG.h](Public/RDG.h) — RDG 资源句柄（`FRDGTextureRef` / `FRDGBufferRef` / `FRenderTarget`）
- [ShaderCompiler.h](Public/ShaderCompiler.h) — 异步着色器编译器（`FShaderCompilerServer` + desc/result）
- [Render.cpp](Private/Render.cpp) — FRender 生命周期 + 渲染图驱动 + `CreateLayer` 导出
- [RenderResourcePool.cpp](Private/RenderResourcePool.cpp) — RDG 资源池（按描述符复用 + transient 过期）

## Concept - Render Subsystem

Render 是渲染子系统：主引擎帧阶段层 + 渲染 feature 的 collector + 专用渲染线程（FThreadedServer）。宿主引擎只看到 FRender 这一个层；渲染 feature（实现 IBeginRender/IRender/IEndRender/IPresent）全部安装在 FRender 内部、由 FRender 自己的渲染图调度。FRender 同时是 RDG 资源池。

### 1. 三个基类角色

- **主引擎帧阶段层**：`FLayer<IPreInit..IPostShutdown>` 10 阶段，宿主引擎调度（Initialize / BeginFrame / Tick / EndFrame / Shutdown）。
- **渲染 feature 的 collector**：`FLayerCollector<FRender>`——Install / RequestUninstall 渲染 feature，调度上下文是 `FRender`。
- **渲染线程**：`FThreadedServer`——RHI 是渲染服务器（不是被调度的层），渲染线程就是它的服务器线程。

### 2. 渲染图（IBeginRender → IRender → IEndRender）

持久渲染图 `FLayerTaskGraph<TTypeList<IBeginRender, IRender, IEndRender>, FRender>`：帧首 Flush（等上一帧异步任务），帧末 Execute（提交下一帧）——渲染线程跨帧流水。IPresent 不进图：它由 `FRender::Tick` 在图上 flush 后驱动，与共享帧命令缓冲串行，永不被并发录制。

```text
Tick:
  FlushPendingUpdatePipelines<IBeginRender, IRender, IEndRender>()   // 应用安装/卸载
  RenderGraph.Init(Select<IBeginRender, IRender, IEndRender>())
  RenderGraph.Compile() -> Execute() -> Flush()                      // 驱动渲染 feature
  for Presenter in Select<IPresent>(): Invoke<IPresent, FRender>(Presenter, *this)
```

### 3. RDG 资源池

`FRDGTextureRef` / `FRDGBufferRef` 是非 RHI 句柄，原生 `FRHITexture` / `FRHIBuffer` 活在 `FRHIResourcePool` 里。**持久资源**按描述符复用（refcount）；**transient 资源**每帧过期（BeginFrame 销毁原生对象），下一次同描述符创建拿到新对象。`Present(Ref)` 是唯一接触交换链后缓冲的入口。

```cpp
FRDGTextureRef Scene = Render.CreateTexture(SceneDesc, /*bTransient=*/false);
// ... feature 录制渲染到 Scene ...
Render.Present(Scene);   // blit 到交换链后缓冲
Render.ReleaseTexture(Scene);
```

### 4. 生命周期

```text
PreInitialize:                       // 安装树：本层在 Render.cplugin 的 Plugins 里声明的 feature
  InstallChildrenOf(GetName())       // Scene / DrawTriangleFeature / UIFeature / FrameRenderFeature / ExampleEditor(Type=Editor)
                                     // 装进**本 collector**；装载 + 构造即时发生，Init 阶段等到 Tick 的安全点

Initialize:
  1. RHI = make_unique<FRHI>(); RHI->Initialize(Platform->GetNativeWindow(), W, H)
  2. ResourcePool = make_unique<FRHIResourcePool>(RHI.get())
  3. ShaderCompiler = make_unique<FShaderCompilerServer>(); ShaderCompiler->Initialize()
  4. RenderGraph = make_unique<FLayerTaskGraph<FRenderStages, FRender>>(Pool, *this)
  5. 绑定 Resource 的资产镜像回调（OnAssetImported / OnAssetUnloaded / OnAssetCreated + SetReadback）
  6. FThreadedServer::Initialize()   // 渲染线程

Tick: 帧首 RenderGraph->Flush() -> FlushPendingUpdatePipelines<IOnInstalled, IPreUnInstall>()
      -> RenderGraph->Init(Select<...IPresent...>) + Compile + Execute（无尾 Flush）
EndFrame: RenderGraph->Flush() -> RHI->EndFrame()（end + submit + present）

Shutdown（逆序释放）:
  RenderGraph.reset() -> ShaderCompiler->FlushCompiles()/reset -> ResourcePool->Shutdown()/reset
  -> RHI->ShutdownRHI()/reset -> FThreadedServer::Shutdown()
```

依赖（构造里用 DSL 声明）：`MyStage<IInit>().IsWaiting<Platform::FPlatform>().ForStage<IPostInit>()`（窗口必须先创建、`GPlatform` 先发布，RHI 才能读原生句柄），另有 `FLog` / `FPaths` / `FNamePool` 的正向边与卸载侧反向边（`FLog` / `FPlatform` / `FResourceSystem` / `FGameWorld` / 按名的 `"FUIViewRegistry"`）。

## Third-party dependencies

- **glslang / SPIRV-Tools**（`MAHO_WITH_GLSLANG`，异步 GLSL → SPIR-V 编译）
- 其他插件（`.cplugin` `Dependencies` = `["RHI", "Platform", "Resource", "Asset", "GameWorld"]`）：`RHI`（IRHI 命令面）、`Platform`（原生窗口句柄）、`Resource`/`Asset`（GPU 镜像 + 读回）、`GameWorld`（世界系统在 `IOnInstalled` 后驱动）
- 子插件（`.cplugin` `Plugins` = `["Scene", "DrawTriangleFeature", "UIFeature", "FrameRenderFeature", "ExampleEditor"]`）：由 `PreInitialize` 的 `InstallChildrenOf(GetName())` 装进本 collector；`ExampleEditor` 是 `Type=Editor`，Runtime 构建自动跳过

## Related docs

- [API.md](API.md) - API documentation
