# RHI（Private）— 实现算法字典

cpp 侧每个关键函数的算法伪代码解释。Public 侧 API 文档通过 `#fn-...` 锚点跳转落到这里。RHI 是单 codegen TU（unity build）：`RHI.cpp` 顶部 `#define VMA_IMPLEMENTATION` 后 `#include` 其余四个 Vulkan cpp。

## RHI.cpp

RHI 插件的宿主 TU：`FRHI`（公共服务器外壳）实现 + 其余 Vulkan cpp 折叠于此。`FRHI` 几乎全部方法都是「null 检查 → 转发给私有 `RHI`（IDynamicRHI）」。

<a id="fn-rhi-initialize"></a>
### FRHI::Initialize(void* NativeWindowHandle, int Width, int Height, ERHIBackend Backend)

← [公开 API](API.md) · `bool`

启动渲染服务器：headless 直通、创建后端设备、初始化设备、再启动服务线程。

```text
Initialize(Handle, W, H, Backend):
1. if Handle == nullptr: return true          // headless：跳过 RHI
2. if W <= 0 || H <= 0: log error; return false
3. RHI = FRHIFactory::Create(Backend)        // 私有设备（Vulkan）
4. if !RHI: return false
5. Desc = { Backend, Handle, W, H }
6. if !RHI->Initialize(Desc): RHI.reset(); return false
7. FThreadedServer::Initialize()             // 启动渲染服务线程
8. return true
```

<a id="fn-rhi-shutdown"></a>
### FRHI::ShutdownRHI()

← [公开 API](API.md) · `void`

拆设备 + 停线程，幂等。

```text
ShutdownRHI():
1. FThreadedServer::Shutdown()               // 先停服务线程
2. if RHI: RHI->Shutdown(); RHI.reset()
```

<a id="fn-rhi-enqueue"></a>
### FRHI::EnqueueTask(FRHICommandList* CmdList, std::function<void(FRHICommandList*)> Task)

← [公开 API](API.md) · `void`

并行命令录制：整段 `Begin → Task → End` 提交给录制线程池。每个任务拥有自己的命令列表（Vulkan 禁止并发录进同一缓冲）。

```text
EnqueueTask(CmdList, Task):
1. if CmdList == nullptr || !Task: return
2. RecordingPool.Submit([CmdList, Task]:
       CmdList->Begin(); Task(CmdList); CmdList->End())
```

<a id="fn-rhi-flush"></a>
### FRHI::Flush()

← [公开 API](API.md) · `void`

录制屏障：委托线程池 barrier，保证所有 EnqueueTask 已录制完（record-all → submit-all）。

```text
Flush():
1. RecordingPool.Flush()
```

<a id="fn-rhi-submit"></a>
### FRHI::Submit(FRHICommandList*, ERHICommandListType, FRHISemaphore* const*, uint32_t, FRHISemaphore* const*, uint32_t, FRHIFence*)

← [公开 API](API.md) · `void`

直接队列提交，按命令列表类型路由；compute/transfer 在原生回退时提交到 graphics 队列与之串行化。

```text
Submit(CmdList, Type, WaitSems, WaitCount, SigSems, SigCount, SigFence):
1. if !RHI || CmdList == nullptr: return
2. switch Type:
     Compute:   Q = GetComputeQueue().IsNativeFallback() ? GraphicsQueue : ComputeQueue
     Transfer:  Q = GetTransferQueue().IsNativeFallback() ? GraphicsQueue : TransferQueue
     default:   Q = GraphicsQueue
3. Q.Submit(&CmdList, 1, WaitSems, WaitCount, SigSems, SigCount, SigFence)
```

## VulkanRHI.cpp

Vulkan 后端实现（`FVulkanRHI : IDynamicRHI`）。初始化是一条长链，每步失败即 `Shutdown()` 回滚。

<a id="fn-vk-init"></a>
### FVulkanRHI::Initialize(const FRHIInitDesc& Desc)

← [公开 API](API.md) · `bool`

