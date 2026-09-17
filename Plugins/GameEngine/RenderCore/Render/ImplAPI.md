# Render（Private）— 实现算法字典

cpp 侧关键函数的算法伪代码解释。Public 侧 API 文档通过 `#fn-...` 锚点跳转落到这里。Render.cpp 用 **unity build** 折叠 ShaderCompiler.cpp 和 RenderResourcePool.cpp（codegen 只编译 Render.cpp）。

## Render.cpp

<a id="fn-render-init"></a>
### FRender::Initialize(FEngineBase&)

← [公开 API](API.md) · `void`

装载归 `PreInitialize`（安装树声明，见下），服务归 `Initialize`：RHI（用 Platform 原生窗口）→ 资源池 → 着色器编译服务 → 渲染图 → 资产镜像绑定 → 渲染线程。

```text
PreInitialize(Engine):
1. InstallChildrenOf(GetName())   // 本层声明在 Render.cplugin 的 Plugins：
                                  // Scene / DrawTriangleFeature / UIFeature / FrameRenderFeature / ExampleEditor(Type=Editor)
                                  // 按 module base name 装进**本 collector**（不是宿主的），
                                  // 装载 + 构造即时发生；它们的 Init 阶段在我自己的安全点（Tick）才跑

Initialize(Engine):
1. P = Platform::GetPlatform()
2. if P != nullptr && P->GetNativeWindow() != nullptr:
       RHI = make_unique<FRHI>()
       if !RHI->Initialize(P->GetNativeWindow(), P->GetWindowWidth(), P->GetWindowHeight()):
           MAHO_LOG_CORE_ERROR("FRender::Initialize: RHI initialization failed")
           RHI.reset()
3. ResourcePool = make_unique<FRHIResourcePool>(RHI.get())
4. ShaderCompiler = make_unique<FShaderCompilerServer>(); ShaderCompiler->Initialize()
5. RenderGraph = make_unique<FLayerTaskGraph<FRenderStages, FRender>>(Pool, *this)
6. 绑定 Resource 的资产镜像回调（OnAssetImported / OnAssetUnloaded / OnAssetCreated + SetReadback）
7. FThreadedServer::Initialize()                              // 渲染线程
```

<a id="fn-render-beginframe"></a>
### FRender::BeginFrame(FEngineBase&) / EndFrame(FEngineBase&)

← [公开 API](API.md) · `void`

帧边界的 RHI 原语 + transient 资源过期。

```text
BeginFrame(Engine):
1. if RHI: RHI->BeginFrame()
2. if ResourcePool: ResourcePool->BeginFrame()   // transient 资源过期

EndFrame(Engine):
1. if RHI: RHI->EndFrame()   // end + submit 帧命令缓冲
```

<a id="fn-render-tick"></a>
### FRender::Tick(FEngineBase&)

← [公开 API](API.md) · `void`

驱动渲染图：帧首排空上一帧 → 应用 feature 安装/卸载 → `Init` + `Compile` + `Execute`（**无尾 Flush**：本帧任务留给下一次帧首 `Flush` 或 `EndFrame` 的 `Flush`）。

```text
Tick(Engine):
1. if !RenderGraph: return
2. RenderGraph->Flush()                       // 帧首栅栏：等上一帧的图任务（Init 不与在飞的 Render() 竞争）
3. FlushPendingUpdatePipelines<IOnInstalled, IPreUnInstall>()   // 应用 feature 的安装 / 卸载
4. RenderGraph->Init(Select<[IEditorInput,] IInitViews, IBeginRender, IRender, IEndRender,
                           IPostProcess, IRenderUI [, IEditorCompose], IPresent>())
5. if !RenderGraph->Compile(): ReportFatal("FRender::Tick: render pipeline Compile failed")
6. RenderGraph->Execute()                     // 无尾 Flush
```

<a id="fn-render-shutdown"></a>
### FRender::Shutdown(FEngineBase&)

← [公开 API](API.md) · `void`

**逆序释放**：渲染图 → 编译服务 → 资源池 → RHI → 渲染线程。

