# RHI — API 文档

RHI 插件 = 后端无关的 GPU 设备面，形态是**渲染服务器**（`FRHI final : FThreadedServer, IRHI`），**不是调度层**：一个常驻渲染服务线程（帧路径）+ 线程池并行命令录制 + 直接队列提交。swapchain / Vk / VMA 细节全部藏在私有的 `IDynamicRHI` 后端里，公共接口零 Vulkan 类型。本文档按头文件分节，如实反映当前 `Public/RHI/` 的公开接口。

## RHIServer.h

### IRHI <struct（能力接口）>

渲染层公开能力面，父层（FRender / RDG）持有 `FRHI` 并只经这个接口交互。命令列表生命周期属于调用方（RDG 负责帧隔离）；**何时提交**是调用方（RDG）的调度决策——RHI 只执行队列提交本身。帧原语 / 队列提交要求调用方保持串行。

#### 接口（命令列表）

| 签名 | 说明 |
|------|------|
| `[[nodiscard]] FRHICommandList* CreateCommandList(ERHICommandListType Type)` | 创建指定类型的命令列表；生命周期归调用方（RDG 管帧隔离），`DestroyCommandList` 释放 |
| `void DestroyCommandList(FRHICommandList* CmdList)` | 销毁命令列表 |
| `void Submit(FRHICommandList* CmdList, ERHICommandListType Type = Graphics, FRHISemaphore* const* WaitSemaphores = nullptr, uint32_t WaitCount = 0, FRHISemaphore* const* SignalSemaphores = nullptr, uint32_t SignalCount = 0, FRHIFence* SignalFence = nullptr)` | 把已录制命令列表提交到匹配类型的队列（compute/transfer 无专有队列时经 `IsNativeFallback` 回退到 graphics）；仅执行提交本身 |
| `void EnqueueTask(FRHICommandList* CmdList, std::function<void(FRHICommandList*)> Task)` | 在线程池 worker 上并行录制：内部 `Begin → Task → End`；回调收到**自己的**命令列表——Vulkan 禁止并发录进同一缓冲，跨任务共享是错误 |
| `void Flush()` | barrier：等此前所有 `EnqueueTask` 录制完；`Submit` 前调用保证 record-all → submit-all 顺序 |

#### 接口（帧原语，服务线程内调用）

| 签名 | 说明 |
|------|------|
| `void BeginFrame()` | 帧开始 |
| `void EndFrame()` | 帧结束：收尾帧命令缓冲 + 提交 + present |
| `void Resize(int Width, int Height)` | 帧缓冲尺寸变更（内部重建 swapchain） |
| `[[nodiscard]] FRHICommandList* GetFrameCommandList()` | 借帧命令缓冲为录制面（已 Begin，EndFrame 结束/提交）；**非拥有**，不要对其 Begin/End |
| `void PresentTexture(FRHITexture* Src)` | 把离屏纹理 blit 到当前 swapchain 后缓冲；所有场景录制后、EndFrame 前调用 |
| `[[nodiscard]] ERHIFormat GetSwapchainFormat() const` | 当前 swapchain 图像格式（离屏场景目标必须匹配才能 blit） |

#### 接口（同步）

| 签名 | 说明 |
|------|------|
| `[[nodiscard]] FRHIFence* CreateFence(bool bSignaled)` | 创建 fence（初始 signaled/unsignaled） |
| `void DestroyFence(FRHIFence* Fence)` | 销毁 fence |
| `void WaitForFence(FRHIFence* Fence, uint64_t TimeoutNs = max)` | 阻塞等 fence（GPU 同步；超时上限默认无限） |
| `[[nodiscard]] bool IsFenceSignaled(FRHIFence* Fence)` | 非阻塞查询（vkGetFenceStatus） |
| `[[nodiscard]] FRHISemaphore* CreateGpuSemaphore()` | 创建 GPU semaphore |
| `void DestroyGpuSemaphore(FRHISemaphore* Semaphore)` | 销毁 semaphore |

#### 接口（资源工厂）→ 见「RHIResources.h 资源工厂表」