链式初始化：实例 → 表面 → 物理设备 → 逻辑设备 → 内存分配器 → 逻辑队列/池 → swapchain → image views → render pass → framebuffers → 命令池/缓冲 → 同步对象。

```text
Initialize(Desc):
1. if bInitialized: return false
2. if Backend != Vulkan || Handle == null || W/H <= 0: return false
3. 依次执行并检查，任一失败 -> Shutdown(); return false:
   CreateInstance() → CreateSurface() → PickPhysicalDevice()
   → CreateLogicalDevice() → CreateMemoryAllocator()
   → CreateLogicalQueuesAndPools() → CreateSwapchain()
   → CreateImageViews() → CreateRenderPass() → CreateFramebuffers()
   → CreateCommandPoolAndBuffer() → CreateSyncObjects()
4. bInitialized = true
```

<a id="fn-vk-queues"></a>
### FVulkanRHI::FindQueueFamilies(VkPhysicalDevice)

内部 · `bool`

枚举队列族，为 graphics / present / compute / transfer 选族。compute/transfer 优先选**非 graphics 专用族**；找不到则回退 graphics 本族并置 `bNativeFallback`。

```text
FindQueueFamilies(PD):
1. for Index in 队列族:
     if 有 GRAPHICS 且未选: Graphics = Index
     if 支持 present 且未选: Present = Index
     if 有 COMPUTE 且无 GRAPHICS 且未选: Compute = Index; 专用
     if 有 TRANSFER 且无 GRAPHICS 且无 COMPUTE 且未选: Transfer = Index; 专用
2. if Graphics/Present 缺失: return false
3. if Compute 未选: Compute = Graphics; 回退
4. if Transfer 未选: Transfer = Graphics; 回退
5. 记录各族 + 回退标志; return true
```

<a id="fn-vk-pickdevice"></a>
### FVulkanRHI::PickPhysicalDevice()

内部 · `bool`

选设备：优先独立 GPU，其次集成 GPU，最后任意可用设备。可适格 = 队列族齐全 + 支持所需扩展 + surface 有格式和 present mode。

```text
PickPhysicalDevice():
1. 枚举物理设备；无 -> false
2. 遍历适格设备，选第一个 DISCRETE（优先），记第一个 INTEGRATED 为备用
3. 没有 discrete -> 用 integrated；都没有 -> 遍历取任意适格
4. 无适格 -> false
5. PhysicalDevice = 选中; FindQueueFamilies(PhysicalDevice)
```

<a id="fn-vk-logicaldev"></a>
### FVulkanRHI::CreateLogicalDevice()

内部 · `bool`

建逻辑设备：合并各族的请求队列数（graphics 2 个当 compute 共享本族且够用）、启用功能特性链（dynamic rendering → descriptor indexing → Vulkan 1.1/1.2 特性）、取回队列句柄，最后动态解析光追 KHR 函数指针并置 `bRayTracingSupported`。

```text
CreateLogicalDevice():
1. 统计各族所需队列数（graphics 至少 1；compute 同族且 queueCount>=2 时取第 2 个队列，否则独立族）
2. 构造 QueueCreateInfos（4 个优先级 1.0）
3. 特性：multiDrawIndirect / drawIndirectFirstInstance / fragment+vertex storesAndAtomics
   / dynamicIndexing / fillModeNonSolid / dynamicRendering
   / descriptorIndexing(partiallyBound, variableCount, updateAfterBind) / drawIndirectCount / bufferDeviceAddress
4. vkCreateDevice
5. vkGetDeviceQueue 取回 G/P/C/T 队列
6. vkGetDeviceProcAddr 解析 9 个 KHR 函数；全非空 => bRayTracingSupported = true
```

<a id="fn-vk-swapchain"></a>
### FVulkanRHI::CreateSwapchain()

内部 · `bool`

