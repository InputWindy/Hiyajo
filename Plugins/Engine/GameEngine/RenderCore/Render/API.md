# Render — API 文档

Render 插件 = 渲染子系统：**主引擎帧阶段层 + 渲染 feature 的 collector + 专用渲染线程**（`FThreadedServer`）。宿主引擎只把它看作一个层；渲染 feature（实现 IBeginRender/IRender/IEndRender/IPresent）全部安装在 FRender 内部、由 FRender 自己的渲染图调度。FRender 同时是 RDG 资源池——离屏资源经 CreateTexture/CreateBuffer 创建，原生 FRHITexture/FRHIBuffer 活在这个池里（跨帧复用），`Present` 是唯一接触交换链后缓冲的入口。`RenderApi.h` 提供 `MAHO_RENDER_API` 导出宏。

## Render.h

### IBeginRender / IRender / IEndRender / IPresent <class>

FRender 自有的 4 个渲染 stage 能力接口——每个只有一个纯虚方法，签名统一为 `void Xxx(FRender&)`。由 FRender 内部安装的渲染 feature 实现。

| 接口 | 方法 |
|------|------|
| `IBeginRender` | `void BeginRender(FRender&)` |
| `IRender` | `void Render(FRender&)` |
| `IEndRender` | `void EndRender(FRender&)` |
| `IPresent` | `void Present(FRender&)` |

### FRender <class>

渲染子系统——三合一：宿主引擎的 10 阶段帧层 + `FLayerCollector<FRender>`（渲染 feature 集合）+ `FThreadedServer`（渲染线程）。内部持有一个持久渲染图 `FLayerTaskGraph<TTypeList<IBeginRender, IRender, IEndRender>, FRender>`：帧首 Flush 等上一帧异步任务，帧末 Execute 提交下一帧——渲染线程跨帧流水。IPresent **不进渲染图**，由 `FRender::Tick` 在图上 flush 后驱动，共享帧命令缓冲从不被并发录制。

#### 接口

| 签名 | 说明 |
|------|------|
| `IRHI* GetRHI() const` | RHI 命令面（渲染 feature 经它录命令） |
| `FShaderCompilerServer* GetShaderCompiler() const` | 异步着色器编译器（GLSL → SPIR-V） |
| `FRHICommandList* GetFrameCommandList() const` | 借用帧命令缓冲（已 begin；由 FRender end/submit，非拥有） |
| `[[nodiscard]] std::uint32_t GetFramebufferWidth() const` | 帧缓冲宽（交换链尺寸） |
| `[[nodiscard]] std::uint32_t GetFramebufferHeight() const` | 帧缓冲高 |
| `[[nodiscard]] ERHIFormat GetSwapchainFormat() const` | 交换链图像格式（离屏场景目标须匹配才能 blit） |
| `[[nodiscard]] FRDGTextureRef CreateTexture(const FRHITextureDesc& Desc, bool bTransient = false)` | 建离屏纹理（池化句柄） |
| `[[nodiscard]] FRDGBufferRef CreateBuffer(const FRHIBufferDesc& Desc, bool bTransient = false)` | 建离屏缓冲（池化句柄） |
| `void ReleaseTexture(FRDGTextureRef& Ref)` | 释放纹理（引用归零；transient 立即销毁原生对象） |
| `void ReleaseBuffer(FRDGBufferRef& Ref)` | 释放缓冲 |
| `void Present(FRDGTextureRef& Src)` | 把离屏颜色纹理 blit 到交换链后缓冲（IPresent 阶段） |

#### 生命周期阶段（宿主引擎 10 个 stage）

| 阶段 | 方法 | 行为 |
|------|------|------|
| `IInit` | `Initialize(FEngineBase&)` | 用 Platform 原生窗口建 RHI；建资源池 + 着色器编译服务 + 渲染图；安装 Scene.dll / DrawTriangleFeature.dll；启动渲染线程 |
| `IBeginFrame` | `BeginFrame(FEngineBase&)` | `RHI->BeginFrame()` + `ResourcePool->BeginFrame()`（transient 资源过期） |
| `ITick` | `Tick(FEngineBase&)` | 应用 feature 安装/卸载 → 编译执行渲染图（IBeginRender→IRender→IEndRender）并排空 → 驱动 IPresent presenters |
| `IEndFrame` | `EndFrame(FEngineBase&)` | `RHI->EndFrame()` |
| `IShutdown` | `Shutdown(FEngineBase&)` | 逆序释放：渲染图 → 编译服务（FlushCompiles）→ 资源池 → RHI → 渲染线程 |
| 其余 5 个 | — | no-op |