| 签名 | 说明 |
|------|------|
| `CreateBuffer / CreateTexture / CreateSampler / CreateShaderModule / CreateGraphicsPipeline / CreateComputePipeline / CreateStructuredBuffer / CreateBufferView / CreateTextureView / CreateDescriptorSetLayout / CreatePipelineLayout / CreateDescriptorPool / CreateRenderPass / CreateFramebuffer`（各带 `const <Desc>&`）+ 对应 `Destroy*` | 内部资源工厂——Create/Destroy 一一配对，见下表 |
| `[[nodiscard]] FRHIDescriptorSet* AllocateDescriptorSet(FRHIDescriptorPool* Pool, FRHIDescriptorSetLayout* Layout)` | 从池分配描述符集 |
| `void FreeDescriptorSet(FRHIDescriptorPool* Pool, FRHIDescriptorSet* Set)` | 释放描述符集回池 |

#### 接口（swapchain 尺寸）

| 签名 | 说明 |
|------|------|
| `[[nodiscard]] uint32_t GetFramebufferWidth() const` | swapchain 帧缓冲宽（场景目标须匹配） |
| `[[nodiscard]] uint32_t GetFramebufferHeight() const` | swapchain 帧缓冲高（场景目标须匹配） |

#### 接口（GPU 查询）

| 签名 | 说明 |
|------|------|
| `[[nodiscard]] FRHIQueryPool* CreateQueryPool(ERHIQueryType Type, uint32_t QueryCount)` | 创建查询池（occlusion / timestamp） |
| `void DestroyQueryPool(FRHIQueryPool* Pool)` | 销毁查询池 |
| `bool GetQueryPoolResults(FRHIQueryPool* Pool, uint32_t FirstQuery, uint32_t QueryCount, uint64_t* Results, size_t Stride, bool bWait = true)` | 拷回查询结果到 CPU 内存；`bWait` 阻塞等可用（同步读——RHI 线程外调用） |

#### 接口（光追）

| 签名 | 说明 |
|------|------|
| `[[nodiscard]] FRHIRayTracingPipeline* CreateRayTracingPipeline(const FRHIRayTracingPipelineDesc& Desc)` | 创建光追管线 |
| `void DestroyRayTracingPipeline(FRHIRayTracingPipeline* Pipeline)` | 销毁光追管线 |
| `[[nodiscard]] FRHIAccelerationStructure* CreateAccelerationStructure(const FRHIRayTracingGeometryDesc& Desc)` | 创建加速结构（BLAS/TLAS）；设备不支持光追返回 nullptr；结构稍后经 `FRHICommandList::BuildAccelerationStructure` 构建 |
| `void DestroyAccelerationStructure(FRHIAccelerationStructure* Accel)` | 销毁加速结构 |
| `bool GetAccelerationStructureBuildSizes(const FRHIRayTracingGeometryDesc& Desc, uint64_t& OutAccelSize, uint64_t& OutScratchSize)` | 查询几何描述所需的 accel + scratch 设备尺寸 |
| `[[nodiscard]] FRHIBuffer* CreateShaderBindingTable(FRHIRayTracingPipeline* Pipeline, const FRHISbtGroup* Groups, uint32_t GroupCount, uint32_t* OutRayGenOffset = nullptr, uint32_t* OutRayGenStride = nullptr, uint32_t* OutHitOffset = nullptr, uint32_t* OutHitStride = nullptr, uint32_t* OutMissOffset = nullptr, uint32_t* OutMissStride = nullptr)` | 由管线的 stage groups 建 SBT；返回 GPU 布局的不透明句柄（提交给 `FRHIRayTracingSbt`），缓冲为 DeviceAddress + Storage 标志 |

### FRHI <class（渲染服务器）>

`FRHI final : FThreadedServer, IRHI`——渲染服务器，**不是调度层**：命令录制并行（线程池 `EnqueueTask`），队列提交与帧原语是直接调用，由调用方（RDG）保持串行。设备（私有的 `IDynamicRHI`）留在本 DLL 内——高层（RDG / render 插件）永不触碰具体后端类型。全部 `IRHI` 方法转发到私有后端，另加两个生命周期入口。

#### 接口