选格式（优先 B8G8R8A8_SRGB + SRGB 非线性）、选 present mode（r.VSync=0 时偏好 Mailbox → Immediate，否则 FIFO）、算 extent（窗口未映射时回退请求尺寸）、定图像数（min + r.Swapchain.ExtraImages，clamp 到 max）、建 swapchain 并取回图像。

```text
CreateSwapchain():
1. 查 surface formats；优先 B8G8R8A8_SRGB + SRGB_NONLINEAR，否则 Formats[0]
2. 查 present modes；VSync=0: Mailbox→Immediate；否则 FIFO
3. extent = currentExtent（UINT32_MAX 时用 FramebufferWidth/Height clamp 到 min/max；仍 0 则回退请求尺寸）
4. imageCount = min + max(0, ExtraImages)；clamp 到 maxImageCount
5. vkCreateSwapchainKHR（imageUsage = COLOR_ATTACHMENT | TRANSFER_DST）
6. 取回 SwapchainImages
```

<a id="fn-vk-beginend"></a>
### FVulkanRHI::BeginFrame() / EndFrame() / PresentTexture(FRHITexture* Src)

← [公开 API](API.md) · `void`

帧循环：BeginFrame 等上一帧 fence → reset → acquire 下一张 swapchain 图像（out-of-date 或 resize 时重建）→ 开帧命令缓冲；EndFrame 关帧命令缓冲 → 提交（等 image-available、信号 render-finished + fence）→ present（out-of-date/suboptimal 重建）；PresentTexture 在帧命令缓冲上把离屏纹理 blit 到当前后缓冲。

```text
BeginFrame():
1. vkWaitForFences(InFlightFence); vkResetFences
2. AcquireResult = vkAcquireNextImageKHR(→ CurrentImageIndex)
3. if OUT_OF_DATE || bFramebufferResized: RecreateSwapchain(); 重 acquire
4. FrameCommandListRHI->Begin()            // 让特性可借帧缓冲录制

EndFrame():
1. FrameCommandListRHI->End()
2. vkQueueSubmit(Graphics, [wait ImageAvailable, 1 cmd, signal RenderFinished], InFlightFence)
3. vkQueuePresentKHR(Present, [wait RenderFinished, CurrentImageIndex])
4. if OUT_OF_DATE || SUBOPTIMAL || resize: RecreateSwapchain()

PresentTexture(Src):
1. Transition Src → TRANSFER_SRC，Dst(后缓冲) → TRANSFER_DST
2. vkCmdBlitImage(Src → Dst, FILTER_LINEAR)
3. Transition Dst → PRESENT_SRC；Src → COLOR_ATTACHMENT（供下帧）
```

<a id="fn-vk-recreate"></a>
### FVulkanRHI::RecreateSwapchain()

内部 · `bool`

窗口 resize / swapchain 过期重建：等设备空闲 → 销毁旧 swapchain 资源 → 重建 swapchain + image views + framebuffers。

```text
RecreateSwapchain():
1. vkDeviceWaitIdle
2. DestroySwapchainResources()     // 释放非拥有 RHI 包装 + Vk 句柄
3. CreateSwapchain() → CreateImageViews() → CreateFramebuffers()
```

<a id="fn-vk-gfxpipe"></a>
### FVulkanRHI::CreateGraphicsPipeline(const FRHIGraphicsPipelineDesc& Desc)

← [公开 API](API.md) · `FRHIGraphicsPipeline*`

装配图形管线：shader stages（VS/FS + entry）、顶点输入（有 VertexStride 才建 binding）、图元拓扑、光栅化（cull/fill）、深度模板、颜色混合（AttachmentBlends 空则默认不混合）、动态 viewport/scissor；有 RenderPass 用传统 pass，否则走 dynamic rendering pNext。

```text
CreateGraphicsPipeline(Desc):
1. if 缺 VS/FS 或句柄无效: return nullptr
2. RenderPass? VkRp = pass 句柄 : 填 pipeline-rendering pNext（ColorFormat/DepthFormat）
3. Layout? VkPLayout = layout 句柄
4. 装配：ShaderStages / VertexInput（stride + attributes）/ InputAssembly
   / ViewportState(动态) / Rasterizer / Multisample / DepthStencil / ColorBlend / DynamicState
5. vkCreateGraphicsPipelines；成功 -> new FVulkanGraphicsPipeline
```