依赖：构造时声明 `IInit` 依赖 `"FPlatform"` 的 `IPostInit`（窗口必须先创建、`GPlatform` 先发布，RHI 才能读原生句柄）。

## RDG.h

### FRDGTextureRef <class>

RDG 纹理句柄——指向池化离屏纹理的**非 RHI 引用**。原生 `FRHITexture` 活在 FRender 的资源池里；`GetRHI()` / `GetView()` 是瞬时解析点，只在当前帧录制期间有效。feature 从不直接创建/销毁原生对象。

#### 接口

| 签名 | 说明 |
|------|------|
| `FRDGTextureRef() = default` | 空句柄 |
| `[[nodiscard]] bool IsValid() const` | `Pool != nullptr && Id != ~0u` |
| `void Reset()` | 清空句柄 |
| `[[nodiscard]] FRHITexture* GetRHI() const` | 瞬时解析到池化纹理（当前帧） |
| `[[nodiscard]] FRHITextureView* GetView() const` | 瞬时解析到池化纹理视图（当前帧） |

### FRDGBufferRef <class>

RDG 缓冲句柄——与 FRDGTextureRef 相同的瞬时解析语义。

#### 接口

| 签名 | 说明 |
|------|------|
| `FRDGBufferRef() = default` | 空句柄 |
| `[[nodiscard]] bool IsValid() const` | `Pool != nullptr && Id != ~0u` |
| `void Reset()` | 清空句柄 |
| `[[nodiscard]] FRHIBuffer* GetRHI() const` | 瞬时解析到池化缓冲（当前帧） |

### FRenderTarget <struct>

虚拟输出目标——用户声明"渲染到哪"。附件用 `FRDGTextureRef`（离屏）描述，feature 从不接触原生 framebuffer / 交换链。FRender 在 BeginRenderPass 时把声明解析成具体 render pass + framebuffer（缓存）。

#### 成员

| 字段 | 类型 | 说明 |
|------|------|------|
| `Color` | `std::vector<FAttachment>` | 颜色附件列表 |
| `Depth` | `FAttachment` | 深度附件 |
| `bHasDepth` | `bool` | 是否有深度附件 |
| `SampleCount` | `std::uint32_t` | MSAA 采样数（默认 1） |

#### FAttachment <嵌套 struct>

| 字段 | 类型 | 说明 |
|------|------|------|
| `View` | `FRDGTextureRef` | 附件纹理句柄 |
| `LoadOp` | `ERHILoadOp` | 加载操作（默认 `Clear`） |
| `StoreOp` | `ERHIStoreOp` | 存储操作（默认 `Store`） |
| `ClearColor[4]` | `float` | 清屏颜色（默认 {0, 0, 0, 1}） |

## ShaderCompiler.h

### FShaderCompileResult <struct>

一个已编译着色器阶段（SPIR-V 字节码）。

#### 成员

| 字段 | 类型 | 说明 |
|------|------|------|
| `bSuccess` | `bool` | 是否编译成功 |
| `Bytecode` | `std::vector<std::uint32_t>` | SPIR-V words |
| `ErrorLog` | `std::string` | 编译错误日志 |

### FShaderCompileDesc <struct>

着色器编译请求（GLSL 源码 + 阶段 + 入口点）。

#### 成员

| 字段 | 类型 | 说明 |
|------|------|------|
| `Source` | `std::string` | GLSL 源码 |
| `Stage` | `ERHIShaderStage` | 着色器阶段（默认 `Vertex`） |
| `EntryPoint` | `std::string` | 入口点（默认 `"main"`） |

### FShaderCompilerServer <class : FThreadedServer>

异步着色器编译器——专用编译线程（`FThreadedServer`）。FRender 持有它；`CompileAsync` 提交请求，编译完成后**在调用线程**回调。把 GLSL 编译从主线程和渲染线程都挪走。

#### 接口

| 签名 | 说明 |
|------|------|
| `FShaderCompilerServer()` | 构造 |
| `~FShaderCompilerServer() override` | 析构（Shutdown + glslang FinalizeProcess） |
| `bool Initialize()` | 启动编译线程（幂等）；无 glslang 时返回 false |
| `void CompileAsync(const FShaderCompileDesc& Desc, std::function<void(const FShaderCompileResult&)> OnDone)` | 提交异步编译；OnDone 在调用线程收结果 |
| `void FlushCompiles()` | 阻塞到所有已提交编译完成 |
| `static FShaderCompileResult CompileStage(const FShaderCompileDesc& Desc)` | 同步编译（在调用线程跑，不经编译线程） |

- [Render.md](Render.md) — 概念 · [实现字典](ImplAPI.md) — 算法