| 签名 | 说明 |
|------|------|
| `FRHI()` | 默认构造 |
| `virtual ~FRHI() override` | 析构（FThreadedServer 自动 Shutdown） |
| `bool Initialize(void* NativeWindowHandle, int Width, int Height, ERHIBackend Backend = ERHIBackend::Vulkan)` | 启动渲染服务线程 + 初始化设备；headless（null 窗口句柄）直接返回 true 跳过 |
| `void ShutdownRHI()` | 先停服务线程再拆设备（幂等） |

### ERHIBackend <enum class>

后端选择枚举。当前仅一个成员。

#### 约束

| 签名 | 说明 |
|------|------|
| `Vulkan = 0` | Vulkan 后端 |

## RHICommandList.h

### FRHIRenderingAttachmentInfo <struct>

动态渲染（BeginRendering）的颜色 / 深度附件描述。

#### 成员变量

| 字段 | 类型 | 说明 |
|------|------|------|
| `View` | `FRHITextureView*` | 附件视图（默认 nullptr） |
| `LoadOp` | `ERHILoadOp` | 载入操作（默认 Clear） |
| `StoreOp` | `ERHIStoreOp` | 存储操作（默认 Store） |
| `ClearColor[4]` | `float[4]` | 清屏颜色（默认 0,0,0,1） |

### FRHIQueue <class（逻辑提交端点）>

Graphics / Compute / Transfer 三个逻辑端点**总是存在**。Transfer/Compute 在设备无专有队列族时映射到 graphics 本族——`IsNativeFallback()` 报告该情况（调试/日志用）。

#### 接口

| 签名 | 说明 |
|------|------|
| `[[nodiscard]] ERHIQueueType GetType() const` | 队列类型 |
| `[[nodiscard]] bool IsNativeFallback() const` | 逻辑 Transfer/Compute 是否共享非专有原生队列（默认 false，调试/日志用） |
| `void Submit(FRHICommandList* const* CmdLists, uint32_t Count, FRHISemaphore* const* WaitSemaphores, uint32_t WaitCount, FRHISemaphore* const* SignalSemaphores, uint32_t SignalCount, FRHIFence* SignalFence)` | 一次性提交 1+ 命令列表 + 可选 wait/signal semaphore 链 + 可选信号 fence |

### FRHICommandList <class（命令录制面）>

命令录制面（≈ VkCommandBuffer）。能力取决于 `GetType()`，非法调用 Debug 下断言。**记录式**：`Begin` 后录制、`End` 提交；`UpdateBuffer` / `UpdateDescriptorSets` 也是录制语义（在 `EnqueueTask` 内执行）。

#### 接口（录制控制 / 类型）

| 签名 | 说明 |
|------|------|
| `[[nodiscard]] ERHICommandListType GetType() const` | 命令列表类型 |
| `void Begin()` | 开始录制 |
| `void End()` | 结束录制 |

#### 接口（Transfer / Barrier）

| 签名 | 说明 |
|------|------|
| `void CopyBuffer(FRHIBuffer* Src, uint64_t SrcOffset, FRHIBuffer* Dst, uint64_t DstOffset, uint64_t Size)` | 缓冲区间拷贝 |
| `void CopyBufferToTexture(FRHIBuffer* Src, FRHITexture* Dst, uint64_t SrcOffset)` | buffer → texture 拷贝 |
| `void CopyTextureToBuffer(FRHITexture* Src, FRHIBuffer* Dst, uint64_t DstOffset)` | texture → buffer 拷贝 |
| `void FillBuffer(FRHIBuffer* Buffer, uint64_t Offset, uint64_t Size, uint32_t Data)` | 填充缓冲 |
| `void UpdateBuffer(FRHIBuffer* Buffer, uint64_t Offset, uint64_t Size, const void* Data)` | 上传 CPU 数据：host-visible 缓冲直接写；device-local 走 staging 拷贝 |
| `void UpdateDescriptorSets(const FRHIDescriptorWrite* Writes, uint32_t Count)` | 更新描述符（记录式——实际 vkUpdateDescriptorSets，立即 CPU 操作） |
| `void TransitionBuffer(FRHIBuffer* Buffer, ERHIResourceState OldState, ERHIResourceState NewState)` | 缓冲状态 / 屏障转换 |
| `void TransitionTexture(FRHITexture* Texture, ERHIResourceState OldState, ERHIResourceState NewState)` | 纹理状态 / 屏障转换 |