<a id="fn-vk-accel"></a>
### FVulkanRHI::CreateAccelerationStructure(const FRHIRayTracingGeometryDesc& Desc)

← [公开 API](API.md) · `FRHIAccelerationStructure*`

建 BLAS：把几何列表转成 Vk 几何 + primitive counts，`GetAccelerationStructureBuildSizesKHR` 查尺寸，建 storage 缓冲（GPU-only、accel-storage + device-address），`CreateAccelerationStructureKHR` 建对象。设备无光追或几何为空 → nullptr。

```text
CreateAccelerationStructure(Desc):
1. if 无光追或几何为空: return nullptr
2. 转 VkAccelerationStructureGeometryKHR[]（三角形，opaque 标志）
3. primitiveCounts = IndexCount/3 或 VertexCount/3
4. GetAccelerationStructureBuildSizesKHR → Sizes
5. 建 storage 缓冲（GPUOnly，ACCELERATION_STRUCTURE_STORAGE | SHADER_DEVICE_ADDRESS），取设备地址
6. CreateAccelerationStructureKHR(缓冲, BLAS)
7. return new FVulkanAccelerationStructure(...)
```

<a id="fn-vk-sbt"></a>
### FVulkanRHI::CreateShaderBindingTable(FRHIRayTracingPipeline*, const FRHISbtGroup*, uint32_t, uint32_t*..., uint32_t*...)

← [公开 API](API.md) · `FRHIBuffer*`

由管线的 stage groups 生成 SBT 缓冲：统计 raygen/miss/hit 记录数 → 算每段 offset/stride（句柄 32B 对齐 16）→ 建 CPUToGPU + DeviceAddress 缓冲 → `GetRayTracingShaderGroupHandlesKHR` 取全部管线组句柄 → 按 raygen→miss→hit 顺序把句柄写进映射内存。回传各段 offset/stride。

```text
CreateShaderBindingTable(Pipeline, Groups, GroupCount, 各 out):
1. if 无光追或参数无效: return nullptr
2. 统计 RayGenCount / MissCount / HitCount（按 Groups[].Stage）
3. HandleAligned = 32 对齐 16；offset: raygen=0, miss=raygen*N, hit=miss+miss*N
4. 建缓冲（CPUToGPU，DeviceAddress | TransferDst）→ Map
5. GetRayTracingShaderGroupHandlesKHR → Handles[]（每组 32B）
6. 按 raygen(1) → miss(N) → hit(N) 顺序 memcpy 句柄到映射内存；Unmap
7. 回填各 out offset/stride；return Sbt 缓冲
```

<a id="fn-vk-factory"></a>
### FRHIFactory::Create(ERHIBackend Backend)

← [公开 API](API.md) · `IDynamicRHI*`

按后端创建设备。当前仅 Vulkan。

```text
Create(Backend):
1. switch Backend:
     Vulkan: return new FVulkanRHI()
2. log error; return nullptr
```

## VulkanCommandList.cpp

`FVulkanCommandList`（≈ VkCommandBuffer 的 RHI 包装）。录制方法几乎都是「断言类型 → 取 Vk 句柄 → vkCmdXxx」的直接映射；下列为带算法的。

<a id="fn-cmd-beginend"></a>
### FVulkanCommandList::Begin() / End()

← [公开 API](API.md) · `void`

`Begin`：reset + begin 命令缓冲（ONE_TIME_SUBMIT），置录制位。`End`：end 命令缓冲，清录制位。

```text
Begin():
1. vkResetCommandBuffer; vkBeginCommandBuffer(ONE_TIME_SUBMIT)
2. bRecording = true

End():
1. vkEndCommandBuffer; bRecording = false
```