```text
Shutdown(Engine):
1. RenderGraph.reset()                        // 先释放图（在 FFrameBuilder 的池之前）
2. if ShaderCompiler: ShaderCompiler->FlushCompiles(); ShaderCompiler.reset()
3. if ResourcePool: ResourcePool->Shutdown(); ResourcePool.reset()
4. if RHI: RHI->ShutdownRHI(); RHI.reset()
5. FThreadedServer::Shutdown()
```

<a id="fn-render-present"></a>
### FRender::Present(FRDGTextureRef& Src)

← [公开 API](API.md) · `void`

唯一接触交换链后缓冲的入口——blit 离屏颜色纹理到后缓冲。

```text
Present(Src):
1. if RHI && Src.IsValid(): RHI->PresentTexture(Src.GetRHI())
```

## RenderResourcePool.cpp

<a id="fn-pool-createtex"></a>
### FRHIResourcePool::CreateTexture(const FRHITextureDesc& Desc, bool bTransient)

← [公开 API](API.md) · `FRDGTextureRef`

持久资源按描述符复用非活跃槽；transient 直接新建。新建时创建原生纹理 + 视图。

```text
CreateTexture(Desc, bTransient):
1. if !bTransient:
       for I in [0, Textures.size()):
           E = Textures[I]
           if !E.bTransient && !E.bActive && E.Desc == Desc && E.Native != nullptr:
               E.bActive = true; E.RefCount = 1
               return FRDGTextureRef(this, I)          // 复用
2. Slot = AllocTextureSlot()
3. Entry = Textures[Slot]; Entry.Desc = Desc; Entry.Native = RHI->CreateTexture(Desc)
4. Entry.RefCount = 1; Entry.bTransient = bTransient; Entry.bActive = true
5. if Entry.Native != nullptr:
       ViewDesc = { Entry.Native, Desc.Format, MipCount = Desc.MipLevels, ArrayLayerCount = Desc.ArrayLayers }
       Entry.View = RHI->CreateTextureView(ViewDesc)
6. return FRDGTextureRef(this, Slot)
```

<a id="fn-pool-createbuf"></a>
### FRHIResourcePool::CreateBuffer(const FRHIBufferDesc& Desc, bool bTransient)

← [公开 API](API.md) · `FRDGBufferRef`

同上（无视图）。持久按描述符复用；transient 直接新建。

```text
CreateBuffer(Desc, bTransient):
1. if !bTransient:
       for I in [0, Buffers.size()):
           E = Buffers[I]
           if !E.bTransient && !E.bActive && E.Desc == Desc && E.Native != nullptr:
               E.bActive = true; E.RefCount = 1
               return FRDGBufferRef(this, I)
2. Slot = AllocBufferSlot()
3. Entry = Buffers[Slot]; Entry.Desc = Desc; Entry.Native = RHI->CreateBuffer(Desc)
4. Entry.RefCount = 1; Entry.bTransient = bTransient; Entry.bActive = true
5. return FRDGBufferRef(this, Slot)
```

<a id="fn-pool-release"></a>
### FRHIResourcePool::ReleaseTexture / ReleaseBuffer

← [公开 API](API.md) · `void`

引用归零则置非活跃；transient 立即销毁原生对象；最后句柄 Reset。

```text
ReleaseTexture(Ref):
1. if !Ref.IsValid(): return
2. E = Textures[Ref.Id]
3. if E.RefCount > 0: E.RefCount -= 1
4. if E.RefCount == 0:
       E.bActive = false
       if E.bTransient && E.Native:
           if E.View: RHI->DestroyTextureView(E.View); E.View = nullptr
           RHI->DestroyTexture(E.Native); E.Native = nullptr
5. Ref.Reset()
```

<a id="fn-pool-beginframe"></a>
### FRHIResourcePool::BeginFrame()

← [公开 API](API.md) · `void`

transient 条目过期：销毁原生资源，下一次同描述符创建拿到新对象。