#### 接口（Graphics）

| 签名 | 说明 |
|------|------|
| `void BeginRenderPass(FRHIRenderPass* RenderPass, FRHIFramebuffer* Framebuffer, uint32_t Width, uint32_t Height, const float ClearColor[4], bool bHasDepthStencil = false, float DepthClear = 1.0f, uint32_t StencilClear = 0)` | 传统 render pass 开始 |
| `void EndRenderPass()` | 结束 render pass |
| `void BeginRendering(const FRHIRenderingAttachmentInfo* ColorAttachments, uint32_t ColorCount, const FRHIRenderingAttachmentInfo* DepthAttachment, uint32_t Width, uint32_t Height)` | Vulkan dynamic rendering 开始 |
| `void EndRendering()` | 结束 dynamic rendering |
| `void SetViewport(float X, float Y, float Width, float Height, float MinDepth = 0.0f, float MaxDepth = 1.0f)` | 设置视口（dynamic state） |
| `void SetScissor(int32_t X, int32_t Y, uint32_t Width, uint32_t Height)` | 设置裁剪矩形（dynamic state） |
| `void BindGraphicsPipeline(FRHIGraphicsPipeline* Pipeline)` | 绑定图形管线 |
| `void BindVertexBuffer(uint32_t Binding, FRHIBuffer* Buffer, uint64_t Offset = 0)` | 绑定顶点缓冲 |
| `void BindIndexBuffer(FRHIBuffer* Buffer, uint64_t Offset = 0, bool bIndex32 = true)` | 绑定索引缓冲（32/16 位） |
| `void Draw(uint32_t VertexCount, uint32_t InstanceCount = 1, uint32_t FirstVertex = 0, uint32_t FirstInstance = 0)` | 顶点绘制 |
| `void DrawIndexed(uint32_t IndexCount, uint32_t InstanceCount = 1, uint32_t FirstIndex = 0, int32_t VertexOffset = 0, uint32_t FirstInstance = 0)` | 索引绘制 |
| `void DrawIndirect(FRHIBuffer* ArgsBuffer, uint64_t ArgsOffset, uint32_t DrawCount = 1, uint32_t Stride = 0)` | GPU 驱动间接绘制 |
| `void DrawIndexedIndirect(FRHIBuffer* ArgsBuffer, uint64_t ArgsOffset, uint32_t DrawCount = 1, uint32_t Stride = 0)` | GPU 驱动间接索引绘制 |
| `void DrawIndirectCount(FRHIBuffer* ArgsBuffer, uint64_t ArgsOffset, FRHIBuffer* CountBuffer, uint64_t CountOffset, uint32_t MaxDrawCount, uint32_t Stride = 0)` | GPU 驱动 draw count（drawCount 来自 GPU 写入的缓冲） |
| `void DrawIndexedIndirectCount(FRHIBuffer* ArgsBuffer, uint64_t ArgsOffset, FRHIBuffer* CountBuffer, uint64_t CountOffset, uint32_t MaxDrawCount, uint32_t Stride = 0)` | 同上的索引版本 |

#### 接口（Compute）

| 签名 | 说明 |
|------|------|
| `void BindComputePipeline(FRHIComputePipeline* Pipeline)` | 绑定计算管线 |
| `void Dispatch(uint32_t GroupCountX, uint32_t GroupCountY, uint32_t GroupCountZ)` | 计算分发 |
| `void DispatchIndirect(FRHIBuffer* ArgsBuffer, uint64_t ArgsOffset)` | 间接计算分发（GPU 写参数） |

#### 接口（Graphics + Compute）

| 签名 | 说明 |
|------|------|
| `void BindDescriptorSets(uint32_t FirstSet, FRHIDescriptorSet* const* Sets, uint32_t Count)` | 绑定描述符集（bind point 由命令列表类型决定） |
| `void PushConstants(ERHIShaderStage Stages, uint32_t Offset, uint32_t Size, const void* Data)` | 推送常量 |

#### 接口（GPU 查询）