<a id="fn-cmd-updatebuffer"></a>
### FVulkanCommandList::UpdateBuffer(FRHIBuffer*, uint64_t Offset, uint64_t Size, const void* Data)

← [公开 API](API.md) · `void`

上传 CPU 数据：host-visible 缓冲直接 map+memcpy（无 GPU 工作）；device-local 缓冲建 staging + 双屏障 + vkCmdCopyBuffer。

```text
UpdateBuffer(Buffer, Offset, Size, Data):
1. if 任一无效: return
2. Map(缓冲) 成功? -> memcpy(映射+Offset, Data); Unmap; return
3. // device-local 路径
4. 建 staging 缓冲（CPUToGPU, TRANSFER_SRC）→ Map+memcpy+Unmap
5. Barrier: ALL_COMMANDS -> TRANSFER（dstAccess=TRANSFER_WRITE）
6. vkCmdCopyBuffer(Staging -> Buffer, Offset)
7. Barrier: TRANSFER -> ALL_COMMANDS（dstAccess=SHADER_READ|VERTEX_ATTRIBUTE_READ）
8. 销毁 staging
```

<a id="fn-cmd-updatedesc"></a>
### FVulkanCommandList::UpdateDescriptorSets(const FRHIDescriptorWrite* Writes, uint32_t Count)

← [公开 API](API.md) · `void`

把 RHI 描述符写转为 VkWriteDescriptorSet（UBO → buffer info；combined image sampler → image info），一次性 vkUpdateDescriptorSets。记录式但实际立即 CPU 执行。

```text
UpdateDescriptorSets(Writes, Count):
1. if 无效: return
2. for W in Writes:
     跳过 Set 为空的
     按 W.Type 填 buffer/image info（Info 临时 vector，存活到调用结束）
3. vkUpdateDescriptorSets(全部 VkWrites)
```

<a id="fn-cmd-beginrendering"></a>
### FVulkanCommandList::BeginRendering(const FRHIRenderingAttachmentInfo*, uint32_t, const FRHIRenderingAttachmentInfo*, uint32_t, uint32_t)

← [公开 API](API.md) · `void`

Vulkan dynamic rendering：颜色附件数组 + 可选深度附件 → VkRenderingAttachmentInfo（image view / load-store op / clear 值）→ vkCmdBeginRendering。

```text
BeginRendering(ColorAttachments, ColorCount, DepthAttachment, W, H):
1. 断言 Graphics
2. for 颜色附件: 建 VkRenderingAttachmentInfo（COLOR_ATTACHMENT 布局，load/store 映射，clear 值）
3. 深度附件（若有）: 建 DepthAtt（DEPTH_STENCIL 布局，clear 深度）
4. vkCmdBeginRendering(renderArea = {W,H}, 1 层)
```

<a id="fn-cmd-buildaccel"></a>
### FVulkanCommandList::BuildAccelerationStructure(FRHIAccelerationStructure*, FRHIBuffer* ScratchBuffer, uint64_t ScratchOffset)

← [公开 API](API.md) · `void`

在命令缓冲上构建 BLAS：把 accel 的几何描述转 Vk 几何（顶点/索引走 device address + 偏移）+ build range → `CmdBuildAccelerationStructuresKHR`。

```text
BuildAccelerationStructure(Accel, Scratch, ScratchOffset):
1. if RT.BuildAccel 为 null: error; return
2. for Geometry in Accel->GetGeometryDesc().Geometries:
     转 VkGeometry（vertexData/vertexStride/maxVertex；indexData 可选）
     build range：primitiveCount = IndexCount/3 或 VertexCount/3
3. BuildInfo（BLAS, BUILD 模式, scratchData = Scratch->GetDeviceAddress() + ScratchOffset）
4. RT.BuildAccel(Buffer, 1, &BuildInfo, RangePtrs)
```

<a id="fn-cmd-tracerays"></a>
### FVulkanCommandList::TraceRays(FRHIRayTracingPipeline*, const FRHIRayTracingSbt&, uint32_t, uint32_t, uint32_t)

