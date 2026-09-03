# RHI

## Code files

- [RHIAPI.h](Public/RHI/RHIAPI.h) — DLL 导出宏（`MAHO_RHI_API`）
- [RHIServer.h](Public/RHI/RHIServer.h) — `IRHI` 能力接口 + `FRHI` 渲染服务器 + `ERHIBackend`
- [RHICommandList.h](Public/RHI/RHICommandList.h) — 命令录制面 `FRHICommandList` / 逻辑提交端点 `FRHIQueue` / 动态渲染附件 `FRHIRenderingAttachmentInfo`
- [RHIEnums.h](Public/RHI/RHIEnums.h) — 全部 RHI 枚举 + 位运算模板（`RHIEnumOr` / `RHIEnumHas` / `operator|`）
- [RHIResources.h](Public/RHI/RHIResources.h) — 资源句柄族 + Desc 结构体族 + 内存分配器抽象
- （Private）[RHI.h](Private/RHI.h) — 内部设备接口 `IDynamicRHI` / 工厂 `FRHIFactory` / `FRHIInitDesc`（本 DLL 私有）
- （Private）[VulkanRHI.h](Private/VulkanRHI.h) / [VulkanCommandList.h](Private/VulkanCommandList.h) / [VulkanMemory.h](Private/VulkanMemory.h) / [VulkanResources.h](Private/VulkanResources.h) — Vulkan 后端内部实现头

## Concept — 渲染服务器

RHI 是**后端无关的 GPU 设备面**，形态是**渲染服务器**——一个常驻渲染服务线程（`FThreadedServer`）+ 线程池并行命令录制 + 直接队列提交，**不是调度层**。渲染所有者（FRender）持有 `FRHI`，只经 `IRHI` 命令面交互：`EnqueueTask`（并行录制）/ `Submit`（直接提交）/ 帧原语。命令列表生命周期与**何时提交**都是调用方（RDG）的调度决策；队列提交与帧原语由调用方保持串行。设备本身（私有的 `IDynamicRHI`）留在 RHI DLL 内，高层（RDG / render 插件）永不触碰具体后端类型。

### 双线程：并行录制 + 服务线程

`EnqueueTask(CmdList, Task)` 把 `Begin → Task → End` 整段扔进 `RecordingPool`（线程池）并行录制；`Flush()` 是录制屏障，`Submit` 前调用保证 record-all → submit-all 顺序。帧原语（`BeginFrame/EndFrame/Resize/PresentTexture`）是直接调用，跑在服务线程上——RDG 保持它们串行。Vulkan 禁止并发录进同一缓冲，故每个任务必须使用**自己的**命令列表。

### 帧管线

离屏渲染 + 最终 blit：特性经 `GetFrameCommandList()` 借帧命令缓冲录制（已 Begin、非拥有），全部场景录制后 `PresentTexture(Src)` 把离屏颜色纹理 blit 到当前 swapchain 后缓冲，`EndFrame` 收尾 + 提交 + present。swapchain 保持 RHI 私有，只暴露格式与帧缓冲尺寸——离屏场景目标必须与两者匹配（blit 要求格式一致）。

```cpp
// RDG 侧帧驱动（示意）
FRHI* RHI = Render->GetRHI();
RHI->BeginFrame();
FRHICommandList* Cmd = RHI->GetFrameCommandList();   // 已 Begin
Cmd->BeginRendering(ColorAttachments, 1, nullptr, W, H);
Cmd->BindGraphicsPipeline(PSO); Cmd->Draw(...);
Cmd->EndRendering();
RHI->PresentTexture(SceneColor);                     // 离屏 → swapchain
RHI->EndFrame();                                     // 提交 + present
```

### 并行命令录制

```cpp
// 并行录制多张命令列表（RDG 拥有生命周期）
for (auto& Job : Jobs)
    RHI->EnqueueTask(Job.CmdList, [Job](FRHICommandList* Cmd)
    {
        Cmd->BeginRenderPass(Pass, FB, W, H, ClearColor);
        Cmd->BindGraphicsPipeline(Job.PSO); Cmd->Draw(...);
        Cmd->EndRenderPass();
    });
RHI->Flush();                        // 等全部录制完
RHI->Submit(Job0.CmdList, ERHICommandListType::Graphics);
```

### 队列提交与回退

`Submit` 按命令列表类型路由到匹配逻辑队列（`GetGraphicsQueue/GetComputeQueue/GetTransferQueue`）。当 compute/transfer 队列 `IsNativeFallback()`（设备无专有队列族，映射回 graphics 本族）时，提交到 graphics 队列与其串行化——跨队列顺序由 RDG 保持串行。

### 资源所有权

全部资源经 `IRHI` 工厂创建，Create/Destroy 一一配对，句柄类不可拷贝（GPU 句柄按所有）。命令列表生命周期归调用方；swapchain 保持设备私有，其 framebuffer/render pass 的 RHI 包装是非拥有视图（析构不销毁 Vk 句柄）。

## Third-party dependencies

- **Vulkan**（Vulkan SDK 头 + 加载器）——后端 API；实例/设备扩展经 Vulkan 1.2 核心 + swapchain / dynamic rendering / descriptor indexing
- **VMA**（vk_mem_alloc）——GPU 内存分配（`VMA_IMPLEMENTATION` 单 TU 编译，`VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT`）

## CVars

| CVar | 默认 | 说明 |
|------|------|------|
| `r.RHI.Validation` | `1` | 0=关，1=开：Khronos validation layers + debug messenger（校验消息经 Maho 日志输出）。仅当 `VK_LAYER_KHRONOS_validation` 已安装时生效 |
| `r.VSync` | `1` | 0=prefer Mailbox/Immediate，1=FIFO（垂直同步） |
| `r.Swapchain.ExtraImages` | `1` | 交换链额外图像数（超出 minImageCount，受设备上限钳制） |
| `r.MinSwapchainImages` | `2` | 报告的最小交换链图像数 |

## Related docs

- [API.md](API.md) — API documentation