| 签名 | 说明 |
|------|------|
| `void BeginQuery(FRHIQueryPool* Pool, uint32_t QueryIndex)` | 开始查询（occlusion） |
| `void EndQuery(FRHIQueryPool* Pool, uint32_t QueryIndex)` | 结束查询 |
| `void WriteTimestamp(FRHIQueryPool* Pool, uint32_t QueryIndex)` | 写时间戳 |
| `void ResetQueryPool(FRHIQueryPool* Pool, uint32_t FirstQuery, uint32_t QueryCount)` | 重置查询池段 |

#### 接口（光追）

| 签名 | 说明 |
|------|------|
| `void BuildAccelerationStructure(FRHIAccelerationStructure* Accel, FRHIBuffer* ScratchBuffer, uint64_t ScratchOffset)` | 构建加速结构（BLAS/TLAS）；ScratchBuffer 须 DeviceAddress + Storage 且足够大 |
| `void CopyAccelerationStructure(FRHIAccelerationStructure* Dst, FRHIAccelerationStructure* Src)` | 拷贝加速结构（紧凑 / refit 结果） |
| `void TraceRays(FRHIRayTracingPipeline* Pipeline, const FRHIRayTracingSbt& Sbt, uint32_t Width, uint32_t Height, uint32_t Depth = 1)` | 发射光线；Sbt 由 `CreateShaderBindingTable` 填充 |

`FRHIRayTracingSbt`（内嵌 struct）成员：`SbtBuffer`（整块 SBT 缓冲）、`RayGenOffset/RayGenStride`、`HitOffset/HitStride`、`MissOffset/MissStride`。

## RHIEnums.h

全部 RHI 枚举 + 位运算模板，一行一个。

| 枚举 | 成员 |
|------|------|
| `ERHIBackend` | Vulkan |
| `ERHIQueueType` | Graphics, Compute, Transfer |
| `ERHICommandListType` | Graphics, Compute, Transfer |
| `ERHIResourceType` | Unknown, Buffer, StructuredBuffer, BufferView, Texture, TextureView, Sampler, ShaderModule, DescriptorSetLayout, PipelineLayout, GraphicsPipeline, ComputePipeline, RayTracingPipeline, DescriptorPool, DescriptorSet, Framebuffer, RenderPass, CommandPool, Fence, Semaphore, QueryPool, AccelerationStructure |
| `ERHIFormat` | Unknown, R8G8B8A8_UNORM, B8G8R8A8_UNORM, B8G8R8A8_SRGB, R32_SFLOAT, R32G32_SFLOAT, R32G32B32_SFLOAT, R16G16_SFLOAT, D24_UNORM_S8_UINT, D32_SFLOAT, R8G8B8A8_SRGB, R16G16B16A16_SFLOAT, R32G32B32A32_SFLOAT, R8_UNORM, R8G8_UNORM, R8G8B8_UNORM, R16_SFLOAT |
| `ERHITextureDimension` | Tex2D, Tex2DArray, Cube, Tex3D |
| `ERHIBufferUsage`（位掩码） | None, Vertex, Index, Uniform, Storage, TransferSrc, TransferDst, Indirect, DeviceAddress（shader 寻址）, AccelerationStructure（BLAS/TLAS 存储） |
| `ERHITextureUsage`（位掩码） | None, Sampled, ColorAttachment, DepthStencil, Storage, TransferSrc, TransferDst, Transient |
| `ERHIMemoryUsage` | GPUOnly, CPUToGPU, GPUToCPU, CPUOnly |
| `ERHIResourceState` | Common, VertexBuffer, IndexBuffer, UniformBuffer, ShaderResource, UnorderedAccess, IndirectArgument, RenderTarget, DepthWrite, CopySrc, CopyDst, Present |
| `ERHIQueryType` | Occlusion（二元遮挡）, Timestamp（GPU 时间戳） |
| `ERHIDescriptorType` | Sampler, CombinedImageSampler, SampledImage, StorageImage, UniformBuffer, StorageBuffer, DynamicUniform, DynamicStorage, AccelerationStructure（只读 AS 绑定） |
| `ERHIShaderStage`（位掩码） | None, Vertex, Fragment, Compute, RayGen, AnyHit, ClosestHit, Miss, Intersection, Callable, AllGraphics（Vertex\|Fragment）, AllRayTracing（六光追级） |
| `ERHIPrimitiveTopology` | TriangleList, TriangleStrip, LineList, PointList |
| `ERHICullMode` | None, Front, Back |
| `ERHIFillMode` | Solid, Wireframe |
| `ERHICompareOp` | Never, Less, Equal, LessOrEqual, Greater, NotEqual, GreaterOrEqual, Always |
| `ERHIBlendFactor` | Zero, One, SrcColor, OneMinusSrcColor, DstColor, OneMinusDstColor, SrcAlpha, OneMinusSrcAlpha, DstAlpha, OneMinusDstAlpha |
| `ERHIBlendOp` | Add, Subtract, ReverseSubtract, Min, Max |
| `ERHILoadOp` | Load, Clear, DontCare |
| `ERHIStoreOp` | Store, DontCare |
| `ERHIFilter` | Nearest, Linear |
| `ERHIAddressMode` | Repeat, MirroredRepeat, ClampToEdge, ClampToBorder |