← [公开 API](API.md) · `void`

发射光线：把 Sbt 各段 offset/stride 转成 device-address 区域（raygen/miss/hit + 空 callable）→ `CmdTraceRaysKHR`。

```text
TraceRays(Pipeline, Sbt, W, H, D):
1. if RT.TraceRays 为 null: error; return
2. SbtAddress = Sbt.SbtBuffer->GetDeviceAddress()
3. RayGen/Miss/Hit 区域 = SbtAddress + offset, stride; Callable = 空
4. RT.TraceRays(Buffer, &RayGen, &Miss, &Hit, &Callable, W, H, D)
```

## VulkanMemory.cpp

`FVulkanMemoryAllocator`：VMA 的薄封装（`VMA_IMPLEMENTATION` 定义后编译 vk_mem_alloc）。

<a id="fn-mem-init"></a>
### FVulkanMemoryAllocator::Initialize(VkInstance, VkPhysicalDevice, VkDevice)

内部 · `bool`

建 VMA 分配器（外部同步标志，幂等）。

```text
Initialize(Instance, PD, Device):
1. if Allocator 已建: return true
2. vmaCreateAllocator(VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT)
3. 失败 -> Allocator = nullptr; return false
```

<a id="fn-mem-allocinfo"></a>
### FVulkanMemoryAllocator::MakeAllocationInfo(ERHIMemoryUsage)

内部 · `VmaAllocationCreateInfo`

把 RHI 内存用途映射为 VMA 分配提示。

```text
MakeAllocationInfo(MemoryUsage):
1. usage = VMA_MEMORY_USAGE_AUTO
2. switch:
     GPUOnly:   preferredFlags = DEVICE_LOCAL
     CPUToGPU:  HOST_ACCESS_SEQUENTIAL_WRITE | MAPPED
     GPUToCPU:  HOST_ACCESS_RANDOM | MAPPED
     CPUOnly:   HOST_ACCESS_RANDOM | MAPPED; requiredFlags = HOST_VISIBLE
```

<a id="fn-mem-map"></a>
### FVulkanMemoryAllocator::Map(FRHIMemoryAllocation&) / Unmap(FRHIMemoryAllocation&)

← [公开 API](API.md) · `void*` / `void`

Map：已映射直接返回缓存指针；否则 vmaMapMemory 并缓存。Unmap：vmaUnmapMemory 并清缓存。CreateBuffer/CreateImage 的持久映射（MAPPED 标志）会在 AllocResult.pMappedData 直接带上。

```text
Map(Alloc):
1. if 无效或 Mapped 非空: return Mapped
2. vmaMapMemory; Alloc.Mapped = 结果; return

Unmap(Alloc):
1. vmaUnmapMemory; Alloc.Mapped = nullptr
```

## VulkanResources.cpp

全部 Vulkan 资源句柄的析构。统一模式：句柄非空时调用对应 `vkDestroyXxx` 并把句柄置空；缓冲/纹理经内存分配器销毁。两个例外：

- **非拥有视图**：swapchain 的 framebuffer / render pass 包装（`bOwnsHandle=false`）析构**跳过** vk 销毁——Vk 句柄归 swapchain 所有（`DestroySwapchainResources` 先 delete 包装再销毁 Vk 句柄）。
- **结构化缓冲**：`FVulkanStructuredBuffer` 不拥有底层缓冲（创建者销毁时连带 `delete Underlying`）；`FVulkanAccelerationStructure` 用 `vkGetDeviceProcAddr` 动态解析销毁函数（KHR 不在静态加载器）。

```text
模板（持有型资源析构）:
if Device != VK_NULL_HANDLE && Handle != VK_NULL_HANDLE:
    vkDestroyXxx(Device, Handle); Handle = VK_NULL_HANDLE

FRHIFence / FRHISemaphore / FRHIQueryPool / ...（delete 即销毁）
```

- [RHI.md](RHI.md) — 概念 · [公开 API](API.md) — 签名入口