```text
BeginFrame():
1. for E in Textures:
       if E.bTransient && E.bActive:
           E.bActive = false
           if E.View: RHI->DestroyTextureView(E.View); E.View = nullptr
           if E.Native: RHI->DestroyTexture(E.Native); E.Native = nullptr
2. for E in Buffers:
       if E.bTransient && E.bActive:
           E.bActive = false
           if E.Native: RHI->DestroyBuffer(E.Native); E.Native = nullptr
```

<a id="fn-pool-shutdown"></a>
### FRHIResourcePool::Shutdown()

← [公开 API](API.md) · `void`

销毁全部原生资源并清空槽位（在 RHI 设备销毁前调用）。

```text
Shutdown():
1. for E in Textures: 销毁 E.View / E.Native
2. Textures.clear(); FreeTextureSlots.clear()
3. for E in Buffers: 销毁 E.Native
4. Buffers.clear(); FreeBufferSlots.clear()
```

<a id="fn-pool-get"></a>
### FRDGTextureRef::GetRHI / GetView / FRDGBufferRef::GetRHI

← [公开 API](API.md) · `FRHITexture*` / `FRHITextureView*` / `FRHIBuffer*`

瞬时解析点——只在当前帧录制期间有效。

```text
FRDGTextureRef::GetRHI():
1. return Pool ? Pool->GetTexture(*this) : nullptr

FRDGTextureRef::GetView():
1. return Pool ? Pool->GetTextureView(*this) : nullptr

FRDGBufferRef::GetRHI():
1. return Pool ? Pool->GetBuffer(*this) : nullptr
```

## ShaderCompiler.cpp

<a id="fn-shader-init"></a>
### FShaderCompilerServer::Initialize() / ~FShaderCompilerServer()

← [公开 API](API.md) · `bool` / 析构

```text
Initialize():
1. #if MAHO_WITH_GLSLANG
2.     glslang::InitializeProcess()
3. #endif
4. return FThreadedServer::Initialize()   // 启动编译线程

~FShaderCompilerServer():
1. Shutdown()
2. #if MAHO_WITH_GLSLANG: glslang::FinalizeProcess()
```

<a id="fn-shader-async"></a>
### FShaderCompilerServer::CompileAsync / FlushCompiles

← [公开 API](API.md) · `void`

编译提交到专用线程，完成后在调用线程回调。

```text
CompileAsync(Desc, OnDone):
1. Submit([this, Desc, OnDone]:
       Result = CompileStage(Desc)
       if OnDone: OnDone(Result))

FlushCompiles():
1. Flush()   // FIFO barrier
```

<a id="fn-shader-compile"></a>
### FShaderCompilerServer::CompileStage(const FShaderCompileDesc& Desc)（static）

← [公开 API](API.md) · `FShaderCompileResult`

同步 GLSL → SPIR-V（glslang）。调用线程跑，不经编译线程。

```text
CompileStage(Desc):
1. Result.bSuccess = false
2. #if !MAHO_WITH_GLSLANG: Result.ErrorLog = "glslang not available (MAHO_WITH_GLSLANG=0)"; return Result
3. if Desc.Source.empty(): Result.ErrorLog = "empty source"; return Result
4. Shader = TShader(ToGlslangStage(Desc.Stage))
5. Shader.setStringsWithLengths(Desc.Source, 1); Shader.setEntryPoint(Desc.EntryPoint)
6. Shader.setEnvClient(EShClientVulkan, Vulkan_1_0); Shader.setEnvTarget(EShTargetSpv, Spv_1_0)
7. if !Shader.parse(GetDefaultBuiltInResource(), 460, false, EShMsgDefault):
       Result.ErrorLog = Shader.getInfoLog() + Shader.getInfoDebugLog(); return Result
8. Program.addShader(&Shader)
9. if !Program.link(EShMsgDefault):
       Result.ErrorLog = Program.getInfoLog() + Program.getInfoDebugLog(); return Result
10. GlslangToSpv(*Program.getIntermediate(Stage), Result.Bytecode, &Logger, &Options)
11. Result.bSuccess = true; return Result
```

- [Render.md](Render.md) — 概念 · [公开 API](API.md) — 签名入口