#### 位运算模板 / 运算符

| 签名 | 说明 |
|------|------|
| `constexpr TEnum RHIEnumOr(TEnum A, TEnum B)` | 按底层整数位或（通用模板） |
| `constexpr bool RHIEnumHas(TEnum Mask, TEnum Flag)` | 掩码是否含标志位（通用模板） |
| `operator\|(ERHIBufferUsage, ERHIBufferUsage)` / `(ERHITextureUsage, ERHITextureUsage)` / `(ERHIShaderStage, ERHIShaderStage)` | 三个位掩码枚举的 `\|` 运算符 |

## RHIResources.h

### FRHIResource <class（资源基类）>

全部 GPU 资源句柄的抽象基类。**不可拷贝**（GPU 资源按句柄所有），每类实现 `GetTypeName` / `GetType`；`GetDebugName` 取回调试名。基类保护成员：`DebugName`、`RefCount`（当前恒 1）。

#### 接口

| 签名 | 说明 |
|------|------|
| `virtual ~FRHIResource()` | 虚析构 |
| `[[nodiscard]] virtual const char* GetTypeName() const` | 类型名（"FRHIBuffer" 等） |
| `[[nodiscard]] virtual ERHIResourceType GetType() const` | 资源类型枚举 |
| `[[nodiscard]] const std::string& GetDebugName() const` | 调试名 |
| `FRHIResource(const FRHIResource&) = delete` / `operator=` deleted | 不可拷贝 |
| `FRHIResource() protected` | 仅派生可构造 |

### IDynamicRHIMemoryAllocator <class>

后端内存分配器的公开抽象（VMA 的薄封装，资源句柄用它 Map/Unmap）。分配所有权归资源句柄——句柄析构时经它释放。

#### 接口

| 签名 | 说明 |
|------|------|
| `virtual void Free(FRHIMemoryAllocation& Alloc)` | 释放分配（原生句柄置空） |
| `virtual void* Map(FRHIMemoryAllocation& Alloc)` | 映射为 CPU 可写指针（持久映射缓存） |
| `virtual void Unmap(FRHIMemoryAllocation& Alloc)` | 解除映射 |

`FRHIMemoryAllocation` 成员：`Native`（原生句柄，不透明）、`Mapped`（当前映射指针，可空）。

### 资源句柄族 <class>

每个 GPU 资源一个句柄类：`FRHIBuffer / FRHIStructuredBuffer / FRHIBufferView / FRHITexture / FRHITextureView / FRHISampler / FRHIShaderModule / FRHIDescriptorSetLayout / FRHIPipelineLayout / FRHIGraphicsPipeline / FRHIComputePipeline / FRHIRayTracingPipeline / FRHIDescriptorPool / FRHIDescriptorSet / FRHIFramebuffer / FRHIRenderPass / FRHICommandPool / FRHIFence / FRHISemaphore / FRHIQueryPool / FRHIAccelerationStructure`。均继承 `FRHIResource`，各自实现 `GetTypeName` / `GetType`；多数是**纯句柄**（无附加接口），下列为带附加接口者。

#### 附加接口

| 签名 | 说明 |
|------|------|
| `FRHIBuffer::GetDesc()` → `const FRHIBufferDesc&` | 缓冲描述 |
| `FRHIBuffer::GetDeviceAddress()` → `uint64_t` | GPU 设备地址（Desc 带 DeviceAddress 时非 0，否则 0） |
| `FRHIStructuredBuffer::GetDesc()` / `GetUnderlyingBuffer()` → `FRHIBuffer*` | 结构化缓冲描述 + 底层缓冲 |
| `FRHITexture::GetDesc()` → `const FRHITextureDesc&` | 纹理描述 |
| `FRHISampler::GetDesc()` → `const FRHISamplerDesc&` | 采样器描述 |
| `FRHIAccelerationStructure::GetGeometryDesc()` → `const FRHIRayTracingGeometryDesc&` | 加速结构几何描述（受保护成员 GeometryDesc） |

### Desc 结构体族 <struct>

全部资源创建描述。取默认即安全创建；带 `operator==` / `!=` 的可比较描述：`FRHIBufferDesc`、`FRHIExtent3D`、`FRHITextureDesc`。

| 结构体 | 关键成员 |
|------|------|
| `FRHIBufferDesc` | Size, Usage(ERHIBufferUsage), MemoryUsage(ERHIMemoryUsage) |
| `FRHIStructuredBufferDesc` | Size, Stride, ElementCount, Usage(默认 Storage), MemoryUsage |
| `FRHIBufferViewDesc` | Buffer, Offset, Range, Format, Stride |
| `FRHIExtent3D` | Width, Height, Depth（默认全 1） |
| `FRHITextureDesc` | Format, Dimension, Extent, MipLevels, ArrayLayers, Usage, MemoryUsage |
| `FRHITextureViewDesc` | Texture, Format, BaseMip, MipCount, BaseArrayLayer, ArrayLayerCount |
| `FRHISamplerDesc` | MinFilter, MagFilter, AddressU/V/W, LodBias, MinLod, MaxLod |
| `FRHIShaderModuleDesc` | Stage, Bytecode, BytecodeSize, EntryPoint("main") |
| `FRHIDescriptorBinding` | Binding, Type, Count, Stages, bPartiallyBound, bVariableCount |
| `FRHIDescriptorSetLayoutDesc` | Bindings（vector） |
| `FRHIPushConstantRange` | Stages, Offset, Size |
| `FRHIPipelineLayoutDesc` | SetLayouts, PushConstants（vector） |
| `FRHIVertexAttribute` | Location, Format, Offset |
| `FRHIAttachmentBlend` | bBlend, SrcColorFactor, DstColorFactor, SrcAlphaFactor, DstAlphaFactor, ColorOp, AlphaOp |
| `FRHIGraphicsPipelineDesc` | VertexShader, FragmentShader, VertexEntryPoint, FragmentEntryPoint, Layout, RenderPass, Topology, VertexStride, Attributes, CullMode, FillMode, ColorFormat(B8G8R8A8_UNORM), DepthFormat, SampleCount, bDepthTest, bDepthWrite, DepthCompare, AttachmentBlends, bAlphaToCoverage |
| `FRHIComputePipelineDesc` | ComputeShader, ComputeEntryPoint, Layout |
| `FRHIRayTracingPipelineDesc` | RayGen, Miss, ClosestHit, AnyHit, Intersection, Callable, EntryPoints, Layout, MaxRecursionDepth |
| `FRHIDescriptorPoolSize` | Type, Count |
| `FRHIDescriptorPoolDesc` | MaxSets, PoolSizes, bUpdateAfterBind |
| `FRHIRenderPassAttachment` | Format, SampleCount, LoadOp, StoreOp |
| `FRHIRenderPassDesc` | ColorAttachments, DepthFormat, SampleCount |
| `FRHIFramebufferDesc` | RenderPass, Attachments, Width, Height |
| `FRHIRayTracingStructureType` | TopLevel, BottomLevel |
| `FRHIRayTracingGeometry` | VertexBuffer(+Offset/Count/Stride=12), IndexBuffer(+Offset/Count), bIndex32, bOpaque |
| `FRHIRayTracingGeometryDesc` | Geometries, bAllowUpdate(BLAS refit), bAllowCompaction |
| `FRHIRayTracingInstance` | InstanceId, InstanceMask(0xFF), SbtOffset, AccelerationStructure, Transform[12]（行主序转置世界） |
| `FRHISbtRecord` | Module（null → miss/空记录）, EntryPoint("main") |
| `FRHISbtGroup` | Stage, Records（rayGen/miss/callable: 记录；hit: 每命中记录） |
| `FRHIDescriptorWrite` | Set, Binding, ArrayIndex, Type, Buffer, Offset, Range, TextureView, Sampler |

### 资源工厂表

`IRHI` / `FRHI` 上所有 Create/Destroy 对，后端内部实现。

| Create | Destroy | 描述 |
|--------|---------|------|
| `CreateBuffer(const FRHIBufferDesc&)` | `DestroyBuffer(FRHIBuffer*)` | 缓冲 |
| `CreateStructuredBuffer(const FRHIStructuredBufferDesc&)` | `DestroyStructuredBuffer(FRHIStructuredBuffer*)` | 结构化缓冲（销毁时连带底层缓冲） |
| `CreateBufferView(const FRHIBufferViewDesc&)` | `DestroyBufferView(FRHIBufferView*)` | 缓冲视图 |
| `CreateTexture(const FRHITextureDesc&)` | `DestroyTexture(FRHITexture*)` | 纹理 |
| `CreateTextureView(const FRHITextureViewDesc&)` | `DestroyTextureView(FRHITextureView*)` | 纹理视图 |
| `CreateSampler(const FRHISamplerDesc&)` | `DestroySampler(FRHISampler*)` | 采样器 |
| `CreateShaderModule(const FRHIShaderModuleDesc&)` | `DestroyShaderModule(FRHIShaderModule*)` | 着色器模块 |
| `CreateGraphicsPipeline(const FRHIGraphicsPipelineDesc&)` | `DestroyGraphicsPipeline(FRHIGraphicsPipeline*)` | 图形管线 |
| `CreateComputePipeline(const FRHIComputePipelineDesc&)` | `DestroyComputePipeline(FRHIComputePipeline*)` | 计算管线 |
| `CreateDescriptorSetLayout(const FRHIDescriptorSetLayoutDesc&)` | `DestroyDescriptorSetLayout(FRHIDescriptorSetLayout*)` | 描述符集布局 |
| `CreatePipelineLayout(const FRHIPipelineLayoutDesc&)` | `DestroyPipelineLayout(FRHIPipelineLayout*)` | 管线布局 |
| `CreateDescriptorPool(const FRHIDescriptorPoolDesc&)` | `DestroyDescriptorPool(FRHIDescriptorPool*)` | 描述符池 |
| `AllocateDescriptorSet(FRHIDescriptorPool*, FRHIDescriptorSetLayout*)` | `FreeDescriptorSet(FRHIDescriptorPool*, FRHIDescriptorSet*)` | 描述符集（池内分配/释放） |
| `CreateRenderPass(const FRHIRenderPassDesc&)` | `DestroyRenderPass(FRHIRenderPass*)` | render pass |
| `CreateFramebuffer(const FRHIFramebufferDesc&)` | `DestroyFramebuffer(FRHIFramebuffer*)` | framebuffer |
| `CreateFence(bool bSignaled)` | `DestroyFence(FRHIFence*)` | fence |
| `CreateGpuSemaphore()` | `DestroyGpuSemaphore(FRHISemaphore*)` | GPU semaphore |
| `CreateQueryPool(ERHIQueryType, uint32_t)` | `DestroyQueryPool(FRHIQueryPool*)` | 查询池 |
| `CreateRayTracingPipeline(const FRHIRayTracingPipelineDesc&)` | `DestroyRayTracingPipeline(FRHIRayTracingPipeline*)` | 光追管线 |
| `CreateAccelerationStructure(const FRHIRayTracingGeometryDesc&)` | `DestroyAccelerationStructure(FRHIAccelerationStructure*)` | 加速结构 |
| `CreateShaderBindingTable(...)` | `DestroyBuffer(FRHIBuffer*)`（返回即缓冲） | SBT 缓冲 |

- [RHI.md](RHI.md) — 概念 · [实现字典](ImplAPI.md) — 算法
