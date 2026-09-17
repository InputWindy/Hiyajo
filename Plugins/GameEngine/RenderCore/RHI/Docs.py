# -*- coding: utf-8 -*-
# RHI 插件文档内容（由 Tools/plugin_docs.py 执行，docs_builder 以变量 D 注入）。
#
# 分层：Public/RHI/*.h = 后端无关的能力面（命令列表 / 资源描述 / 设备服务）——
#       之上只有 RDG / 渲染插件；Private/*.h = Vulkan 后端（含 VMA），纯实现细节。
# 内容按头文件逐条声明：Header → Card/Table/Row → Class/Struct → Interface/Field。

# ══════════════════════════════════════════════════════════════════════════════
# Public/RHI/RHIAPI.h —— RHI 模块导出标签
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/RHI/RHIAPI.h", Title="RHIAPI.h —— RHI 模块导出标签",
         Desc="RHI 的 DLL 边界标签。RHI 交给上层的是一批**多态资源类型**（`FRHIResource` 一族、"
              "`FRHICommandList`、`IRHI`）：实例在 RHI 的 DLL 里构造，却由另一个模块（RDG / 渲染插件）"
              "销毁 —— 正是「实例可能比构造它的模块活得久，且被别的模块 delete」那条硬规则。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT`：平台无关的 dllexport / dllimport 原语")

D.Card("导出标签")
D.Table("宏", "说明")
D.Row("MAHO_RHI_API",
      "编译 RHI 本模块时 codegen 定义 `MAHO_RHI_MODULE_EXPORTS` ⇒ 展开为 `MAHO_EXPORT`（本模块负责"
      "发射 vftable 与 deleting dtor）；消费方只拿到 `MAHO_IMPORT`。**为什么：**不导出的话，"
      "全内联派生类的 vftable 会按 TU 各发一份 COMDAT，谁构造就把谁的模块 vptr 写进对象，"
      "跨模块 `delete` 时静默崩在 `delete` 里（0xC0000005）；加上 dllimport 后这件事从运行期崩溃"
      "变成编译期错误。")

# ══════════════════════════════════════════════════════════════════════════════
# Public/RHI/RHIEnums.h —— 后端无关的枚举 / 位掩码
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/RHI/RHIEnums.h", Title="RHIEnums.h —— 后端无关的枚举 / 位掩码",
         Desc="RHI 与上层之间的**词汇表**：格式、用途、状态、比较函数、着色器阶段……"
              "全部按「D3D 式」命名，**没有一个 Vulkan 名字**（`VK_*` / `Vma*` 只活在 Private/）。"
              "这样 RDG 与渲染插件描述资源时完全不需要认识后端；将来换后端只改 Private 的映射"
              "（`ToVkFormat` 等），上层一行不动。\n"
              "位掩码枚举自带 `operator|` 与两个 constexpr 查询函数：`|` 组合用途、`RHIEnumHas` 判定，"
              "调用点写 `Usage | ERHIBufferUsage::Storage` 而不是手写强制转换 —— 强制转换是位掩码"
              "最常见的静默错误源。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("cstdint", "定宽整数底层类型（`std::uint8_t` / `16` / `32`）：枚举大小进 ABI，必须固定")
D.Row("type_traits", "`std::underlying_type_t`：位掩码运算按底层整数做，不依赖枚举的隐式转换")

D.Card("ERHIBackend", "图形后端选择。**只有一个值不是缺陷**：接口按多后端设计，实现目前只有 Vulkan。")
D.Table("枚举值", "说明")
D.Row("Vulkan = 0", "当前唯一后端；`FRHIFactory::Create` 按它挑实现")

D.Card("ERHIQueueType", "**逻辑**队列类型（三个永远都存在，与原生队列是否独立无关）。")
D.Table("枚举值", "说明")
D.Row("Graphics = 0", "图形队列：渲染通道 / 绘制 / blit")
D.Row("Compute = 1", "计算队列：dispatch（可能复用图形族的非独立队列）")
D.Row("Transfer = 2", "传输队列：host↔device 拷贝（可能复用图形族的非独立队列）")

D.Card("ERHICommandListType", "命令列表的类型 —— 决定这张列表**能录什么**（非法调用在 Debug 断言）。")
D.Table("枚举值", "说明")
D.Row("Graphics = 0", "可录渲染通道 / 绘制 / 计算 / 传输")
D.Row("Compute = 1", "只可录 dispatch 与传输")
D.Row("Transfer = 2", "只可录拷贝 / 填充 / 状态转换（最窄，最容易误用）")

D.Card("ERHIResourceType", "运行时资源分类（`FRHIResource::GetType`）。用于调试名、资源池归类与"
                           "「这指针到底是什么」的诊断，避免对抽象基类盲 `static_cast`。")
D.Table("枚举值", "说明")
D.Row("Unknown = 0", "未分类 / 基类默认")
D.Row("Buffer", "`FRHIBuffer`：线性字节缓冲")
D.Row("StructuredBuffer", "`FRHIStructuredBuffer`：带 stride 的结构化缓冲视图")
D.Row("BufferView", "`FRHIBufferView`：某缓冲的格式化区间视图")
D.Row("Texture", "`FRHITexture`：图像（2D / 数组 / Cube / 3D）")
D.Row("TextureView", "`FRHITextureView`：子资源区间（mip / array 切片）视图")
D.Row("Sampler", "`FRHISampler`：采样状态（过滤 / 寻址 / LOD）")
D.Row("ShaderModule", "`FRHIShaderModule`：一个阶段的字节码 + 入口")
D.Row("DescriptorSetLayout", "`FRHIDescriptorSetLayout`：绑定布局")
D.Row("PipelineLayout", "`FRHIPipelineLayout`：set 布局 + push constant 区间")
D.Row("GraphicsPipeline", "`FRHIGraphicsPipeline`：栅格 PSO（已烘焙成 GPU 二进制）")
D.Row("ComputePipeline", "`FRHIComputePipeline`：计算 PSO")
D.Row("RayTracingPipeline", "`FRHIRayTracingPipeline`：光追 PSO")
D.Row("DescriptorPool", "`FRHIDescriptorPool`：set 的分配池")
D.Row("DescriptorSet", "`FRHIDescriptorSet`：一个已分配的 set")
D.Row("Framebuffer", "`FRHIFramebuffer`：渲染通道的附件绑定")
D.Row("RenderPass", "`FRHIRenderPass`：附件格式 + load / store 语义")
D.Row("CommandPool", "`FRHICommandPool`：命令缓冲区分配池")
D.Row("Fence", "`FRHIFence`：CPU 可等待的 GPU 完成信号")
D.Row("Semaphore", "`FRHISemaphore`：GPU 内部队列间同步")
D.Row("QueryPool", "`FRHIQueryPool`：遮挡 / 时间戳查询")
D.Row("AccelerationStructure", "`FRHIAccelerationStructure`：BLAS / TLAS")

D.Card("ERHIFormat", "像素 / 顶点格式。前半段是**渲染必需的固定集合**，后半段是"
                     "「资源侧 `ETexturePixelFormat` 的镜像」—— 资源要能原样上传就必须有对应格式，"
                     "缺一个就只能在导入期做 CPU 转换。")
D.Table("枚举值", "说明")
D.Row("Unknown = 0", "未指定（不用深度附件时可填这个）")
D.Row("R8G8B8A8_UNORM", "8 位线性 RGBA：离屏目标 / 顶点色")
D.Row("B8G8R8A8_UNORM", "交换链常见颜色格式（线性）")
D.Row("B8G8R8A8_SRGB", "交换链 sRGB 变体")
D.Row("R32_SFLOAT", "单通道 32 位浮点（深度 / 单通道数据）")
D.Row("R32G32_SFLOAT", "双通道浮点（法线 / 切线等）")
D.Row("R32G32B32_SFLOAT", "三通道浮点（位置）")
D.Row("R16G16_SFLOAT", "半精度双通道")
D.Row("D24_UNORM_S8_UINT", "深度 24 + 模板 8（需要模板的深度附件）")
D.Row("D32_SFLOAT", "纯 32 位浮点深度")
D.Row("R8G8B8A8_SRGB", "资源侧 sRGB RGBA（反照率贴图）")
D.Row("R16G16B16A16_SFLOAT", "半精度 RGBA（HDR 中间目标）")
D.Row("R32G32B32A32_SFLOAT", "全精度 RGBA")
D.Row("R8_UNORM", "单通道 8 位（遮罩 / AOV）")
D.Row("R8G8_UNORM", "8 位（UV 等）")
D.Row("R8G8B8_UNORM", "三通道 8 位（无 alpha 的贴图）")
D.Row("R16_SFLOAT", "单通道半精度（高度图等）")

D.Card("ERHITextureDimension", "纹理维度。决定视图类型与上传时的子资源布局（3D 没有 array 层）。")
D.Table("枚举值", "说明")
D.Row("Tex2D = 0", "普通 2D 纹理（离屏目标 / 贴图）")
D.Row("Tex2DArray", "2D 数组（按索引切换层）")
D.Row("Cube", "立方体贴图（6 层 + 立方采样）")
D.Row("Tex3D", "体纹理（三轴采样）")

D.Card("ERHIBufferUsage", "缓冲用途位掩码。**必须如实声明**：用法位在建缓冲时就固定，"
                          "事后再要一个没声明的用途只能重建资源。")
D.Table("枚举值", "说明")
D.Row("None = 0", "无用途（占位 / 判空）")
D.Row("Vertex = 1 << 0", "顶点缓冲（可 `BindVertexBuffer`）")
D.Row("Index = 1 << 1", "索引缓冲（可 `BindIndexBuffer`）")
D.Row("Uniform = 1 << 2", "常量缓冲（UBO）")
D.Row("Storage = 1 << 3", "存储缓冲（SSBO / UAV）")
D.Row("TransferSrc = 1 << 4", "可作拷贝源（回读 / GPU 内拷贝）")
D.Row("TransferDst = 1 << 5", "可作拷贝目标（上传 / GPU 内拷贝）")
D.Row("Indirect = 1 << 6", "间接绘制参数缓冲（`DrawIndirect` 一族）")
D.Row("DeviceAddress = 1 << 7", "可取设备地址 ⇒ 可在着色器里指针访问（SBT / AS scratch 必需）")
D.Row("AccelerationStructure = 1 << 8", "可作加速结构存储（BLAS / TLAS 的 backing buffer）")

D.Card("ERHITextureUsage", "纹理用途位掩码。离屏目标要求含 `ColorAttachment` / `DepthStencil`，"
                          "blit 上屏要求两端都是 `TransferSrc` / `TransferDst`。")
D.Table("枚举值", "说明")
D.Row("None = 0", "无用途（占位 / 判空）")
D.Row("Sampled = 1 << 0", "可被采样（着色器纹理）")
D.Row("ColorAttachment = 1 << 1", "可作颜色附件（渲染目标）")
D.Row("DepthStencil = 1 << 2", "可作深度 / 模板附件")
D.Row("Storage = 1 << 3", "可作存储图像（读写）")
D.Row("TransferSrc = 1 << 4", "可作拷贝源（含上屏 blit 的源）")
D.Row("TransferDst = 1 << 5", "可作拷贝目标（含上屏 blit 的目标）")
D.Row("Transient = 1 << 6", "瞬时（内容不跨帧要求保留，驱动可优化布局 / 内存）")

D.Card("ERHIMemoryUsage", "内存放置策略 —— 直接决定这次分配是否 host 可见，"
                          "因此决定上传走直写还是走 staging。")
D.Table("枚举值", "说明")
D.Row("GPUOnly = 0", "仅设备本地：最快，但 `Map` 返回 nullptr，上传必须先过 staging")
D.Row("CPUToGPU", "host 可见、写多读少（staging / 动态常量）")
D.Row("GPUToCPU", "host 可见、用于读回（回读 / 查询结果）")
D.Row("CPUOnly", "纯 CPU 侧（不经 GPU）")

D.Card("ERHIResourceState", "资源状态转换用的粗粒度状态：**Vulkan 的屏障语义被折成这一层**，"
                            "上层只说「从什么状态到什么状态」，具体 stage / access 掩码由后端填。")
D.Table("枚举值", "说明")
D.Row("Common = 0", "通用（转换的起点 / 终点，或不确定时）")
D.Row("VertexBuffer", "顶点缓冲读取")
D.Row("IndexBuffer", "索引缓冲读取")
D.Row("UniformBuffer", "常量缓冲读取")
D.Row("ShaderResource", "着色器只读资源（采样纹理 / SRV）")
D.Row("UnorderedAccess", "着色器读写资源（UAV）")
D.Row("IndirectArgument", "间接参数读取")
D.Row("RenderTarget", "作为颜色附件写入")
D.Row("DepthWrite", "作为深度 / 模板写入")
D.Row("CopySrc", "作为拷贝源")
D.Row("CopyDst", "作为拷贝目标")
D.Row("Present", "交接给呈现（仅交换链镜像）")

D.Card("ERHIQueryType", "GPU 查询种类。`Occlusion` 用于可见性剔除回读，"
                        "`Timestamp` 用于帧内耗时统计（写入后要跨帧才读得到结果）。")
D.Table("枚举值", "说明")
D.Row("Occlusion = 0", "二值遮挡查询（`VK_QUERY_TYPE_OCCLUSION`）")
D.Row("Timestamp = 1", "GPU 时间戳（`VK_QUERY_TYPE_TIMESTAMP`）")

D.Card("ERHIDescriptorType", "描述符绑定种类。`Dynamic*` 变体是「同一 layout、每绘制换一个偏移」"
                             "的路径：`BindDescriptorSets` 的 `DynamicOffsets` 只对它们生效。")
D.Table("枚举值", "说明")
D.Row("Sampler = 0", "独立采样器")
D.Row("CombinedImageSampler", "采样器 + 图像（最常见）")
D.Row("SampledImage", "只读图像")
D.Row("StorageImage", "读写图像")
D.Row("UniformBuffer", "常量缓冲")
D.Row("StorageBuffer", "存储缓冲")
D.Row("DynamicUniform", "带动态偏移的常量缓冲（每实例切片）")
D.Row("DynamicStorage", "带动态偏移的存储缓冲")
D.Row("AccelerationStructure", "只读加速结构绑定（`VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR`）")

D.Card("ERHIShaderStage", "着色器阶段位掩码（用于绑定可见性、push constant 区间、SBT 分组）。")
D.Table("枚举值", "说明")
D.Row("None = 0", "无阶段（占位）")
D.Row("Vertex = 1 << 0", "顶点着色器")
D.Row("Fragment = 1 << 1", "片段（像素）着色器")
D.Row("Compute = 1 << 2", "计算着色器")
D.Row("RayGen = 1 << 3", "光追：光线生成")
D.Row("AnyHit = 1 << 4", "光追：任意命中")
D.Row("ClosestHit = 1 << 5", "光追：最近命中")
D.Row("Miss = 1 << 6", "光追：未命中")
D.Row("Intersection = 1 << 7", "光追：相交（自定义几何，需含相交着色器的扩展）")
D.Row("Callable = 1 << 8", "光追：可调用着色器")
D.Row("AllGraphics", "`Vertex | Fragment` 的便捷组合")
D.Row("AllRayTracing", "全部 6 个光追阶段的组合")
D.Row("MAX_COUNT = 9", "位数上限（SBT / 分组数组的容量依据）")

D.Card("ERHIPrimitiveTopology", "图元装配方式（`FRHIGraphicsPipelineDesc::Topology`）。")
D.Table("枚举值", "说明")
D.Row("TriangleList = 0", "独立三角形（默认）")
D.Row("TriangleStrip", "三角形带")
D.Row("LineList", "独立线段（线框 / 调试绘制）")
D.Row("PointList", "点（粒子 / 调试）")

D.Card("ERHICullMode", "面剔除。默认 `Back` —— 反面剔除既省填充率，也让「绕序写错」立刻可见。")
D.Table("枚举值", "说明")
D.Row("None = 0", "不剔除（双面材质 / 调试排错）")
D.Row("Front", "剔除正面（内表面渲染）")
D.Row("Back", "剔除反面（不透明实体的默认）")

D.Card("ERHIFillMode", "填充模式。`Wireframe` 依赖设备的非填充多边形特性"
                       "（扩展没启用时后端会退化成实心）。")
D.Table("枚举值", "说明")
D.Row("Solid = 0", "实心（默认）")
D.Row("Wireframe", "线框（调试 / 可视化）")

D.Card("ERHICompareOp", "深度 / 模板比较函数（顺序与 Vulkan 的 `VkCompareOp` 一致）。")
D.Table("枚举值", "说明")
D.Row("Never = 0", "永不通过（可用于临时禁写）")
D.Row("Less", "小于（深度测试的默认：近处通过）")
D.Row("Equal", "相等")
D.Row("LessOrEqual", "小于等于（深度预通道后常用）")
D.Row("Greater", "大于（反向 Z）")
D.Row("NotEqual", "不等（模板常用）")
D.Row("GreaterOrEqual", "大于等于")
D.Row("Always", "总是通过（关掉测试但保留状态）")

D.Card("ERHIBlendFactor", "混合因子（源 / 目标各自的系数）。")
D.Table("枚举值", "说明")
D.Row("Zero = 0", "零")
D.Row("One", "一")
D.Row("SrcColor", "源颜色")
D.Row("OneMinusSrcColor", "1 − 源颜色")
D.Row("DstColor", "目标颜色")
D.Row("OneMinusDstColor", "1 − 目标颜色")
D.Row("SrcAlpha", "源 alpha（普通 alpha 混合的源因子）")
D.Row("OneMinusSrcAlpha", "1 − 源 alpha（普通 alpha 混合的目标因子）")
D.Row("DstAlpha", "目标 alpha")
D.Row("OneMinusDstAlpha", "1 − 目标 alpha")

D.Card("ERHIBlendOp", "颜色 / alpha 的混合运算。")
D.Table("枚举值", "说明")
D.Row("Add = 0", "相加（默认，普通透明混合）")
D.Row("Subtract", "源 − 目标")
D.Row("ReverseSubtract", "目标 − 源")
D.Row("Min", "逐通道取小（加法光 / 变暗）")
D.Row("Max", "逐通道取大（变亮）")

D.Card("ERHILoadOp", "附件进入渲染通道时对已有内容的处理。")
D.Table("枚举值", "说明")
D.Row("Load = 0", "保留已有内容（叠加 / 多 pass 复用同一目标）")
D.Row("Clear", "先清成确定值（帧首色 / 深度目标的默认）")
D.Row("DontCare", "内容无意义（驱动可跳过，最省带宽）")

D.Card("ERHIStoreOp", "附件离开渲染通道时的处理。")
D.Table("枚举值", "说明")
D.Row("Store = 0", "写回内存（后续 pass / 呈现要用）")
D.Row("DontCare", "丢弃（只在本通道内用的中间态，可省一次写回）")

D.Card("ERHIFilter", "纹理过滤方式。")
D.Table("枚举值", "说明")
D.Row("Nearest = 0", "最近邻（像素风 / 数据贴图，避免插值污染）")
D.Row("Linear", "线性（普通贴图的默认）")

D.Card("ERHIAddressMode", "寻址模式（U / V / W 各一份）。")
D.Table("枚举值", "说明")
D.Row("Repeat = 0", "重复平铺（默认）")
D.Row("MirroredRepeat", "镜像重复")
D.Row("ClampToEdge", "夹到边缘（UI / 全屏贴图）")
D.Row("ClampToBorder", "夹到边界色")

D.Card("枚举辅助函数", "位掩码型枚举的公共运算：组合与判定。都声明 `constexpr` + `[[nodiscard]]`，"
                      "调用点得到编译期常量，且返回值不会被无意丢掉。")
D.Table("签名", "说明")
D.Row("template <typename TEnum> constexpr TEnum RHIEnumOr(TEnum A, TEnum B)",
      "按底层整数取或 —— 所有位掩码 `operator|` 的实现，避免在每个枚举上重复写强制转换")
D.Row("template <typename TEnum> constexpr bool RHIEnumHas(TEnum Mask, TEnum Flag)",
      "掩码判定：`(Mask & Flag) != 0`。**为什么单独给函数：**`enum class` 上裸写 `static_cast` "
      "又长又易错，而这是位掩码最常见的静默错误源")
D.Row("constexpr ERHIBufferUsage operator|(ERHIBufferUsage, ERHIBufferUsage)",
      "缓冲用途组合：`Usage | ERHIBufferUsage::Storage`")
D.Row("constexpr ERHITextureUsage operator|(ERHITextureUsage, ERHITextureUsage)",
      "纹理用途组合：`ColorAttachment | TransferSrc`")
D.Row("constexpr ERHIShaderStage operator|(ERHIShaderStage, ERHIShaderStage)",
      "阶段可见性组合：`Vertex | Fragment`")

# ══════════════════════════════════════════════════════════════════════════════
# Public/RHI/RHIResources.h —— 资源描述（desc）+ 资源基类
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/RHI/RHIResources.h", Title="RHIResources.h —— 资源描述 + 资源基类",
         Desc="RHI 的**数据面**：一族 `FRHIxxxDesc`（纯 POD 描述）+ 一族 `FRHIxxx`（多态资源句柄）。"
              "两层刻意分开：\n"
              "**描述是值**（可拷贝、可比较、可当哈希键）—— 资源池 / PSO 缓存正是靠 `operator==` 与"
              "描述字段做「同样的描述只建一次」的去重。\n"
              "**资源是句柄**（不可拷贝、只能 `Create*` / `Destroy*`）—— 它们跨 DLL 边界，"
              "所以整族都挂 `MAHO_RHI_API`。基类只放「所有资源都有的东西」：类型名（诊断）、"
              "类型枚举（分类）、调试名；`GetTypeName` / `GetType` 在派生类的头文件里内联"
              "（返回字面量 / 常量枚举），因为这是逐类型自报家门，不该走虚调用到另一个模块。\n"
              "**没有 Vulkan 类型**：`VkImage` / `VmaAllocation` 只在 Private 的派生类里，"
              "上层拿到的永远是不透明基类指针。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("RHIAPI.h", "`MAHO_RHI_API`：整族资源类跨 DLL")
D.Row("RHI/RHIEnums.h", "描述里的 Format / Usage / MemoryUsage 等枚举")
D.Row("cstddef / cstdint", "`std::size_t` / 定宽整数：描述进 ABI，尺寸必须确定")
D.Row("string", "资源基类里的调试名")
D.Row("vector", "描述里的数组字段（绑定、附件、几何…）")

D.Struct("FRHIMemoryAllocation", Desc="一次分配的不透明句柄：`Native` 是后端资源"
                                     "（`VkBuffer` / `VkImage` 的 VMA allocation），`Mapped` 是 host 指针。"
                                     "**故意用 `void*`**：它经 `IDynamicRHIMemoryAllocator` 跨模块传递，"
                                     "公开面不能出现 VMA 类型。")
D.Field("void* Native", "分配句柄（VmaAllocation 的不透明形式）")
D.Field("void* Mapped", "已映射的 host 指针；未映射 / 不可映射（GPUOnly）时为 nullptr")

D.Class("IDynamicRHIMemoryAllocator", Desc="内存分配器的**设备侧**抽象：映射 / 解除映射 / 释放。"
        "存在的意义是让「host 可见性」这件事留在后端 —— 上层只问「能映射吗」，"
        "不能就让 `UpdateBuffer` 自动改走 staging。")
D.Interface("virtual ~IDynamicRHIMemoryAllocator() = default", "虚析构：经基类指针释放（跨模块安全）")
D.Interface("virtual void Free(FRHIMemoryAllocation& Alloc) = 0", "释放一次分配（成功后清空句柄）")
D.Interface("virtual void* Map(FRHIMemoryAllocation& Alloc) = 0",
            "映射进 CPU 地址空间。**GPUOnly 分配返回 nullptr**（设备本地内存不可映射，"
            "强行 `vmaMapMemory` 违反 VUID-vkMapMemory-memory-00682）")
D.Interface("virtual void Unmap(FRHIMemoryAllocation& Alloc) = 0", "解除映射（映射配对，避免长期占用地址空间）")

D.Class("FRHIResource", Base="（抽象基类）",
        Desc="所有 RHI 资源的公共基类。只承载「每个资源都该能回答」的三件事：类型名、类型枚举、"
             "调试名。**不可拷贝**（句柄语义：拷贝一个资源指针等于多一份所有权，"
             "而释放点只有 `Destroy*` 一个）；构造函数 protected ⇒ 只能经 `Create*` 得到。")
D.SetAccess("public")
D.Interface("virtual ~FRHIResource() = default", "虚析构：上层只持有基类指针，销毁必须经虚表回到本模块")
D.Interface("FRHIResource(const FRHIResource&) = delete", "禁止拷贝构造（句柄唯一所有权）")
D.Interface("FRHIResource& operator=(const FRHIResource&) = delete", "禁止拷贝赋值")
D.Interface("[[nodiscard]] virtual const char* GetTypeName() const = 0",
            "类型名（字面量）：日志 / 调试用，派生类内联返回，不走字符串分配")
D.Interface("[[nodiscard]] virtual ERHIResourceType GetType() const = 0",
            "类型枚举：资源池归类与诊断（比 `dynamic_cast` 便宜且可在任意模块用）")
D.Interface("[[nodiscard]] const std::string& GetDebugName() const", "调试名（非虚：直接返回字段，零拷贝）")
D.SetAccess("protected")
D.Interface("FRHIResource() = default", "protected 构造 ⇒ 只能由 RHI 内部的工厂创建")
D.Field("std::string DebugName", "调试名（Debug / 报错时显示）")
D.Field("std::uint32_t RefCount = 1", "引用计数初值 1（生命周期由 `Destroy*` 收口，不靠共享指针）")

D.Struct("FRHIBufferDesc", Desc="缓冲描述。带 `operator==` 是为了**去重缓存**："
                                "同样的 Size / Usage / MemoryUsage 命中同一条缓存项。")
D.Field("std::uint64_t Size", "字节数")
D.Field("ERHIBufferUsage Usage = ERHIBufferUsage::None", "用途位掩码（决定可绑定点与可拷贝方向）")
D.Field("ERHIMemoryUsage MemoryUsage = ERHIMemoryUsage::GPUOnly", "内存放置（默认设备本地）")
D.Interface("[[nodiscard]] bool operator==(const FRHIBufferDesc& O) const", "逐字段相等（缓存键）")
D.Interface("[[nodiscard]] bool operator!=(const FRHIBufferDesc& O) const", "不等（实现为取反）")

D.Class("FRHIBuffer", Base="FRHIResource", Desc="线性缓冲句柄（顶点 / 索引 / 常量 / 存储 / 暂存，"
        "具体用途由 desc 的 Usage 决定）。")
D.SetAccess("public")
D.Interface("[[nodiscard]] const char* GetTypeName() const override", "返回 `\"FRHIBuffer\"`")
D.Interface("[[nodiscard]] ERHIResourceType GetType() const override", "返回 `ERHIResourceType::Buffer`")
D.Interface("[[nodiscard]] virtual const FRHIBufferDesc& GetDesc() const = 0", "只读描述（引用，不拷贝）")
D.Interface("[[nodiscard]] virtual std::uint64_t GetDeviceAddress() const",
            "GPU 设备地址（Usage 含 `DeviceAddress` 才有意义），默认 0 —— "
            "**给默认实现**：只有可寻址缓冲才需要覆写，其余类型不必被逼着写空实现")

D.Struct("FRHIStructuredBufferDesc", Desc="结构化缓冲描述：明确 stride 与元素个数，"
        "让描述符 / 绑定能一次算出区间（不需要调用方自己乘）。")
D.Field("std::uint64_t Size", "总字节数")
D.Field("std::uint32_t Stride", "每元素字节数")
D.Field("std::uint32_t ElementCount", "元素个数")
D.Field("ERHIBufferUsage Usage = ERHIBufferUsage::Storage", "默认 Storage（SSBO 用途）")
D.Field("ERHIMemoryUsage MemoryUsage = ERHIMemoryUsage::GPUOnly", "内存放置")

D.Class("FRHIStructuredBuffer", Base="FRHIResource",
        Desc="结构化缓冲：**包装**一个底层 `FRHIBuffer`（用 `View` 语义复用同一段显存），"
             "便于按 stride 描述绑定。")
D.SetAccess("public")
D.Interface("[[nodiscard]] const char* GetTypeName() const override", "返回 `\"FRHIStructuredBuffer\"`")
D.Interface("[[nodiscard]] ERHIResourceType GetType() const override",
            "返回 `ERHIResourceType::StructuredBuffer`")
D.Interface("[[nodiscard]] virtual const FRHIStructuredBufferDesc& GetDesc() const = 0", "只读描述")
D.Interface("[[nodiscard]] virtual FRHIBuffer* GetUnderlyingBuffer() = 0",
            "底层缓冲（拷贝 / 传输按它走；**非 const**：释放时后端要拿它做延迟回收记账）")

D.Struct("FRHIBufferViewDesc", Desc="缓冲视图描述：某段区间按某种格式解释。用于纹素缓冲 / 反缓冲这类"
        "「同一段显存换一种读法」。")
D.Field("FRHIBuffer* Buffer", "被视图化的缓冲（不持有；生命周期由调用方保证）")
D.Field("std::uint64_t Offset", "起始字节偏移")
D.Field("std::uint64_t Range", "长度（0 表示到末尾）")
D.Field("ERHIFormat Format = ERHIFormat::Unknown", "格式（纹素缓冲需要）")
D.Field("std::uint32_t Stride", "跨距（结构化视图需要）")

D.Class("FRHIBufferView", Base="FRHIResource", Desc="缓冲视图句柄（区间 + 格式）。")
D.SetAccess("public")
D.Interface("[[nodiscard]] const char* GetTypeName() const override", "返回 `\"FRHIBufferView\"`")
D.Interface("[[nodiscard]] ERHIResourceType GetType() const override",
            "返回 `ERHIResourceType::BufferView`")

D.Struct("FRHIExtent3D", Desc="三维尺寸（默认 1×1×1：让只填长宽的使用方不必写全三个字段）。"
        "带相等比较，参与 `FRHITextureDesc` 的整体比较。")
D.Field("std::uint32_t Width = 1", "宽（像素）")
D.Field("std::uint32_t Height = 1", "高（像素）")
D.Field("std::uint32_t Depth = 1", "深（3D 纹理 / 数组层数相关）")
D.Interface("[[nodiscard]] bool operator==(const FRHIExtent3D& O) const", "逐字段相等")
D.Interface("[[nodiscard]] bool operator!=(const FRHIExtent3D& O) const", "不等（实现为取反）")

D.Struct("FRHITextureDesc", Desc="纹理描述：格式 + 维度 + 尺寸 + mip / 层 + 用途 + 内存。"
        "**用途必须写全**（颜色附件、深度、采样、blit 两端…），因为 Vulkan 的 image usage 在创建时"
        "固定；描述比较用于资源池去重。")
D.Field("ERHIFormat Format = ERHIFormat::Unknown", "像素格式")
D.Field("ERHITextureDimension Dimension = ERHITextureDimension::Tex2D", "维度")
D.Field("FRHIExtent3D Extent{}", "尺寸")
D.Field("std::uint32_t MipLevels = 1", "mip 层数（1 = 只有原始层）")
D.Field("std::uint32_t ArrayLayers = 1", "数组层数（Cube 为 6）")
D.Field("ERHITextureUsage Usage = ERHITextureUsage::None", "用途位掩码")
D.Field("ERHIMemoryUsage MemoryUsage = ERHIMemoryUsage::GPUOnly", "内存放置")
D.Interface("[[nodiscard]] bool operator==(const FRHITextureDesc& O) const",
            "逐字段相等（含 `Extent`）—— 资源池的缓存键")
D.Interface("[[nodiscard]] bool operator!=(const FRHITextureDesc& O) const", "不等（实现为取反）")

D.Class("FRHITexture", Base="FRHIResource", Desc="纹理句柄（离屏目标 / 贴图 / 深度）。"
        "**交换链镜像不在这一族**（见 `IRHI::GetFrameCommandList` 的说明）："
        "上层只渲染到自己的纹理，再由 `PresentTexture` blit 上屏。")
D.SetAccess("public")
D.Interface("[[nodiscard]] const char* GetTypeName() const override", "返回 `\"FRHITexture\"`")
D.Interface("[[nodiscard]] ERHIResourceType GetType() const override", "返回 `ERHIResourceType::Texture`")
D.Interface("[[nodiscard]] virtual const FRHITextureDesc& GetDesc() const = 0", "只读描述")

D.Struct("FRHITextureViewDesc", Desc="纹理视图描述：在父纹理上圈一个子资源区间（mip / array 切片 +"
        "可选换格式）。**为什么要视图：**渲染通道的附件与描述符都需要「同一张纹理的不同切面」"
        "而不重建纹理。")
D.Field("FRHITexture* Texture", "父纹理（不持有）")
D.Field("ERHIFormat Format = ERHIFormat::Unknown", "视图格式（Unknown = 沿用父纹理格式）")
D.Field("std::uint32_t BaseMip = 0", "起始 mip")
D.Field("std::uint32_t MipCount = 1", "mip 数量")
D.Field("std::uint32_t BaseArrayLayer = 0", "起始数组层")
D.Field("std::uint32_t ArrayLayerCount = 1", "数组层数量")

D.Class("FRHITextureView", Base="FRHIResource", Desc="纹理视图句柄（渲染通道附件 / 描述符绑定用）。")
D.SetAccess("public")
D.Interface("[[nodiscard]] const char* GetTypeName() const override", "返回 `\"FRHITextureView\"`")
D.Interface("[[nodiscard]] ERHIResourceType GetType() const override",
            "返回 `ERHIResourceType::TextureView`")

D.Struct("FRHISamplerDesc", Desc="采样器描述：过滤 + 寻址 + LOD。"
        "`MaxLod` 默认 1000 而非 0 —— 0 会把 mip 链掐死在第一层。")
D.Field("ERHIFilter MinFilter = ERHIFilter::Linear", "缩小过滤")
D.Field("ERHIFilter MagFilter = ERHIFilter::Linear", "放大过滤")
D.Field("ERHIAddressMode AddressU = ERHIAddressMode::Repeat", "U 向寻址")
D.Field("ERHIAddressMode AddressV = ERHIAddressMode::Repeat", "V 向寻址")
D.Field("ERHIAddressMode AddressW = ERHIAddressMode::Repeat", "W 向寻址")
D.Field("float LodBias = 0.0f", "LOD 偏移（锐化 / 柔化）")
D.Field("float MinLod = 0.0f", "最小 LOD")
D.Field("float MaxLod = 1000.0f", "最大 LOD（默认放开整条 mip 链）")

D.Class("FRHISampler", Base="FRHIResource", Desc="采样器句柄（过滤 / 寻址状态对象）。")
D.SetAccess("public")
D.Interface("[[nodiscard]] const char* GetTypeName() const override", "返回 `\"FRHISampler\"`")
D.Interface("[[nodiscard]] ERHIResourceType GetType() const override", "返回 `ERHIResourceType::Sampler`")
D.Interface("[[nodiscard]] virtual const FRHISamplerDesc& GetDesc() const = 0", "只读描述")

D.Struct("FRHIShaderModuleDesc", Desc="着色器模块描述：阶段 + 字节码 + 入口。"
        "**字节码是不透明指针**（SPIR-V words 或 D3D 字节码）—— 公开面不认识具体 IR。")
D.Field("ERHIShaderStage Stage = ERHIShaderStage::None", "阶段（决定在管线里挂哪个位置）")
D.Field("const void* Bytecode", "字节码数据（不持有；创建后即可释放）")
D.Field("std::size_t BytecodeSize", "字节码长度")
D.Field("const char* EntryPoint = \"main\"", "入口函数名（多数情况为 main）")

D.Class("FRHIShaderModule", Base="FRHIResource",
        Desc="着色器模块句柄。**是短命对象**：PSO 一旦创建就自带编译好的 GPU 二进制，"
             "模块本身可以立刻销毁 —— 所以 PSO 缓存必须按字节码内容（哈希）而不按模块指针做键。")
D.SetAccess("public")
D.Interface("[[nodiscard]] const char* GetTypeName() const override", "返回 `\"FRHIShaderModule\"`")
D.Interface("[[nodiscard]] ERHIResourceType GetType() const override",
            "返回 `ERHIResourceType::ShaderModule`")

D.Struct("FRHIDescriptorBinding", Desc="描述符绑定项：一个 binding 号 + 类型 + 数量 + 可见阶段。"
        "`bPartiallyBound` / `bVariableCount` 是「不要求全部写满 / 运行期决定数量」的绑定数组能力。")
D.Field("std::uint32_t Binding", "binding 号（与着色器里的 layout 一致）")
D.Field("ERHIDescriptorType Type = ERHIDescriptorType::UniformBuffer", "描述符类型")
D.Field("std::uint32_t Count = 1", "数组元素个数")
D.Field("ERHIShaderStage Stages = ERHIShaderStage::None", "可见阶段位掩码")
D.Field("bool bPartiallyBound = false", "允许部分绑定（不必写满整个数组）")
D.Field("bool bVariableCount = false", "数组数量运行期可变（update-after-bind）")

D.Struct("FRHIDescriptorSetLayoutDesc", Desc="set 布局 = 一列绑定项。")
D.Field("std::vector<FRHIDescriptorBinding> Bindings", "绑定列表（顺序无关，按 binding 号匹配）")

D.Class("FRHIDescriptorSetLayout", Base="FRHIResource", Desc="set 布局句柄（写入 `PipelineLayout`）。")
D.SetAccess("public")
D.Interface("[[nodiscard]] const char* GetTypeName() const override", "返回 `\"FRHIDescriptorSetLayout\"`")
D.Interface("[[nodiscard]] ERHIResourceType GetType() const override",
            "返回 `ERHIResourceType::DescriptorSetLayout`")

D.Struct("FRHIPushConstantRange", Desc="push constant 区间：阶段 + 偏移 + 大小。"
        "每帧小常量走这里最省（不需要 UBO + 描述符），但**区间必须在管线布局里声明**。")
D.Field("ERHIShaderStage Stages = ERHIShaderStage::None", "哪些阶段可见")
D.Field("std::uint32_t Offset", "起始字节偏移")
D.Field("std::uint32_t Size", "字节数")

D.Struct("FRHIPipelineLayoutDesc", Desc="管线布局 = set 布局列表 + push constant 区间列表。"
        "它是「着色器怎么拿到数据」的全部约定，管线与绑定必须一致。")
D.Field("std::vector<FRHIDescriptorSetLayout*> SetLayouts", "set 布局（下标 = set 号）")
D.Field("std::vector<FRHIPushConstantRange> PushConstants", "push constant 区间")

D.Class("FRHIPipelineLayout", Base="FRHIResource", Desc="管线布局句柄。")
D.SetAccess("public")
D.Interface("[[nodiscard]] const char* GetTypeName() const override", "返回 `\"FRHIPipelineLayout\"`")
D.Interface("[[nodiscard]] ERHIResourceType GetType() const override",
            "返回 `ERHIResourceType::PipelineLayout`")

D.Struct("FRHIVertexAttribute", Desc="顶点属性：location + 格式 + 偏移。"
        "跨距（stride）在管线描述里给，属性只描述自己那一段。")
D.Field("std::uint32_t Location", "着色器里的 location")
D.Field("ERHIFormat Format = ERHIFormat::Unknown", "属性格式")
D.Field("std::uint32_t Offset", "在顶点结构里的字节偏移")

D.Struct("FRHIAttachmentBlend", Desc="单个颜色附件的混合状态（MRT 时每个附件一份）。"
        "`bBlend` 关掉时其余字段无意义。")
D.Field("bool bBlend = false", "是否开启混合")
D.Field("ERHIBlendFactor SrcColorFactor = ERHIBlendFactor::One", "颜色源因子")
D.Field("ERHIBlendFactor DstColorFactor = ERHIBlendFactor::Zero", "颜色目标因子")
D.Field("ERHIBlendFactor SrcAlphaFactor = ERHIBlendFactor::One", "alpha 源因子")
D.Field("ERHIBlendFactor DstAlphaFactor = ERHIBlendFactor::Zero", "alpha 目标因子")
D.Field("ERHIBlendOp ColorOp = ERHIBlendOp::Add", "颜色混合运算")
D.Field("ERHIBlendOp AlphaOp = ERHIBlendOp::Add", "alpha 混合运算")

D.Struct("FRHIGraphicsPipelineDesc", Desc="栅格管线描述：着色器 + 布局 + 渲染通道 + 顶点布局 + "
        "光栅 / 深度 / 混合状态。**必须带 `RenderPass`**：Vulkan 的 PSO 与渲染通道的附件格式强绑定，"
        "不兼容的通道不能复用同一个 PSO。")
D.Field("FRHIShaderModule* VertexShader", "顶点着色器模块（不持有）")
D.Field("FRHIShaderModule* FragmentShader", "片段着色器模块")
D.Field("const char* VertexEntryPoint = \"main\"", "顶点入口名")
D.Field("const char* FragmentEntryPoint = \"main\"", "片段入口名")
D.Field("FRHIPipelineLayout* Layout", "管线布局（描述符 / push constant 约定）")
D.Field("FRHIRenderPass* RenderPass", "配套的渲染通道（附件格式必须一致）")
D.Field("ERHIPrimitiveTopology Topology = ERHIPrimitiveTopology::TriangleList", "图元装配")
D.Field("std::uint32_t VertexStride", "顶点跨距（字节）")
D.Field("std::vector<FRHIVertexAttribute> Attributes", "顶点属性表")
D.Field("ERHICullMode CullMode = ERHICullMode::Back", "面剔除（默认剔反面）")
D.Field("ERHIFillMode FillMode = ERHIFillMode::Solid", "填充模式")
D.Field("ERHIFormat ColorFormat = ERHIFormat::B8G8R8A8_UNORM", "颜色附件格式（与 RenderPass 一致）")
D.Field("ERHIFormat DepthFormat = ERHIFormat::Unknown", "深度附件格式（Unknown = 无深度）")
D.Field("std::uint32_t SampleCount = 1", "采样数（1 = 不开 MSAA）")
D.Field("bool bDepthTest = false", "是否开启深度测试")
D.Field("bool bDepthWrite = false", "是否写深度")
D.Field("ERHICompareOp DepthCompare = ERHICompareOp::Less", "深度比较函数")
D.Field("std::vector<FRHIAttachmentBlend> AttachmentBlends", "各颜色附件的混合状态")
D.Field("bool bAlphaToCoverage = false", "alpha-to-coverage（植被 / 镂空 + MSAA）")
D.Field("std::uint64_t VertexShaderHash", "顶点字节码指纹（调用方编译后填，如 SPIR-V 词的 FNV）")
D.Field("std::uint64_t FragmentShaderHash", "片段字节码指纹")

D.Class("FRHIGraphicsPipeline", Base="FRHIResource",
        Desc="栅格管线句柄（已烘焙的 GPU 二进制）。**创建即贵**：上层必须经 PSO 缓存按"
             "「描述 + 字节码哈希」复用，不能每帧建。")
D.SetAccess("public")
D.Interface("[[nodiscard]] const char* GetTypeName() const override", "返回 `\"FRHIGraphicsPipeline\"`")
D.Interface("[[nodiscard]] ERHIResourceType GetType() const override",
            "返回 `ERHIResourceType::GraphicsPipeline`")

D.Struct("FRHIComputePipelineDesc", Desc="计算管线描述（只需计算模块 + 布局，没有附件 / 顶点状态）。")
D.Field("FRHIShaderModule* ComputeShader", "计算着色器模块")
D.Field("const char* ComputeEntryPoint = \"main\"", "入口名")
D.Field("FRHIPipelineLayout* Layout", "管线布局")

D.Class("FRHIComputePipeline", Base="FRHIResource", Desc="计算管线句柄。")
D.SetAccess("public")
D.Interface("[[nodiscard]] const char* GetTypeName() const override", "返回 `\"FRHIComputePipeline\"`")
D.Interface("[[nodiscard]] ERHIResourceType GetType() const override",
            "返回 `ERHIResourceType::ComputePipeline`")

D.Struct("FRHIRayTracingPipelineDesc", Desc="光追管线描述：各阶段模块成组（一个阶段可有多个模块，"
        "如多个 miss）。`EntryPoints` 与模块**顺序一一对应**。")
D.Field("FRHIShaderModule* RayGen", "光线生成着色器（必须有且只有一个）")
D.Field("std::vector<FRHIShaderModule*> Miss", "未命中着色器（可多个）")
D.Field("std::vector<FRHIShaderModule*> ClosestHit", "最近命中着色器")
D.Field("std::vector<FRHIShaderModule*> AnyHit", "任意命中着色器（可选）")
D.Field("std::vector<FRHIShaderModule*> Intersection", "相交着色器（可选，需扩展特性）")
D.Field("std::vector<FRHIShaderModule*> Callable", "可调用着色器（可选）")
D.Field("std::vector<const char*> EntryPoints", "每个模块的入口名（顺序与上面各组拼接一致）")
D.Field("FRHIPipelineLayout* Layout", "管线布局（SBT / 描述符）")
D.Field("std::uint32_t MaxRecursionDepth = 1", "最大递归深度（越大越贵）")

D.Class("FRHIRayTracingPipeline", Base="FRHIResource", Desc="光追管线句柄（设备无光追时不创建）。")
D.SetAccess("public")
D.Interface("[[nodiscard]] const char* GetTypeName() const override", "返回 `\"FRHIRayTracingPipeline\"`")
D.Interface("[[nodiscard]] ERHIResourceType GetType() const override",
            "返回 `ERHIResourceType::RayTracingPipeline`")

D.Struct("FRHIDescriptorPoolSize", Desc="描述符池的一种类型的容量声明。")
D.Field("ERHIDescriptorType Type = ERHIDescriptorType::UniformBuffer", "描述符类型")
D.Field("std::uint32_t Count = 0", "该类型可分配的数量上限")

D.Struct("FRHIDescriptorPoolDesc", Desc="描述符池描述：set 上限 + 各类型容量 + 是否允许边更新边用。"
        "**容量必须预付**（池内的分配是 O(1) 但从池里借，借完就只能新建池）。")
D.Field("std::uint32_t MaxSets = 0", "可分配的 set 上限")
D.Field("std::vector<FRHIDescriptorPoolSize> PoolSizes", "各类型的容量清单")
D.Field("bool bUpdateAfterBind = false", "允许在执行中更新（需设备特性 / 扩展）")

D.Class("FRHIDescriptorPool", Base="FRHIResource", Desc="描述符池句柄（set 的分配来源，也是释放单位）。")
D.SetAccess("public")
D.Interface("[[nodiscard]] const char* GetTypeName() const override", "返回 `\"FRHIDescriptorPool\"`")
D.Interface("[[nodiscard]] ERHIResourceType GetType() const override",
            "返回 `ERHIResourceType::DescriptorPool`")

D.Class("FRHIDescriptorSet", Base="FRHIResource", Desc="描述符集句柄（一组绑定的实际内容）。"
        "内容可以重写（同一 set 换纹理），但**被在飞的提交引用时不能重写** —— "
        "环形 / 分槽的同步责任属于调用方。")
D.SetAccess("public")
D.Interface("[[nodiscard]] const char* GetTypeName() const override", "返回 `\"FRHIDescriptorSet\"`")
D.Interface("[[nodiscard]] ERHIResourceType GetType() const override",
            "返回 `ERHIResourceType::DescriptorSet`")

D.Class("FRHIFramebuffer", Base="FRHIResource", Desc="帧缓冲句柄（渲染通道 + 一组附件视图 + 尺寸）。"
        "除交换链帧缓冲（RHI 私有）外，离屏 pass 的帧缓冲由上层用纹理视图自己组装。")
D.SetAccess("public")
D.Interface("[[nodiscard]] const char* GetTypeName() const override", "返回 `\"FRHIFramebuffer\"`")
D.Interface("[[nodiscard]] ERHIResourceType GetType() const override",
            "返回 `ERHIResourceType::Framebuffer`")

D.Class("FRHIRenderPass", Base="FRHIResource", Desc="渲染通道句柄（附件格式 + load / store 语义）。"
        "**与 PSO 绑死**：通道的附件格式决定哪些管线可以在这个通道里用。")
D.SetAccess("public")
D.Interface("[[nodiscard]] const char* GetTypeName() const override", "返回 `\"FRHIRenderPass\"`")
D.Interface("[[nodiscard]] ERHIResourceType GetType() const override", "返回 `ERHIResourceType::RenderPass`")

D.Struct("FRHIRenderPassAttachment", Desc="渲染通道里的一个颜色附件：格式 + 采样数 + 进出语义。"
        "`LoadOp::Clear` 之外的清理值由 `BeginRenderPass` 的参数给（通道不含具体清理值）。")
D.Field("ERHIFormat Format = ERHIFormat::B8G8R8A8_UNORM", "附件格式")
D.Field("std::uint32_t SampleCount = 1", "采样数")
D.Field("ERHILoadOp LoadOp = ERHILoadOp::Clear", "进入时的 load 语义")
D.Field("ERHIStoreOp StoreOp = ERHIStoreOp::Store", "离开时的 store 语义")

D.Struct("FRHIRenderPassDesc", Desc="渲染通道描述：颜色附件列表 + 可选深度格式 + 采样数。")
D.Field("std::vector<FRHIRenderPassAttachment> ColorAttachments", "颜色附件（支持 MRT）")
D.Field("ERHIFormat DepthFormat = ERHIFormat::Unknown", "深度格式（Unknown = 无深度附件）")
D.Field("std::uint32_t SampleCount = 1", "整体采样数（必须与各附件一致）")

D.Struct("FRHIFramebufferDesc", Desc="帧缓冲描述：渲染通道 + 附件视图（顺序与通道一致）+ 尺寸。")
D.Field("FRHIRenderPass* RenderPass", "配套渲染通道")
D.Field("std::vector<FRHITextureView*> Attachments", "附件视图（顺序与通道的附件列表对应）")
D.Field("std::uint32_t Width", "宽")
D.Field("std::uint32_t Height", "高")

D.Class("FRHICommandPool", Base="FRHIResource", Desc="命令池句柄（命令缓冲区的分配 / 重置单位）。")
D.SetAccess("public")
D.Interface("[[nodiscard]] const char* GetTypeName() const override", "返回 `\"FRHICommandPool\"`")
D.Interface("[[nodiscard]] ERHIResourceType GetType() const override", "返回 `ERHIResourceType::CommandPool`")

D.Class("FRHIFence", Base="FRHIResource", Desc="栅栏句柄（CPU 可等待的完成信号）。典型用法："
        "提交时挂上栅栏 → 下一帧先查 `IsFenceSignaled`（非阻塞）把已完成的帧回收。")
D.SetAccess("public")
D.Interface("[[nodiscard]] const char* GetTypeName() const override", "返回 `\"FRHIFence\"`")
D.Interface("[[nodiscard]] ERHIResourceType GetType() const override", "返回 `ERHIResourceType::Fence`")

D.Class("FRHISemaphore", Base="FRHIResource", Desc="信号量句柄（GPU 侧队列间 / 帧间同步，CPU 不可等待）。")
D.SetAccess("public")
D.Interface("[[nodiscard]] const char* GetTypeName() const override", "返回 `\"FRHISemaphore\"`")
D.Interface("[[nodiscard]] ERHIResourceType GetType() const override", "返回 `ERHIResourceType::Semaphore`")

D.Class("FRHIQueryPool", Base="FRHIResource", Desc="查询池句柄（遮挡 / 时间戳结果容器）。")
D.SetAccess("public")
D.Interface("[[nodiscard]] const char* GetTypeName() const override", "返回 `\"FRHIQueryPool\"`")
D.Interface("[[nodiscard]] ERHIResourceType GetType() const override", "返回 `ERHIResourceType::QueryPool`")

D.Card("ERHIRayTracingStructureType", "加速结构的层级（决定 build 的输入类型）。")
D.Table("枚举值", "说明")
D.Row("TopLevel = 0", "TLAS：实例数组（引用 BLAS + 变换）")
D.Row("BottomLevel = 1", "BLAS：几何（顶点 / 索引缓冲）")

D.Struct("FRHIRayTracingGeometry", Desc="BLAS 中的一个几何（三角形数据来自顶点 / 索引缓冲）。"
        "`bOpaque` 直接映射 `VK_GEOMETRY_OPAQUE_BIT`：置位可省掉 any-hit 调用。")
D.Field("FRHIBuffer* VertexBuffer", "顶点缓冲")
D.Field("std::uint64_t VertexBufferOffset", "顶点数据起始偏移")
D.Field("std::uint32_t VertexCount", "顶点数")
D.Field("std::uint32_t VertexStride = 12", "每顶点字节数（默认 3 个 float）")
D.Field("FRHIBuffer* IndexBuffer", "索引缓冲")
D.Field("std::uint64_t IndexBufferOffset", "索引数据起始偏移")
D.Field("std::uint32_t IndexCount", "索引数")
D.Field("bool bIndex32 = true", "索引位宽（true = 32 位）")
D.Field("bool bOpaque = true", "不透明几何（置位则不调用 any-hit）")

D.Struct("FRHIRayTracingGeometryDesc", Desc="加速结构描述：BLAS 的几何列表 / TLAS 的实例列表，"
        "外加 refit 与压缩两个可选能力。")
D.Field("std::vector<FRHIRayTracingGeometry> Geometries", "几何列表（BLAS）")
D.Field("bool bAllowUpdate = false", "允许 BLAS refit（用顶点的更新值重建）")
D.Field("bool bAllowCompaction = false", "允许压缩（build 后拷贝到更小的显存）")

D.Struct("FRHIRayTracingInstance", Desc="TLAS 的一个实例：变换 + 指向 BLAS 的缓冲 + SBT 偏移。"
        "**变换是 12 个 float、行主序的转置世界矩阵**（列 0/1/2 各 3 个 float + 第 4 列平移），"
        "正好是 GPU 实例结构要求的布局。")
D.Field("std::uint32_t InstanceId", "实例 ID（着色器里可读，用于索引自定义数据）")
D.Field("std::uint32_t InstanceMask = 0xFF", "可见性掩码（与 ray mask 相与）")
D.Field("std::uint32_t SbtOffset", "该实例的 SBT 记录偏移")
D.Field("FRHIBuffer* AccelerationStructure", "实例化的加速结构缓冲（TLAS 用）")
D.Field("float Transform[12]", "行主序 12 浮点变换（列 0/1/2 各 3 个 + 第 4 列平移），默认单位阵")

D.Class("FRHIAccelerationStructure", Base="FRHIResource",
        Desc="加速结构句柄（BLAS / TLAS）。创建只分配显存，**真正的 build 在命令列表上**"
             "（`FRHICommandList::BuildAccelerationStructure`）—— 这样 build 能与其他 GPU 工作"
             "一起录进同一张列表，不需要 CPU 侧等待。")
D.SetAccess("public")
D.Interface("[[nodiscard]] const char* GetTypeName() const override",
            "返回 `\"FRHIAccelerationStructure\"`")
D.Interface("[[nodiscard]] ERHIResourceType GetType() const override",
            "返回 `ERHIResourceType::AccelerationStructure`")
D.Interface("[[nodiscard]] const FRHIRayTracingGeometryDesc& GetGeometryDesc() const",
            "创建时用的几何 / 实例描述（build 时需要，故随对象保存）")
D.SetAccess("protected")
D.Field("FRHIRayTracingGeometryDesc GeometryDesc", "创建时保存的描述（protected ⇒ 后端派生类写入）")

D.Struct("FRHISbtRecord", Desc="SBT 里的一条记录：模块 + 入口。`Module` 为空表示空记录"
        "（miss 段落需要占位时用）。")
D.Field("FRHIShaderModule* Module", "着色器模块（null = 空记录）")
D.Field("const char* EntryPoint = \"main\"", "入口名")

D.Struct("FRHISbtGroup", Desc="SBT 的一个阶段分组：阶段 + 该阶段的记录列表。"
        "`CreateShaderBindingTable` 按这些分组铺出一整块 DeviceAddress 缓冲。")
D.Field("ERHIShaderStage Stage = ERHIShaderStage::RayGen", "分组对应的阶段")
D.Field("std::vector<FRHISbtRecord> Records", "记录列表（rayGen / miss / callable：每记录一个 shader；"
                                               "hit：每命中组一个）")

D.Struct("FRHIDescriptorWrite", Desc="一次描述符写入：目标 set + binding + 具体资源。"
        "**同一个结构同时能表达缓冲、图像、采样器、加速结构**，靠 `Type` 选择哪个字段有效。")
D.Field("FRHIDescriptorSet* Set = nullptr", "目标 set")
D.Field("std::uint32_t Binding = 0", "目标 binding 号")
D.Field("std::uint32_t ArrayIndex = 0", "数组元素下标")
D.Field("ERHIDescriptorType Type = ERHIDescriptorType::UniformBuffer", "类型（决定下面哪个字段有效）")
D.Field("FRHIBuffer* Buffer = nullptr", "缓冲（Uniform / Storage / Dynamic* 时用）")
D.Field("std::uint64_t Offset = 0", "缓冲内偏移")
D.Field("std::uint64_t Range = 0", "缓冲内长度（0 = 到末尾）")
D.Field("FRHITextureView* TextureView = nullptr", "纹理视图（SampledImage / StorageImage 时用）")
D.Field("FRHISampler* Sampler = nullptr", "采样器（Sampler / CombinedImageSampler 时用）")

# ══════════════════════════════════════════════════════════════════════════════
# Public/RHI/RHICommandList.h —— 队列 + 命令录制面
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/RHI/RHICommandList.h", Title="RHICommandList.h —— 队列 + 命令录制面",
         Desc="GPU 工作的**录制侧**：`FRHIQueue`（提交端点）与 `FRHICommandList`（≈ VkCommandBuffer）。"
              "整层就是「一张列表，按顺序录命令」的模型 —— 栅格 / 计算 / 传输共用同一个接口，"
              "能力差异由 `GetType()` 划界，非法调用在 Debug 断言（运行期不静默忽略，因为静默忽略"
              "只会把错误推迟到「画面不对」这种难查的形态）。\n"
              "**为什么描述符写入也挂在命令列表上**（`UpdateDescriptorSet`）：它底层不是录制的 `vkCmd`，"
              "而是立刻生效的 `vkUpdateDescriptorSets` —— 之所以放在这里，是为了让「写 set」与"
              "本 pass 的其他命令在同一处编排，顺序一眼可见；写的时候 set 不能被在飞的提交引用，"
              "这一点由调用方（RDG）用环形 / 分槽保证。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("RHIAPI.h", "`MAHO_RHI_API`：接口族跨 DLL")
D.Row("RHI/RHIEnums.h", "命令列表类型 / 资源状态 / 阶段等枚举")
D.Row("RHI/RHIResources.h", "命令的参数类型（缓冲 / 纹理 / 管线 / 查询池…）")
D.Row("cstdint", "定宽整数参数（偏移 / 计数 / 尺寸）")

D.Struct("FRHIRenderingAttachmentInfo", Desc="动态渲染（`BeginRendering`）的单个附件：视图 + load / store + "
        "清理色。是渲染通道对象的**轻量替代**：不需要预先创建 `FRHIRenderPass`，"
        "适合「附件组合随帧变化」的 pass。")
D.Field("FRHITextureView* View = nullptr", "附件视图")
D.Field("ERHILoadOp LoadOp = ERHILoadOp::Clear", "进入时的 load 语义")
D.Field("ERHIStoreOp StoreOp = ERHIStoreOp::Store", "离开时的 store 语义")
D.Field("float ClearColor[4]", "清理色（LoadOp = Clear 时用），默认不透明黑")

D.Class("FRHIQueue", Desc="逻辑提交端点（Graphics / Compute / Transfer）。三个端点**永远都存在**："
        "即使后端把传输 / 计算映射到图形族的同一个原生队列（`IsNativeFallback()` 为真），"
        "上层也不需要写分歧代码 —— 分歧只在 `FRHI::Submit` 的路由里出现一次。")
D.SetAccess("public")
D.Interface("virtual ~FRHIQueue() = default", "虚析构（句柄由后端持有，引用返回，不删除）")
D.Interface("[[nodiscard]] virtual ERHIQueueType GetType() const = 0", "本端点的逻辑类型")
D.Interface("[[nodiscard]] virtual bool IsNativeFallback() const",
            "是否复用了非独立原生队列（仅供调试 / 日志；默认 false）")
D.Interface("virtual void Submit(FRHICommandList* const* CmdLists, std::uint32_t Count, "
            "FRHISemaphore* const* WaitSemaphores, std::uint32_t WaitCount, "
            "FRHISemaphore* const* SignalSemaphores, std::uint32_t SignalCount, FRHIFence* SignalFence) = 0",
            "批量提交一组命令列表：等待 / 发出信号量 + 可选栅栏。**调用方保证串行**"
            "（队列提交本身要串行化，引擎的设计是把串行责任放在 RDG 而不是在这里加锁）")

D.Class("FRHICommandList", Desc="命令录制面（≈ VkCommandBuffer）。一个实例只属于一次录制 ——"
        "**绝不跨任务共享**（Vulkan 禁止并发录制进同一个命令缓冲）：并行录制时"
        "`IRHI::EnqueueTask` 给每个任务自己的列表。录制顺序即执行顺序，引擎不在这里做重排。")
D.SetAccess("public")
D.Interface("virtual ~FRHICommandList() = default", "虚析构：经基类指针销毁（生命周期归调用方 / RDG）")
D.Interface("[[nodiscard]] virtual ERHICommandListType GetType() const = 0",
            "本列表的类型；决定下面哪些命令合法")
D.Interface("virtual void Begin() = 0", "开始录制（必须与 `End` 配对）")
D.Interface("virtual void End() = 0", "结束录制（此后才能提交）")
D.Interface("virtual void CopyBuffer(FRHIBuffer* Src, std::uint64_t SrcOffset, FRHIBuffer* Dst, "
            "std::uint64_t DstOffset, std::uint64_t Size) = 0",
            "缓冲间拷贝（传输命令；图形 / 计算列表也允许）")
D.Interface("virtual void CopyBufferToTexture(FRHIBuffer* Src, FRHITexture* Dst, std::uint64_t SrcOffset) = 0",
            "缓冲 → 纹理（上传贴图的常用入口）")
D.Interface("virtual void CopyTextureToBuffer(FRHITexture* Src, FRHIBuffer* Dst, std::uint64_t DstOffset) = 0",
            "纹理 → 缓冲（回读 / 截图）")
D.Interface("virtual void FillBuffer(FRHIBuffer* Buffer, std::uint64_t Offset, std::uint64_t Size, "
            "std::uint32_t Data) = 0",
            "用 32 位模式填充缓冲（清零 / 填初值，比上传便宜）")
D.Interface("virtual void UpdateBuffer(FRHIBuffer* Buffer, std::uint64_t Offset, std::uint64_t Size, "
            "const void* Data) = 0",
            "把 CPU 数据写进缓冲。**host 可见就直写、设备本地就走 staging 拷贝**——"
            "这个选择在后端内部完成，调用点不需要知道；`Data` 只需活到录制结束（直写路径）"
            "或拷贝命令完成（staging 路径，由延迟回收保证）")
D.Interface("virtual void TransitionBuffer(FRHIBuffer* Buffer, ERHIResourceState OldState, "
            "ERHIResourceState NewState) = 0",
            "缓冲状态转换（屏障）。**必须显式声明**：引擎没有全局状态追踪，"
            "漏一次转换就是「读到的内容是上一帧的」这类静默错误")
D.Interface("virtual void TransitionTexture(FRHITexture* Texture, ERHIResourceState OldState, "
            "ERHIResourceState NewState) = 0",
            "纹理状态转换（布局 + 读写域屏障），理由同上")
D.Interface("virtual void BeginRenderPass(FRHIRenderPass* RenderPass, FRHIFramebuffer* Framebuffer, "
            "std::uint32_t Width, std::uint32_t Height, const float ClearColor[4], "
            "bool bHasDepthStencil = false, float DepthClear = 1.0f, std::uint32_t StencilClear = 0) = 0",
            "开始传统渲染通道（**仅图形列表**）：通道对象 + 帧缓冲 + 尺寸 + 清理值。"
            "深度清理值默认 1.0（标准反向深度前的约定）")
D.Interface("virtual void EndRenderPass() = 0", "结束渲染通道（与 Begin 配对）")
D.Interface("virtual void BeginRendering(const FRHIRenderingAttachmentInfo* ColorAttachments, "
            "std::uint32_t ColorCount, const FRHIRenderingAttachmentInfo* DepthAttachment, "
            "std::uint32_t Width, std::uint32_t Height) = 0",
            "开始动态渲染：附件由参数直接给（不需要预建通道 / 帧缓冲），"
            "深度附件传 nullptr 表示无深度")
D.Interface("virtual void EndRendering() = 0", "结束动态渲染")
D.Interface("virtual void SetViewport(float X, float Y, float Width, float Height, "
            "float MinDepth = 0.0f, float MaxDepth = 1.0f) = 0",
            "设置视口（默认满深度域）")
D.Interface("virtual void SetScissor(std::int32_t X, std::int32_t Y, std::uint32_t Width, "
            "std::uint32_t Height) = 0",
            "设置裁剪矩形（UI / 局部重绘用）")
D.Interface("virtual void BindGraphicsPipeline(FRHIGraphicsPipeline* Pipeline) = 0",
            "绑定栅格管线（**必须与当前渲染通道的附件格式兼容**）")
D.Interface("virtual void BindVertexBuffer(std::uint32_t Binding, FRHIBuffer* Buffer, "
            "std::uint64_t Offset = 0) = 0",
            "绑定顶点缓冲（Binding 对应管线描述里的顶点输入槽）")
D.Interface("virtual void BindIndexBuffer(FRHIBuffer* Buffer, std::uint64_t Offset = 0, "
            "bool bIndex32 = true) = 0",
            "绑定索引缓冲（位宽必须与索引数据一致）")
D.Interface("virtual void Draw(std::uint32_t VertexCount, std::uint32_t InstanceCount = 1, "
            "std::uint32_t FirstVertex = 0, std::uint32_t FirstInstance = 0) = 0",
            "非索引绘制（程序化几何 / 全屏三角）")
D.Interface("virtual void DrawIndexed(std::uint32_t IndexCount, std::uint32_t InstanceCount = 1, "
            "std::uint32_t FirstIndex = 0, std::int32_t VertexOffset = 0, "
            "std::uint32_t FirstInstance = 0) = 0",
            "索引绘制（常规网格；`VertexOffset` 是**有符号**的，可为负以复用顶点区间）")
D.Interface("virtual void DrawIndirect(FRHIBuffer* ArgsBuffer, std::uint64_t ArgsOffset, "
            "std::uint32_t DrawCount = 1, std::uint32_t Stride = 0) = 0",
            "间接绘制：参数来自缓冲（GPU 驱动管线的基元）")
D.Interface("virtual void DrawIndexedIndirect(FRHIBuffer* ArgsBuffer, std::uint64_t ArgsOffset, "
            "std::uint32_t DrawCount = 1, std::uint32_t Stride = 0) = 0",
            "间接索引绘制")
D.Interface("virtual void DrawIndirectCount(FRHIBuffer* ArgsBuffer, std::uint64_t ArgsOffset, "
            "FRHIBuffer* CountBuffer, std::uint64_t CountOffset, std::uint32_t MaxDrawCount, "
            "std::uint32_t Stride = 0) = 0",
            "**GPU 决定绘制条数**：count 从 GPU 写入的缓冲读（配合 `MaxDrawCount` 上界，"
            "避免读到未初始化值导致越界）")
D.Interface("virtual void DrawIndexedIndirectCount(FRHIBuffer* ArgsBuffer, std::uint64_t ArgsOffset, "
            "FRHIBuffer* CountBuffer, std::uint64_t CountOffset, std::uint32_t MaxDrawCount, "
            "std::uint32_t Stride = 0) = 0",
            "索引版 GPU 决定条数")
D.Interface("virtual void BindComputePipeline(FRHIComputePipeline* Pipeline) = 0", "绑定计算管线")
D.Interface("virtual void Dispatch(std::uint32_t GroupCountX, std::uint32_t GroupCountY, "
            "std::uint32_t GroupCountZ) = 0",
            "派发计算（参数是**组数**，不是线程数）")
D.Interface("virtual void DispatchIndirect(FRHIBuffer* ArgsBuffer, std::uint64_t ArgsOffset) = 0",
            "间接派发（组数来自缓冲）")
D.Interface("virtual void BindDescriptorSets(std::uint32_t FirstSet, FRHIDescriptorSet* const* Sets, "
            "std::uint32_t Count, const std::uint32_t* DynamicOffsets = nullptr, "
            "std::uint32_t DynamicOffsetCount = 0) = 0",
            "在**绝对 set 号**处绑定一列 set。`DynamicOffsets` 只喂动态描述符"
            "（`DynamicUniform` / `DynamicStorage`）：每个偏移为绑定选一段切片；"
            "布局里没有动态描述符就传 nullptr / 0")
D.Interface("virtual void PushConstants(ERHIShaderStage Stages, std::uint32_t Offset, "
            "std::uint32_t Size, const void* Data) = 0",
            "推入小常量（区间必须在管线布局里声明过）")
D.Interface("virtual void UpdateDescriptorSet(FRHIDescriptorSet* Set, const FRHIDescriptorWrite* Writes, "
            "std::uint32_t WriteCount) = 0",
            "**录制期**写入 set 内容（底层是立刻生效的 `vkUpdateDescriptorSets`，不是录制的 `vkCmd`）。"
            "动态 set（每帧 / 每 pass）在这里写一次，随后再绑定；"
            "写的时候该 set **不能**被在飞的提交引用 —— 环形 / 分槽安全由调用方负责")
D.Interface("virtual void BeginQuery(FRHIQueryPool* Pool, std::uint32_t QueryIndex) = 0",
            "开始查询（遮挡：区间内是否有像素通过）")
D.Interface("virtual void EndQuery(FRHIQueryPool* Pool, std::uint32_t QueryIndex) = 0", "结束查询")
D.Interface("virtual void WriteTimestamp(FRHIQueryPool* Pool, std::uint32_t QueryIndex) = 0",
            "写入时间戳（同池内多次写入可算区间耗时）")
D.Interface("virtual void ResetQueryPool(FRHIQueryPool* Pool, std::uint32_t FirstQuery, "
            "std::uint32_t QueryCount) = 0",
            "重置查询区间（**复用池必须重置**，否则结果是上一帧的）")
D.Interface("virtual void BuildAccelerationStructure(FRHIAccelerationStructure* Accel, "
            "FRHIBuffer* ScratchBuffer, std::uint64_t ScratchOffset) = 0",
            "构建加速结构（BLAS / TLAS）。`ScratchBuffer` 必须带 `DeviceAddress` + `Storage` 且"
            "大到满足 build 需求（可先用 `GetAccelerationStructureBuildSizes` 问）")
D.Interface("virtual void CopyAccelerationStructure(FRHIAccelerationStructure* Dst, "
            "FRHIAccelerationStructure* Src) = 0",
            "加速结构拷贝（压缩 / refit 结果搬运）")
D.Nested("FRHIRayTracingSbt", Kind="struct",
         Desc="SBT 的 GPU 侧布局：整块缓冲 + 三个段落各自（偏移, 跨距）。"
              "由 `CreateShaderBindingTable` 填好，直接喂给 `TraceRays`。")
D.Interface("virtual void TraceRays(FRHIRayTracingPipeline* Pipeline, const FRHIRayTracingSbt& Sbt, "
            "std::uint32_t Width, std::uint32_t Height, std::uint32_t Depth = 1) = 0",
            "发射光线（尺寸 = 光线数，模板参数默认为 1 维）")

D.Field("FRHIBuffer* SbtBuffer = nullptr", "整块 SBT 缓冲（DeviceAddress + Storage）")
D.Field("std::uint32_t RayGenOffset", "rayGen 段偏移")
D.Field("std::uint32_t RayGenStride", "rayGen 段跨距")
D.Field("std::uint32_t HitOffset", "命中段偏移")
D.Field("std::uint32_t HitStride", "命中段跨距")
D.Field("std::uint32_t MissOffset", "miss 段偏移")
D.Field("std::uint32_t MissStride", "miss 段跨距")

# ══════════════════════════════════════════════════════════════════════════════
# Public/RHI/RHIServer.h —— IRHI 能力面 + FRHI 渲染服务器
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/RHI/RHIServer.h", Title="RHIServer.h —— IRHI 能力面 + FRHI 渲染服务器",
         Desc="RHI 层对外的**唯一入口**。两层结构：\n"
              "**`IRHI`** = 能力面（纯接口）：命令列表工厂 / 提交 / 帧原语 / 资源工厂 / 查询 / 光追。"
              "上层（RDG、渲染插件）只经它说话，因此**后端无关**。\n"
              "**`FRHI`** = 实现 + 宿主：它是 `FThreadedServer`（有自己的常驻线程），"
              "**不是被调度的 stage** —— 引擎的图调度 stage，不调度「一个服务器」。"
              "它私有持有 `IDynamicRHI`（Vulkan 设备）并把它逐个转发出去；"
              "设备类型（`IDynamicRHI`）**永远不离开这个 DLL**。\n"
              "**为什么它是「无状态异步处理器」**：它接收任务、处理任务，**从不拒绝工作**。"
              "「现在该不该提交」「要不要退出」是应用层 / RDG 的调度判断，"
              "把这类判断塞进 RHI 会让它变成第二个调度器。")
D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("RHIAPI.h", "`MAHO_RHI_API`")
D.Row("Core/Singleton.h", "单例标记基类（本层不使用 `Get()` 语义，只为了与引擎侧风格一致的类型锚）")
D.Row("Core/ThreadPool.h", "`FThreadPool RecordingPool`：串行录制池（见成员说明）")
D.Row("Core/ThreadedServer.h", "`FThreadedServer`：常驻专用线程 + FIFO 队列（FRHI 的基类）")
D.Row("Maho.h", "引擎聚合头：日志 / 断言 / 基础类型")
D.Row("RHI/RHICommandList.h", "命令列表与队列（`IRHI` 的大部分签名用到）")
D.Row("RHI/RHIEnums.h", "后端 / 队列 / 格式 / 查询类型枚举")
D.Row("RHI/RHIResources.h", "资源类型与描述（工厂签名用到）")
D.Row("condition_variable / mutex", "线程原语（服务器线程的唤醒与互斥）")
D.Row("cstdint / limits / memory", "定宽整数 / `std::uint64_t` 最大值（栅栏无限等待的默认超时）/ `unique_ptr`")

D.Struct("IRHI", Base="（纯接口）",
         Desc="RHI 的公开能力面。**每个资源工厂都成对**（`Create*` / `Destroy*`）—— "
              "因为生命周期归调用方：命令列表的 Begin / End / Submit 时机由 RDG 决定（它拥有帧隔离），"
              "RHI 只提供对象本身。**交换链不出现在这里**：特性渲染到离屏纹理，"
              "再交给 `PresentTexture` blit 上屏，所以上层只看得见格式与尺寸。")
D.SetAccess("public")
D.Interface("virtual ~IRHI() = default", "虚析构（宿主经基类持有）")
D.Interface("[[nodiscard]] virtual FRHICommandList* CreateCommandList(ERHICommandListType Type) = 0",
            "建一张命令列表。**生命周期归调用方**（如拥有帧隔离的 RDG），RHI 只给裸对象")
D.Interface("virtual void DestroyCommandList(FRHICommandList* CmdList) = 0", "销毁命令列表")
D.Interface("virtual void Submit(FRHICommandList* CmdList, ERHICommandListType Type = "
            "ERHICommandListType::Graphics, FRHISemaphore* const* WaitSemaphores = nullptr, "
            "std::uint32_t WaitCount = 0, FRHISemaphore* const* SignalSemaphores = nullptr, "
            "std::uint32_t SignalCount = 0, FRHIFence* SignalFence = nullptr) = 0",
            "提交一张已录制的列表，按类型路由到对应队列（原生队列不独立时自动回落到图形队列）。"
            "**何时提交是调用方的调度决定**，这里只做队列提交本身")
D.Interface("virtual void EnqueueTask(FRHICommandList* CmdList, "
            "std::function<void(FRHICommandList*)> Task) = 0",
            "在池上跑一个录制任务（并行录制）。回调拿到**自己那张**命令列表 —— "
            "绝不跨任务共享列表（Vulkan 禁止并发录同一个缓冲）。任务跑完由调用方串行 `Submit`")
D.Interface("virtual void Flush() = 0",
            "屏障：等此前所有 `EnqueueTask` 都录完。**并行录制时必须先 Flush 再 Submit**，"
            "否则会出现「边录边提交」")
D.Interface("virtual void BeginFrame() = 0",
            "帧原语（在 `EnqueueTask` 内调用 ⇒ 跑在服务器线程上）：取得交换链镜像并开始帧命令缓冲")
D.Interface("virtual void EndFrame() = 0", "结束并提交帧命令缓冲 + 呈现（与 `BeginFrame` 配对）")
D.Interface("virtual void Resize(int Width, int Height) = 0", "交换链 / 帧缓冲尺寸变化（重建资源）")
D.Interface("virtual void WaitIdle() = 0",
            "设备空闲：等所有已提交 GPU 工作完成。**销毁可能被在飞命令缓冲引用的资源之前必须调**"
            "（引擎关闭 / 交换链拆除）")
D.Interface("[[nodiscard]] virtual FRHICommandList* GetFrameCommandList() = 0",
            "借用帧命令缓冲作为录制面（已被 `BeginFrame` 开始、由 `EndFrame` 结束并提交）——"
            "特性只往里录 pass / draw。**非持有**：绝不调它的 Begin / End")
D.Interface("virtual void PresentTexture(FRHITexture* Src) = 0",
            "把离屏纹理 blit 到当前交换链后缓冲（在帧命令缓冲上做）。"
            "**必须在所有场景录制之后、`EndFrame` 之前**调用；"
            "交换链的渲染通道 / 帧缓冲始终是 RHI 私有的")
D.Interface("[[nodiscard]] virtual ERHIFormat GetSwapchainFormat() const = 0",
            "当前交换链格式 —— 离屏目标必须与它一致（blit 要求格式相同）")
D.Interface("[[nodiscard]] virtual bool IsInitialized() const = 0", "设备是否已就绪")
D.Interface("[[nodiscard]] virtual FRHIFence* CreateFence(bool bSignaled) = 0",
            "建栅栏（可指定初始是否已触发）")
D.Interface("virtual void DestroyFence(FRHIFence* Fence) = 0", "销毁栅栏")
D.Interface("virtual void WaitForFence(FRHIFence* Fence, std::uint64_t TimeoutNs = "
            "(std::numeric_limits<std::uint64_t>::max)()) = 0",
            "阻塞等待栅栏（默认无限超时）")
D.Interface("[[nodiscard]] virtual bool IsFenceSignaled(FRHIFence* Fence) = 0",
            "非阻塞查询（`vkGetFenceStatus`）—— 帧回收循环靠它做到不卡主线程")
D.Interface("[[nodiscard]] virtual FRHISemaphore* CreateGpuSemaphore() = 0", "建 GPU 信号量")
D.Interface("virtual void DestroyGpuSemaphore(FRHISemaphore* Semaphore) = 0", "销毁信号量")
D.Interface("[[nodiscard]] virtual FRHIBuffer* CreateBuffer(const FRHIBufferDesc& Desc) = 0", "建缓冲")
D.Interface("virtual void DestroyBuffer(FRHIBuffer* Buffer) = 0", "销毁缓冲")
D.Interface("[[nodiscard]] virtual FRHITexture* CreateTexture(const FRHITextureDesc& Desc) = 0", "建纹理")
D.Interface("virtual void DestroyTexture(FRHITexture* Texture) = 0", "销毁纹理")
D.Interface("[[nodiscard]] virtual FRHISampler* CreateSampler(const FRHISamplerDesc& Desc) = 0", "建采样器")
D.Interface("virtual void DestroySampler(FRHISampler* Sampler) = 0", "销毁采样器")
D.Interface("[[nodiscard]] virtual FRHIShaderModule* CreateShaderModule(const FRHIShaderModuleDesc& Desc) = 0",
            "建着色器模块（模块是短命对象：PSO 建好后即可销毁）")
D.Interface("virtual void DestroyShaderModule(FRHIShaderModule* Module) = 0", "销毁着色器模块")
D.Interface("[[nodiscard]] virtual FRHIGraphicsPipeline* CreateGraphicsPipeline("
            "const FRHIGraphicsPipelineDesc& Desc) = 0",
            "建栅格管线（**很贵**：上层必须按描述 + 字节码哈希走 PSO 缓存）")
D.Interface("virtual void DestroyGraphicsPipeline(FRHIGraphicsPipeline* Pipeline) = 0", "销毁栅格管线")
D.Interface("[[nodiscard]] virtual FRHIComputePipeline* CreateComputePipeline("
            "const FRHIComputePipelineDesc& Desc) = 0",
            "建计算管线")
D.Interface("virtual void DestroyComputePipeline(FRHIComputePipeline* Pipeline) = 0", "销毁计算管线")
D.Interface("[[nodiscard]] virtual FRHIStructuredBuffer* CreateStructuredBuffer("
            "const FRHIStructuredBufferDesc& Desc) = 0",
            "建结构化缓冲")
D.Interface("virtual void DestroyStructuredBuffer(FRHIStructuredBuffer* Buffer) = 0", "销毁结构化缓冲")
D.Interface("[[nodiscard]] virtual FRHIBufferView* CreateBufferView(const FRHIBufferViewDesc& Desc) = 0",
            "建缓冲视图")
D.Interface("virtual void DestroyBufferView(FRHIBufferView* View) = 0", "销毁缓冲视图")
D.Interface("[[nodiscard]] virtual FRHITextureView* CreateTextureView(const FRHITextureViewDesc& Desc) = 0",
            "建纹理视图（渲染通道附件 / 描述符绑定用）")
D.Interface("virtual void DestroyTextureView(FRHITextureView* View) = 0", "销毁纹理视图")
D.Interface("[[nodiscard]] virtual FRHIDescriptorSetLayout* CreateDescriptorSetLayout("
            "const FRHIDescriptorSetLayoutDesc& Desc) = 0",
            "建 set 布局")
D.Interface("virtual void DestroyDescriptorSetLayout(FRHIDescriptorSetLayout* Layout) = 0", "销毁 set 布局")
D.Interface("[[nodiscard]] virtual FRHIPipelineLayout* CreatePipelineLayout("
            "const FRHIPipelineLayoutDesc& Desc) = 0",
            "建管线布局")
D.Interface("virtual void DestroyPipelineLayout(FRHIPipelineLayout* Layout) = 0", "销毁管线布局")
D.Interface("[[nodiscard]] virtual FRHIDescriptorPool* CreateDescriptorPool("
            "const FRHIDescriptorPoolDesc& Desc) = 0",
            "建描述符池")
D.Interface("virtual void DestroyDescriptorPool(FRHIDescriptorPool* Pool) = 0", "销毁描述符池")
D.Interface("[[nodiscard]] virtual FRHIDescriptorSet* AllocateDescriptorSet(FRHIDescriptorPool* Pool, "
            "FRHIDescriptorSetLayout* Layout) = 0",
            "从池里分配一个 set（池容量预付，借完就得新建池）")
D.Interface("virtual void FreeDescriptorSet(FRHIDescriptorPool* Pool, FRHIDescriptorSet* Set) = 0",
            "归还 set（归还给**创建它的池**）")
D.Interface("virtual void UpdateDescriptorSets(const FRHIDescriptorWrite* Writes, "
            "std::uint32_t Count) = 0",
            "设备级写入 set 内容（立即生效，不需要命令缓冲）。"
            "池的所有者通常在建 set 时就写一次，而不是推迟到某个 pass 里")
D.Interface("[[nodiscard]] virtual FRHIRenderPass* CreateRenderPass(const FRHIRenderPassDesc& Desc) = 0",
            "建渲染通道（附件格式 + load / store；与 PSO 强绑定）")
D.Interface("virtual void DestroyRenderPass(FRHIRenderPass* Pass) = 0", "销毁渲染通道")
D.Interface("[[nodiscard]] virtual FRHIFramebuffer* CreateFramebuffer(const FRHIFramebufferDesc& Desc) = 0",
            "建帧缓冲（通道 + 附件视图 + 尺寸）")
D.Interface("virtual void DestroyFramebuffer(FRHIFramebuffer* Framebuffer) = 0", "销毁帧缓冲")
D.Interface("[[nodiscard]] virtual std::uint32_t GetFramebufferWidth() const = 0",
            "交换链宽（离屏目标必须匹配）")
D.Interface("[[nodiscard]] virtual std::uint32_t GetFramebufferHeight() const = 0",
            "交换链高（离屏目标必须匹配）")
D.Interface("[[nodiscard]] virtual FRHIQueryPool* CreateQueryPool(ERHIQueryType Type, "
            "std::uint32_t QueryCount) = 0",
            "建查询池")
D.Interface("virtual void DestroyQueryPool(FRHIQueryPool* Pool) = 0", "销毁查询池")
D.Interface("virtual bool GetQueryPoolResults(FRHIQueryPool* Pool, std::uint32_t FirstQuery, "
            "std::uint32_t QueryCount, std::uint64_t* Results, std::size_t Stride, "
            "bool bWait = true) = 0",
            "取查询结果到 CPU 内存。`bWait` 为真会阻塞到结果可用 —— "
            "**属于同步读，不要在 RHI 线程上调**（会卡住整条渲染流水）")
D.Interface("[[nodiscard]] virtual FRHIRayTracingPipeline* CreateRayTracingPipeline("
            "const FRHIRayTracingPipelineDesc& Desc) = 0",
            "建光追管线（设备不支持时返回 nullptr）")
D.Interface("virtual void DestroyRayTracingPipeline(FRHIRayTracingPipeline* Pipeline) = 0", "销毁光追管线")
D.Interface("[[nodiscard]] virtual FRHIAccelerationStructure* CreateAccelerationStructure("
            "const FRHIRayTracingGeometryDesc& Desc) = 0",
            "建加速结构（BLAS / TLAS，只分配；build 在命令列表上做）。"
            "**设备无光追时返回 nullptr** —— 上层据此关掉光追路径")
D.Interface("virtual void DestroyAccelerationStructure(FRHIAccelerationStructure* Accel) = 0",
            "销毁加速结构")
D.Interface("virtual bool GetAccelerationStructureBuildSizes(const FRHIRayTracingGeometryDesc& Desc, "
            "std::uint64_t& OutAccelSize, std::uint64_t& OutScratchSize) = 0",
            "查询 build 所需尺寸（加速结构本体 + scratch），返回 false = 不支持 / 参数非法")
D.Interface("[[nodiscard]] virtual FRHIBuffer* CreateShaderBindingTable(FRHIRayTracingPipeline* Pipeline, "
            "const FRHISbtGroup* Groups, std::uint32_t GroupCount, "
            "std::uint32_t* OutRayGenOffset = nullptr, std::uint32_t* OutRayGenStride = nullptr, "
            "std::uint32_t* OutHitOffset = nullptr, std::uint32_t* OutHitStride = nullptr, "
            "std::uint32_t* OutMissOffset = nullptr, std::uint32_t* OutMissStride = nullptr) = 0",
            "按管线的阶段分组铺出 SBT，返回那块 GPU 缓冲（DeviceAddress 能力 + Storage 标记），"
            "并把各段的偏移 / 跨距回填给调用方（随后喂给 `TraceRays` 的 `FRHIRayTracingSbt`）")

D.Class("IDynamicRHI", Desc="前置声明：设备的**设备侧**抽象（真正定义在 Private/RHI.h）。"
        "在这里只出现名字，公开面因此完全不认识后端类型。")
D.Interface("（前向声明）", "`FRHI` 私有持有 `std::unique_ptr<IDynamicRHI>`，逐个方法转发")

D.Class("FRHI", Base="FThreadedServer, IRHI",
        Desc="**渲染服务器**：后端无关的 GPU 设备面。持有 `IDynamicRHI` 设备并实现 `IRHI`。"
             "渲染所有者（`FRender`）持有它，且只经 `IRHI` 的命令面交互"
             "（`EnqueueTask` / `Submit` / 帧原语）—— **渲染线程就是服务器线程**。\n"
             "命令录制经 `EnqueueTask` 并行（池）；队列提交与帧原语是直接调用，"
             "**串行责任交给调用方（RDG）**。后端无关：RDG / 渲染插件永远不碰具体后端类型，"
             "设备本身（`IDynamicRHI`）留在这个 DLL 里。")
D.SetAccess("public")
D.Interface("FRHI()", "构造：不初始化设备（`Initialize` 才连线程 + 设备）")
D.Interface("virtual ~FRHI() override", "虚析构：经基类指针释放（宿主 DLL 边界）")
D.Interface("bool Initialize(void* NativeWindowHandle, int Width, int Height, "
            "ERHIBackend Backend = ERHIBackend::Vulkan)",
            "起服务器（**先建设备再启动线程**）+ 初始化设备。窗口句柄为空 = 无头模式"
            "（跳过 RHI，返回 true）；宽高非法直接失败（不猜尺寸）")
D.Interface("void ShutdownRHI()", "先停服务器线程、再关设备并释放（幂等）—— 顺序不能反，"
            "否则设备析构时可能有在飞任务触碰它")
D.Interface("void EnqueueTask(FRHICommandList* CmdList, std::function<void(FRHICommandList*)> Task) override",
            "把录制任务丢进池。**池是单 worker（`RecordingPool{1}`）**：回调按 `EnqueueTask` 顺序执行，"
            "于是依赖彼此的特性（先清后画）提交顺序自然有序 —— 这条是刻意选的串行点")
D.Interface("void Flush() override",
            "排空所有待录制任务：保证「录完所有 → 再提交所有」的顺序")
D.Interface("[[nodiscard]] FRHICommandList* CreateCommandList(ERHICommandListType Type) override",
            "转发到设备建列表")
D.Interface("void DestroyCommandList(FRHICommandList* CmdList) override", "转发销毁列表")
D.Interface("void Submit(FRHICommandList* CmdList, ERHICommandListType Type = "
            "ERHICommandListType::Graphics, FRHISemaphore* const* WaitSemaphores = nullptr, "
            "std::uint32_t WaitCount = 0, FRHISemaphore* const* SignalSemaphores = nullptr, "
            "std::uint32_t SignalCount = 0, FRHIFence* SignalFence = nullptr) override",
            "按类型路由到对应队列；**计算 / 传输队列若回落到图形族，就提交到图形队列**，"
            "让它们与栅格工作天然串行（跨队列顺序不需要额外信号量）")
D.Interface("void BeginFrame() override", "帧原语：转发到设备（获取镜像 + 开帧缓冲区）")
D.Interface("void EndFrame() override", "帧原语：转发到设备（结束 + 提交 + 呈现）")
D.Interface("void Resize(int Width, int Height) override", "转发尺寸变化（宽高非法时忽略）")
D.Interface("void WaitIdle() override", "转发设备空闲等待（拆除前的必要动作）")
D.Interface("[[nodiscard]] FRHICommandList* GetFrameCommandList() override", "转发：借用帧命令缓冲（非持有）")
D.Interface("void PresentTexture(FRHITexture* Src) override", "转发：离屏纹理 blit 上屏")
D.Interface("[[nodiscard]] ERHIFormat GetSwapchainFormat() const override",
            "转发：交换链格式（设备未就绪时 `Unknown`）")
D.Interface("[[nodiscard]] bool IsInitialized() const override", "设备存在且已初始化")
D.Interface("[[nodiscard]] FRHIFence* CreateFence(bool bSignaled) override", "转发设备建栅栏")
D.Interface("void DestroyFence(FRHIFence* Fence) override", "转发销毁栅栏")
D.Interface("void WaitForFence(FRHIFence* Fence, std::uint64_t TimeoutNs) override", "转发等待栅栏")
D.Interface("[[nodiscard]] bool IsFenceSignaled(FRHIFence* Fence) override", "转发非阻塞查询")
D.Interface("[[nodiscard]] FRHISemaphore* CreateGpuSemaphore() override", "转发建信号量")
D.Interface("void DestroyGpuSemaphore(FRHISemaphore* Semaphore) override", "转发销毁信号量")
D.Interface("[[nodiscard]] FRHIBuffer* CreateBuffer(const FRHIBufferDesc& Desc) override", "转发建缓冲")
D.Interface("void DestroyBuffer(FRHIBuffer* Buffer) override", "转发销毁缓冲")
D.Interface("[[nodiscard]] FRHITexture* CreateTexture(const FRHITextureDesc& Desc) override", "转发建纹理")
D.Interface("void DestroyTexture(FRHITexture* Texture) override", "转发销毁纹理")
D.Interface("[[nodiscard]] FRHISampler* CreateSampler(const FRHISamplerDesc& Desc) override", "转发建采样器")
D.Interface("void DestroySampler(FRHISampler* Sampler) override", "转发销毁采样器")
D.Interface("[[nodiscard]] FRHIShaderModule* CreateShaderModule(const FRHIShaderModuleDesc& Desc) override",
            "转发建着色器模块")
D.Interface("void DestroyShaderModule(FRHIShaderModule* Module) override", "转发销毁着色器模块")
D.Interface("[[nodiscard]] FRHIGraphicsPipeline* CreateGraphicsPipeline("
            "const FRHIGraphicsPipelineDesc& Desc) override",
            "转发建栅格管线")
D.Interface("void DestroyGraphicsPipeline(FRHIGraphicsPipeline* Pipeline) override", "转发销毁栅格管线")
D.Interface("[[nodiscard]] FRHIComputePipeline* CreateComputePipeline("
            "const FRHIComputePipelineDesc& Desc) override",
            "转发建计算管线")
D.Interface("void DestroyComputePipeline(FRHIComputePipeline* Pipeline) override", "转发销毁计算管线")
D.Interface("[[nodiscard]] FRHIStructuredBuffer* CreateStructuredBuffer("
            "const FRHIStructuredBufferDesc& Desc) override",
            "转发建结构化缓冲")
D.Interface("void DestroyStructuredBuffer(FRHIStructuredBuffer* Buffer) override", "转发销毁结构化缓冲")
D.Interface("[[nodiscard]] FRHIBufferView* CreateBufferView(const FRHIBufferViewDesc& Desc) override",
            "转发建缓冲视图")
D.Interface("void DestroyBufferView(FRHIBufferView* View) override", "转发销毁缓冲视图")
D.Interface("[[nodiscard]] FRHITextureView* CreateTextureView(const FRHITextureViewDesc& Desc) override",
            "转发建纹理视图")
D.Interface("void DestroyTextureView(FRHITextureView* View) override", "转发销毁纹理视图")
D.Interface("[[nodiscard]] FRHIDescriptorSetLayout* CreateDescriptorSetLayout("
            "const FRHIDescriptorSetLayoutDesc& Desc) override",
            "转发建 set 布局")
D.Interface("void DestroyDescriptorSetLayout(FRHIDescriptorSetLayout* Layout) override", "转发销毁 set 布局")
D.Interface("[[nodiscard]] FRHIPipelineLayout* CreatePipelineLayout("
            "const FRHIPipelineLayoutDesc& Desc) override",
            "转发建管线布局")
D.Interface("void DestroyPipelineLayout(FRHIPipelineLayout* Layout) override", "转发销毁管线布局")
D.Interface("[[nodiscard]] FRHIDescriptorPool* CreateDescriptorPool("
            "const FRHIDescriptorPoolDesc& Desc) override",
            "转发建描述符池")
D.Interface("void DestroyDescriptorPool(FRHIDescriptorPool* Pool) override", "转发销毁描述符池")
D.Interface("[[nodiscard]] FRHIDescriptorSet* AllocateDescriptorSet(FRHIDescriptorPool* Pool, "
            "FRHIDescriptorSetLayout* Layout) override",
            "转发分配 set")
D.Interface("void FreeDescriptorSet(FRHIDescriptorPool* Pool, FRHIDescriptorSet* Set) override",
            "转发归还 set")
D.Interface("void UpdateDescriptorSets(const FRHIDescriptorWrite* Writes, std::uint32_t Count) override",
            "转发设备级 set 写入（立即生效，不需命令缓冲）")
D.Interface("[[nodiscard]] FRHIRenderPass* CreateRenderPass(const FRHIRenderPassDesc& Desc) override",
            "转发建渲染通道")
D.Interface("void DestroyRenderPass(FRHIRenderPass* Pass) override", "转发销毁渲染通道")
D.Interface("[[nodiscard]] FRHIFramebuffer* CreateFramebuffer(const FRHIFramebufferDesc& Desc) override",
            "转发建帧缓冲")
D.Interface("void DestroyFramebuffer(FRHIFramebuffer* Framebuffer) override", "转发销毁帧缓冲")
D.Interface("[[nodiscard]] std::uint32_t GetFramebufferWidth() const override", "转发交换链宽")
D.Interface("[[nodiscard]] std::uint32_t GetFramebufferHeight() const override", "转发交换链高")
D.Interface("[[nodiscard]] FRHIQueryPool* CreateQueryPool(ERHIQueryType Type, "
            "std::uint32_t QueryCount) override",
            "转发建查询池")
D.Interface("void DestroyQueryPool(FRHIQueryPool* Pool) override", "转发销毁查询池")
D.Interface("bool GetQueryPoolResults(FRHIQueryPool* Pool, std::uint32_t FirstQuery, "
            "std::uint32_t QueryCount, std::uint64_t* Results, std::size_t Stride, "
            "bool bWait) override",
            "转发取查询结果（`bWait` 是同步读，别在渲染线程上用它等）")
D.Interface("[[nodiscard]] FRHIRayTracingPipeline* CreateRayTracingPipeline("
            "const FRHIRayTracingPipelineDesc& Desc) override",
            "转发建光追管线")
D.Interface("void DestroyRayTracingPipeline(FRHIRayTracingPipeline* Pipeline) override", "转发销毁光追管线")
D.Interface("[[nodiscard]] FRHIAccelerationStructure* CreateAccelerationStructure("
            "const FRHIRayTracingGeometryDesc& Desc) override",
            "转发建加速结构")
D.Interface("void DestroyAccelerationStructure(FRHIAccelerationStructure* Accel) override",
            "转发销毁加速结构")
D.Interface("bool GetAccelerationStructureBuildSizes(const FRHIRayTracingGeometryDesc& Desc, "
            "std::uint64_t& OutAccelSize, std::uint64_t& OutScratchSize) override",
            "转发查询 build 尺寸")
D.Interface("[[nodiscard]] FRHIBuffer* CreateShaderBindingTable(FRHIRayTracingPipeline* Pipeline, "
            "const FRHISbtGroup* Groups, std::uint32_t GroupCount, "
            "std::uint32_t* OutRayGenOffset, std::uint32_t* OutRayGenStride, "
            "std::uint32_t* OutHitOffset, std::uint32_t* OutHitStride, "
            "std::uint32_t* OutMissOffset, std::uint32_t* OutMissStride) override",
            "转发建 SBT（回填各段偏移 / 跨距）")
D.SetAccess("private")
D.Field("std::unique_ptr<IDynamicRHI> RHI", "设备（后端具体类型；**RHI 私有**，永不外泄）")
D.Field("FThreadPool RecordingPool{1}", "**串行**录制 worker：保持依赖特性的提交顺序"
                                        "（一次绘制必须晚于它之前的清理）。"
                                        "并行录制会把顺序变成竞态，所以这里刻意用 1 个 worker")

# ══════════════════════════════════════════════════════════════════════════════
# Private/RHI.h —— 设备侧抽象（IDynamicRHI）+ 初始化描述 + 后端工厂
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Private/RHI.h", Title="RHI.h —— 设备侧抽象（IDynamicRHI）+ 初始化描述 + 后端工厂",
         Desc="**设备侧**的完整抽象：公开面（`IRHI`）只是它减去后端细节后的一个子集。"
              "分层的原因：`FRHI` 是「服务器」（线程 + 队列串行 + 帧原语），"
              "`IDynamicRHI` 是「设备」（资源工厂 + 队列 + 交换链），"
              "两者职责不同 —— 服务器把前者转发给后者，于是换后端只换 `FRHIFactory::Create` 的产物，"
              "`FRHI` 一行不改。\n"
              "它比 `IRHI` 多出来的东西正是「不该出公开面」的部分：`Initialize` / `Shutdown`、"
              "内存分配器、三个**队列对象**（`FRHIQueue&`）、`ResetFence` —— "
              "这些是后端生命期与内部编排需要的，上层不该看见。\n"
              "**公开面没有 Vulkan / VMA 类型**：`VkDevice`、`VmaAllocator` 之类只出现在"
              "Private 的派生类与映射函数里。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("RHI/RHIAPI.h / RHICommandList.h / RHIEnums.h / RHIResources.h", "公开面类型：本抽象的全部签名都用它们")
D.Row("cstdint / limits / memory", "定宽整数 / 栅栏无限等待的默认超时 / `unique_ptr`")

D.Struct("FRHIInitDesc", Desc="初始化描述：后端选择 + 原生窗口句柄 + 初始帧缓冲尺寸。"
        "**窗口句柄是 `void*`**：Win32 是 HWND，别的平台是各自的原生句柄 —— "
        "设备层不需要认识窗口系统类型，只需要把它原样交给 WSI。")
D.Field("ERHIBackend Backend = ERHIBackend::Vulkan", "后端选择（唯一值 = Vulkan）")
D.Field("void* NativeWindowHandle", "原生窗口句柄（WSI 用；空 = 无头）")
D.Field("int FramebufferWidth = 0", "初始宽（0 / 负值非法）")
D.Field("int FramebufferHeight = 0", "初始高")

D.Class("IDynamicRHI", Desc="渲染硬件接口：设备生命期 + 队列 + 交换链 + 全部资源工厂。"
        "公开面（`IRHI`）是它的子集，多出来的部分（`Initialize` / `Shutdown` / 内存分配器 / "
        "队列对象 / `ResetFence`）只给 `FRHI` 用。\n"
        "**队列永远有三个逻辑端点**（Graphics / Compute / Transfer），"
        "原生队列不独立时由实现内部回落到图形族 —— 调用点看到的是统一的端点集合。")
D.SetAccess("public")
D.Interface("virtual ~IDynamicRHI() = default", "虚析构：`unique_ptr<IDynamicRHI>` 销毁时经虚表回到后端模块")
D.Interface("IDynamicRHI(const IDynamicRHI&) = delete", "禁止拷贝（设备唯一）")
D.Interface("IDynamicRHI& operator=(const IDynamicRHI&) = delete", "禁止拷贝赋值")
D.Interface("virtual bool Initialize(const FRHIInitDesc& Desc) = 0",
            "建设备（实例 / 设备 / 交换链 / 同步对象 / 分配器）；任一步失败返回 false "
            "（并且**不留下半初始化的对象**）")
D.Interface("virtual void Shutdown() = 0", "关设备（幂等）。**调用前必须 `WaitIdle`** —— "
            "否则析构交换链 / 池时仍有在飞工作在引用它们")
D.Interface("virtual void BeginFrame() = 0", "取得交换链镜像 + 开始帧命令缓冲")
D.Interface("virtual void EndFrame() = 0", "结束并提交帧命令缓冲 + 呈现")
D.Interface("virtual void Resize(int Width, int Height) = 0", "尺寸变化：重建交换链与依赖资源")
D.Interface("virtual void WaitIdle() = 0", "设备空闲（所有已提交工作完成）")
D.Interface("[[nodiscard]] virtual FRHICommandList* GetFrameCommandList() = 0",
            "借用帧命令缓冲（非持有；Begin / End 由帧原语负责）")
D.Interface("virtual void PresentTexture(FRHITexture* Src) = 0", "离屏纹理 blit 到当前后缓冲")
D.Interface("[[nodiscard]] virtual ERHIFormat GetSwapchainFormat() const = 0", "交换链格式")
D.Interface("[[nodiscard]] virtual bool IsInitialized() const = 0", "是否已就绪")
D.Interface("[[nodiscard]] virtual IDynamicRHIMemoryAllocator* GetMemoryAllocator() = 0",
            "内存分配器（映射 / 释放）；`FRHI` 用它做 host 可见性判断")
D.Interface("[[nodiscard]] virtual FRHIQueue& GetGraphicsQueue() = 0", "逻辑图形队列端点")
D.Interface("[[nodiscard]] virtual FRHIQueue& GetComputeQueue() = 0", "逻辑计算队列端点")
D.Interface("[[nodiscard]] virtual FRHIQueue& GetTransferQueue() = 0", "逻辑传输队列端点")
D.Interface("[[nodiscard]] virtual FRHICommandList* CreateCommandList(ERHICommandListType Type) = 0",
            "建命令列表（生命周期归调用方）")
D.Interface("virtual void DestroyCommandList(FRHICommandList* CmdList) = 0", "销毁命令列表")
D.Interface("[[nodiscard]] virtual FRHIFence* CreateFence(bool bSignaled) = 0", "建栅栏")
D.Interface("virtual void DestroyFence(FRHIFence* Fence) = 0", "销毁栅栏")
D.Interface("virtual void WaitForFence(FRHIFence* Fence, std::uint64_t TimeoutNs = "
            "(std::numeric_limits<std::uint64_t>::max)()) = 0",
            "等待栅栏（默认无限）")
D.Interface("[[nodiscard]] virtual bool IsFenceSignaled(FRHIFence* Fence) = 0", "非阻塞查询")
D.Interface("virtual void ResetFence(FRHIFence* Fence) = 0",
            "把栅栏复位成未触发（复用同一个栅栏再次提交之前必须做，否则等待立刻返回）")
D.Interface("[[nodiscard]] virtual FRHISemaphore* CreateGpuSemaphore() = 0", "建 GPU 信号量")
D.Interface("virtual void DestroyGpuSemaphore(FRHISemaphore* Semaphore) = 0", "销毁信号量")
D.Interface("[[nodiscard]] virtual FRHIBuffer* CreateBuffer(const FRHIBufferDesc& Desc) = 0", "建缓冲")
D.Interface("virtual void DestroyBuffer(FRHIBuffer* Buffer) = 0", "销毁缓冲")
D.Interface("[[nodiscard]] virtual FRHITexture* CreateTexture(const FRHITextureDesc& Desc) = 0", "建纹理")
D.Interface("virtual void DestroyTexture(FRHITexture* Texture) = 0", "销毁纹理")
D.Interface("[[nodiscard]] virtual FRHISampler* CreateSampler(const FRHISamplerDesc& Desc) = 0", "建采样器")
D.Interface("virtual void DestroySampler(FRHISampler* Sampler) = 0", "销毁采样器")
D.Interface("[[nodiscard]] virtual FRHIShaderModule* CreateShaderModule(const FRHIShaderModuleDesc& Desc) = 0",
            "建着色器模块")
D.Interface("virtual void DestroyShaderModule(FRHIShaderModule* Module) = 0", "销毁着色器模块")
D.Interface("[[nodiscard]] virtual FRHIGraphicsPipeline* CreateGraphicsPipeline("
            "const FRHIGraphicsPipelineDesc& Desc) = 0",
            "建栅格管线")
D.Interface("virtual void DestroyGraphicsPipeline(FRHIGraphicsPipeline* Pipeline) = 0", "销毁栅格管线")
D.Interface("[[nodiscard]] virtual FRHIComputePipeline* CreateComputePipeline("
            "const FRHIComputePipelineDesc& Desc) = 0",
            "建计算管线")
D.Interface("virtual void DestroyComputePipeline(FRHIComputePipeline* Pipeline) = 0", "销毁计算管线")
D.Interface("[[nodiscard]] virtual FRHIStructuredBuffer* CreateStructuredBuffer("
            "const FRHIStructuredBufferDesc& Desc) = 0",
            "建结构化缓冲")
D.Interface("virtual void DestroyStructuredBuffer(FRHIStructuredBuffer* Buffer) = 0", "销毁结构化缓冲")
D.Interface("[[nodiscard]] virtual FRHIBufferView* CreateBufferView(const FRHIBufferViewDesc& Desc) = 0",
            "建缓冲视图")
D.Interface("virtual void DestroyBufferView(FRHIBufferView* View) = 0", "销毁缓冲视图")
D.Interface("[[nodiscard]] virtual FRHITextureView* CreateTextureView(const FRHITextureViewDesc& Desc) = 0",
            "建纹理视图")
D.Interface("virtual void DestroyTextureView(FRHITextureView* View) = 0", "销毁纹理视图")
D.Interface("[[nodiscard]] virtual FRHIDescriptorSetLayout* CreateDescriptorSetLayout("
            "const FRHIDescriptorSetLayoutDesc& Desc) = 0",
            "建 set 布局")
D.Interface("virtual void DestroyDescriptorSetLayout(FRHIDescriptorSetLayout* Layout) = 0", "销毁 set 布局")
D.Interface("[[nodiscard]] virtual FRHIPipelineLayout* CreatePipelineLayout("
            "const FRHIPipelineLayoutDesc& Desc) = 0",
            "建管线布局")
D.Interface("virtual void DestroyPipelineLayout(FRHIPipelineLayout* Layout) = 0", "销毁管线布局")
D.Interface("[[nodiscard]] virtual FRHIDescriptorPool* CreateDescriptorPool("
            "const FRHIDescriptorPoolDesc& Desc) = 0",
            "建描述符池")
D.Interface("virtual void DestroyDescriptorPool(FRHIDescriptorPool* Pool) = 0", "销毁描述符池")
D.Interface("[[nodiscard]] virtual FRHIDescriptorSet* AllocateDescriptorSet(FRHIDescriptorPool* Pool, "
            "FRHIDescriptorSetLayout* Layout) = 0",
            "分配 set")
D.Interface("virtual void FreeDescriptorSet(FRHIDescriptorPool* Pool, FRHIDescriptorSet* Set) = 0", "归还 set")
D.Interface("virtual void UpdateDescriptorSets(const FRHIDescriptorWrite* Writes, "
            "std::uint32_t Count) = 0",
            "设备级写入 set 内容（映射到 `vkUpdateDescriptorSets`，立即生效，**不是**录制的 `vkCmd`）")
D.Interface("[[nodiscard]] virtual FRHIRenderPass* CreateRenderPass(const FRHIRenderPassDesc& Desc) = 0",
            "建渲染通道")
D.Interface("virtual void DestroyRenderPass(FRHIRenderPass* Pass) = 0", "销毁渲染通道")
D.Interface("[[nodiscard]] virtual FRHIFramebuffer* CreateFramebuffer(const FRHIFramebufferDesc& Desc) = 0",
            "建帧缓冲")
D.Interface("virtual void DestroyFramebuffer(FRHIFramebuffer* Framebuffer) = 0", "销毁帧缓冲")
D.Interface("[[nodiscard]] virtual std::uint32_t GetFramebufferWidth() const = 0",
            "交换链宽（交换链本身设备私有，只暴露尺寸）")
D.Interface("[[nodiscard]] virtual std::uint32_t GetFramebufferHeight() const = 0", "交换链高")
D.Interface("[[nodiscard]] virtual FRHIQueryPool* CreateQueryPool(ERHIQueryType Type, "
            "std::uint32_t QueryCount) = 0",
            "建查询池")
D.Interface("virtual void DestroyQueryPool(FRHIQueryPool* Pool) = 0", "销毁查询池")
D.Interface("virtual bool GetQueryPoolResults(FRHIQueryPool* Pool, std::uint32_t FirstQuery, "
            "std::uint32_t QueryCount, std::uint64_t* Results, std::size_t Stride, "
            "bool bWait = true) = 0",
            "取查询结果（`bWait` = 同步读，**不要在 RHI 线程上调用**）")
D.Interface("[[nodiscard]] virtual FRHIRayTracingPipeline* CreateRayTracingPipeline("
            "const FRHIRayTracingPipelineDesc& Desc) = 0",
            "建光追管线")
D.Interface("virtual void DestroyRayTracingPipeline(FRHIRayTracingPipeline* Pipeline) = 0", "销毁光追管线")
D.Interface("[[nodiscard]] virtual FRHIAccelerationStructure* CreateAccelerationStructure("
            "const FRHIRayTracingGeometryDesc& Desc) = 0",
            "建加速结构（不支持时返回 nullptr）")
D.Interface("virtual void DestroyAccelerationStructure(FRHIAccelerationStructure* Accel) = 0",
            "销毁加速结构")
D.Interface("virtual bool GetAccelerationStructureBuildSizes(const FRHIRayTracingGeometryDesc& Desc, "
            "std::uint64_t& OutAccelSize, std::uint64_t& OutScratchSize) = 0",
            "查询 build 尺寸（本体 + scratch）")
D.Interface("[[nodiscard]] virtual FRHIBuffer* CreateShaderBindingTable(FRHIRayTracingPipeline* Pipeline, "
            "const FRHISbtGroup* Groups, std::uint32_t GroupCount, "
            "std::uint32_t* OutRayGenOffset = nullptr, std::uint32_t* OutRayGenStride = nullptr, "
            "std::uint32_t* OutHitOffset = nullptr, std::uint32_t* OutHitStride = nullptr, "
            "std::uint32_t* OutMissOffset = nullptr, std::uint32_t* OutMissStride = nullptr) = 0",
            "建 SBT（返回 DeviceAddress + Storage 缓冲，回填各段偏移 / 跨距）")
D.SetAccess("protected")
D.Interface("IDynamicRHI() = default", "protected 构造 ⇒ 只能由 `FRHIFactory::Create` 造")

D.Class("FRHIFactory", Desc="后端工厂：**整个引擎唯一的后端选择点**。"
        "加后端 = 在这里多一个分支 + 多一个派生类，其余代码（`FRHI` / RDG / 渲染插件）零改动。")
D.SetAccess("public")
D.Interface("FRHIFactory() = delete",
            "静态工厂：不可实例化（构造删除 ⇒ 调用点不可能误建对象）")
D.Interface("[[nodiscard]] static IDynamicRHI* Create(ERHIBackend Backend)",
            "按 `Backend` 造设备（返回裸指针：调用方用 `unique_ptr` 接管）")

# ══════════════════════════════════════════════════════════════════════════════
# Private/VulkanMemory.h —— VMA 分配器 + 延迟回收队列
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Private/VulkanMemory.h", Title="VulkanMemory.h —— VMA 分配器 + 延迟回收队列",
         Desc="显存分配被包在 `FVulkanMemoryAllocator` 后面（VMA 的 `VMA_IMPLEMENTATION` "
              "只在这一个 .cpp 里编译一次：宏放在头里会让每个 TU 都实现一遍 VMA，链接期爆炸）。\n"
              "这里最重要的一条规则是**异步上传的生命期**：staging 缓冲被录制的 `vkCmdCopyBuffer` "
              "读写，录制结束不等于 GPU 用完 —— 所以它不能随录制作用域结束就 `vmaDestroyBuffer`。"
              "`DestroyBufferDeferred` 把它挂进队列，`FlushDeferredFrees` 在**下一帧边界**"
              "（宿主已经等过上一帧的栅栏之后）才真正释放，于是「不再有在飞的拷贝读到已释放的显存」。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("RHI/RHIResources.h", "`IDynamicRHIMemoryAllocator`（本类的基类）与 `ERHIMemoryUsage`")
D.Row("mutex", "`DeferredMutex`：录制线程会并发调用延迟回收")
D.Row("vector", "延迟释放列表")
D.Row("vulkan/vulkan.h", "`VkBuffer` / `VkImage` / `VmaAllocation`（本头是 Private，允许出现）")
D.Row("vk_mem_alloc.h", "VMA：`VMA_STATIC_VULKAN_FUNCTIONS 0` + `VMA_DYNAMIC_VULKAN_FUNCTIONS 1` —— "
                        "函数指针运行期取，避免静态链接到 loader")

D.Class("FVulkanMemoryAllocator", Base="IDynamicRHIMemoryAllocator",
        Desc="VMA 的薄封装 + 延迟回收队列，设备唯一（由 `FVulkanRHI` 持有）。"
             "除标准分配 / 映射外，它承担一条**帧边界职责**：把「本帧录制用过的 staging」"
             "推迟一帧释放。")
D.SetAccess("public")
D.Interface("FVulkanMemoryAllocator() = default", "空构造（分配器在 `Initialize` 里才创建）")
D.Interface("~FVulkanMemoryAllocator() override", "析构：释放 VMA（若有未 flush 的延迟列表也一并释放）")
D.Interface("bool Initialize(VkInstance Instance, VkPhysicalDevice PhysicalDevice, VkDevice Device)",
            "创建 VMA 分配器（三件句柄都必需，VMA 用它取函数指针）")
D.Interface("void Shutdown()", "销毁 VMA（幂等）；此后 `IsValid()` 为假")
D.Interface("[[nodiscard]] VmaAllocator GetAllocator() const",
            "底层 VMA 句柄（仅本模块内用 —— 公开面永远看不到它）")
D.Interface("[[nodiscard]] bool IsValid() const", "分配器是否可用")
D.Interface("[[nodiscard]] bool CreateBuffer(const VkBufferCreateInfo& BufferInfo, "
            "const VmaAllocationCreateInfo& AllocInfo, VkBuffer& OutBuffer, "
            "VmaAllocation& OutAllocation, FRHIMemoryAllocation* OutOpaque = nullptr)",
            "建缓冲 + 分配显存（`OutOpaque` 可选：把不透明句柄回交给公开面）")
D.Interface("[[nodiscard]] bool CreateImage(const VkImageCreateInfo& ImageInfo, "
            "const VmaAllocationCreateInfo& AllocInfo, VkImage& OutImage, "
            "VmaAllocation& OutAllocation, FRHIMemoryAllocation* OutOpaque = nullptr)",
            "建图像 + 分配显存")
D.Interface("void DestroyBuffer(VkBuffer Buffer, VmaAllocation Allocation)",
            "立即释放缓冲。**只用于确定没有在飞引用的资源**（否则用延迟版）")
D.Interface("void DestroyImage(VkImage Image, VmaAllocation Allocation)", "立即释放图像")
D.Interface("void DestroyBufferDeferred(VkBuffer Buffer, VmaAllocation Allocation)",
            "把 staging 挂进延迟队列，等 `FlushDeferredFrees` 再释放 —— "
            "让「已录制但未执行完」的拷贝不会读到已释放的显存（**异步上传的生命期规则**）。"
            "内部加锁：录制线程可能并发调用")
D.Interface("void FlushDeferredFrees()",
            "释放上一次 flush 之后挂进来的全部缓冲。**必须在「上一帧栅栏已触发」之后的帧边界调用**——"
            "调用早了就等于提前释放，调用晚了只是多占一帧显存")
D.Interface("[[nodiscard]] static VmaAllocationCreateInfo MakeAllocationInfo(ERHIMemoryUsage MemoryUsage)",
            "把 `ERHIMemoryUsage`（GPUOnly / CPUToGPU / GPUToCPU / CPUOnly）翻成 VMA 的分配参数 —— "
            "**放置策略的唯一定义点**")
D.Interface("virtual void Free(FRHIMemoryAllocation& Alloc) override",
            "经基类释放一次分配（`Native` 是 VmaAllocation）")
D.Interface("virtual void* Map(FRHIMemoryAllocation& Alloc) override",
            "映射 host 可见内存。**GPUOnly 返回 nullptr**（设备本地内存不可映射），"
            "让 `UpdateBuffer` 退回 staging 路径而不是违规映射")
D.Interface("virtual void Unmap(FRHIMemoryAllocation& Alloc) override", "解除映射")
D.SetAccess("private")
D.Nested("FDeferredBuffer", Kind="struct", Desc="队列里的一条：缓冲 + 其分配。")
D.Interface("struct FDeferredBuffer { VkBuffer Buffer; VmaAllocation Allocation; }",
            "朴素的二元组（延迟释放只需要能销毁，不需要别的状态）")
D.Field("VmaAllocator Allocator = nullptr", "底层 VMA 分配器")
D.Field("std::mutex DeferredMutex", "保护延迟列表（录制线程会调用 `DestroyBufferDeferred`）")
D.Field("std::vector<FDeferredBuffer> DeferredBuffers", "本帧挂入的待释放 staging，下一帧边界统一释放")

# ══════════════════════════════════════════════════════════════════════════════
# Private/VulkanResources.h —— Vulkan 资源派生类（公开基类的后端实现）
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Private/VulkanResources.h", Title="VulkanResources.h —— Vulkan 资源派生类",
         Desc="公开资源族的后端实现：每个 `FRHIxxx` 基类对应一个 `FVulkanxxx`，"
              "内部只多两件事 —— **原生句柄**（`VkBuffer` / `VkImage` / `VkPipeline`…）与"
              "**分配 / 设备引用**（释放时要拿它们去调相应的 `vkDestroy*` / VMA 释放）。\n"
              "全部 `final`：这些类只在 RHI 模块内构造，外面拿到的是基类指针，`final` 让编译器"
              "把「调用其实指不到别处」这件事利用起来（也防止别的模块偷偷派生导致 vtable 归属不明）。\n"
              "析构一律 out-of-line（`~FVulkanBuffer() override;` 定义在 .cpp）—— 它们含 Vulkan 类型，"
              "销毁逻辑只能在本模块内跑。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("RHI/RHIResources.h", "公开资源基类（本文件全是它们的派生类）")
D.Row("VulkanMemory.h", "`FVulkanMemoryAllocator`：释放时用 `VmaAllocation` 归还显存")
D.Row("vulkan/vulkan.h", "原生类型：`VkBuffer` / `VkImage` / `VkPipeline` / `VkDescriptorSet`…")

D.Class("FVulkanBuffer", Base="FRHIBuffer", Desc="缓冲实现：原生 `VkBuffer` + 分配 + 所属分配器。"
        "设备地址在创建时算好并缓存（每次问都要调 `vkGetBufferDeviceAddress`，缓存后是纯读）。")
D.SetAccess("public")
D.Interface("FVulkanBuffer(FRHIBufferDesc InDesc, VkBuffer InBuffer, VmaAllocation InAllocation, "
            "FVulkanMemoryAllocator* InAllocator, std::uint64_t InDeviceAddress = 0)",
            "构造：把设备已建好的句柄与分配**接管**进来（本类不负责创建，只负责最终释放）")
D.Interface("~FVulkanBuffer() override", "销毁 `VkBuffer` + 归还分配（out-of-line）")
D.Interface("[[nodiscard]] const FRHIBufferDesc& GetDesc() const override", "描述（引用返回）")
D.Interface("[[nodiscard]] std::uint64_t GetDeviceAddress() const override", "缓存的设备地址")
D.Interface("[[nodiscard]] VkBuffer GetVkBuffer() const", "原生缓冲句柄（模块内用）")
D.Interface("[[nodiscard]] VmaAllocation GetAllocation() const", "VMA 分配句柄")
D.SetAccess("private")
D.Field("FRHIBufferDesc Desc", "创建时的描述")
D.Field("VkBuffer Buffer = VK_NULL_HANDLE", "原生缓冲")
D.Field("VmaAllocation Allocation = nullptr", "显存分配")
D.Field("FVulkanMemoryAllocator* Allocator = nullptr", "所属分配器（释放时回调）")
D.Field("std::uint64_t DeviceAddress = 0", "设备地址缓存（0 = 未申请 / 不可寻址）")

D.Class("FVulkanStructuredBuffer", Base="FRHIStructuredBuffer",
        Desc="结构化缓冲：**不拥有显存**，只转发到底层 `FVulkanBuffer`（同一段显存两种读法）。")
D.SetAccess("public")
D.Interface("FVulkanStructuredBuffer(FRHIStructuredBufferDesc InDesc, FVulkanBuffer* InUnderlying)",
            "构造：描述 + 底层缓冲")
D.Interface("~FVulkanStructuredBuffer() override", "**不释放底层缓冲**（非持有），只清自己")
D.Interface("[[nodiscard]] const FRHIStructuredBufferDesc& GetDesc() const override", "描述")
D.Interface("[[nodiscard]] FRHIBuffer* GetUnderlyingBuffer() override", "底层缓冲（基类指针）")
D.Interface("[[nodiscard]] VkBuffer GetVkBuffer() const", "底层原生句柄（空则返回 `VK_NULL_HANDLE`）")
D.SetAccess("private")
D.Field("FRHIStructuredBufferDesc Desc", "创建时的描述")
D.Field("FVulkanBuffer* UnderlyingBuffer = nullptr", "底层缓冲（非持有）")

D.Class("FVulkanBufferView", Base="FRHIBufferView", Desc="缓冲视图实现（一个 `VkBufferView`）。")
D.SetAccess("public")
D.Interface("FVulkanBufferView(VkDevice InDevice, VkBufferView InView)", "构造：设备 + 视图句柄")
D.Interface("~FVulkanBufferView() override", "销毁 `VkBufferView`（out-of-line）")
D.Interface("[[nodiscard]] VkBufferView GetVkBufferView() const", "原生视图句柄")
D.SetAccess("private")
D.Field("VkDevice Device = VK_NULL_HANDLE", "设备（销毁视图用）")
D.Field("VkBufferView View = VK_NULL_HANDLE", "原生缓冲视图")

D.Class("FVulkanTexture", Base="FRHITexture", Desc="纹理实现（原生 `VkImage` + 分配）。")
D.SetAccess("public")
D.Interface("FVulkanTexture(FRHITextureDesc InDesc, VkImage InImage, VmaAllocation InAllocation, "
            "FVulkanMemoryAllocator* InAllocator)",
            "构造：接管设备建好的图像与分配")
D.Interface("~FVulkanTexture() override", "销毁 `VkImage` + 归还分配")
D.Interface("[[nodiscard]] const FRHITextureDesc& GetDesc() const override", "描述")
D.Interface("[[nodiscard]] VkImage GetVkImage() const", "原生图像句柄")
D.SetAccess("private")
D.Field("FRHITextureDesc Desc", "创建时的描述")
D.Field("VkImage Image = VK_NULL_HANDLE", "原生图像")
D.Field("VmaAllocation Allocation = nullptr", "显存分配")
D.Field("FVulkanMemoryAllocator* Allocator = nullptr", "所属分配器")

D.Class("FVulkanSampler", Base="FRHISampler", Desc="采样器实现（原生 `VkSampler`）。")
D.SetAccess("public")
D.Interface("explicit FVulkanSampler(FRHISamplerDesc InDesc, VkDevice InDevice, VkSampler InSampler)",
            "构造：描述 + 设备 + 采样器句柄（`explicit`：避免描述被隐式转换进来）")
D.Interface("~FVulkanSampler() override", "销毁 `VkSampler`")
D.Interface("[[nodiscard]] const FRHISamplerDesc& GetDesc() const override", "描述")
D.Interface("[[nodiscard]] VkSampler GetVkSampler() const", "原生采样器句柄")
D.SetAccess("private")
D.Field("FRHISamplerDesc Desc", "创建时的描述")
D.Field("VkDevice Device = VK_NULL_HANDLE", "设备")
D.Field("VkSampler Sampler = VK_NULL_HANDLE", "原生采样器")

D.Class("FVulkanShaderModule", Base="FRHIShaderModule", Desc="着色器模块实现（原生 `VkShaderModule`）。")
D.SetAccess("public")
D.Interface("FVulkanShaderModule(VkDevice InDevice, VkShaderModule InModule)", "构造：设备 + 模块句柄")
D.Interface("~FVulkanShaderModule() override", "销毁 `VkShaderModule`")
D.Interface("[[nodiscard]] VkShaderModule GetVkShaderModule() const", "原生模块句柄")
D.SetAccess("private")
D.Field("VkDevice Device = VK_NULL_HANDLE", "设备")
D.Field("VkShaderModule Module = VK_NULL_HANDLE", "原生着色器模块")

D.Class("FVulkanGraphicsPipeline", Base="FRHIGraphicsPipeline",
        Desc="栅格管线实现（原生 `VkPipeline` + 其 `VkPipelineLayout`）。")
D.SetAccess("public")
D.Interface("FVulkanGraphicsPipeline(VkDevice InDevice, VkPipeline InPipeline, VkPipelineLayout InLayout)",
            "构造：设备 + 管线 + 布局")
D.Interface("~FVulkanGraphicsPipeline() override", "销毁 `VkPipeline`")
D.Interface("[[nodiscard]] VkPipeline GetVkPipeline() const", "原生管线句柄")
D.Interface("[[nodiscard]] VkPipelineLayout GetVkPipelineLayout() const", "原生管线布局句柄")
D.SetAccess("private")
D.Field("VkDevice Device = VK_NULL_HANDLE", "设备")
D.Field("VkPipeline Pipeline = VK_NULL_HANDLE", "原生管线")
D.Field("VkPipelineLayout Layout = VK_NULL_HANDLE", "原生管线布局")

D.Class("FVulkanComputePipeline", Base="FRHIComputePipeline", Desc="计算管线实现。")
D.SetAccess("public")
D.Interface("FVulkanComputePipeline(VkDevice InDevice, VkPipeline InPipeline, VkPipelineLayout InLayout)",
            "构造：设备 + 管线 + 布局")
D.Interface("~FVulkanComputePipeline() override", "销毁 `VkPipeline`")
D.Interface("[[nodiscard]] VkPipeline GetVkPipeline() const", "原生管线句柄")
D.Interface("[[nodiscard]] VkPipelineLayout GetVkPipelineLayout() const", "原生管线布局句柄")
D.SetAccess("private")
D.Field("VkDevice Device = VK_NULL_HANDLE", "设备")
D.Field("VkPipeline Pipeline = VK_NULL_HANDLE", "原生管线")
D.Field("VkPipelineLayout Layout = VK_NULL_HANDLE", "原生管线布局")

D.Class("FVulkanFence", Base="FRHIFence", Desc="栅栏实现（原生 `VkFence`）。")
D.SetAccess("public")
D.Interface("FVulkanFence(VkDevice InDevice, VkFence InFence)", "构造：设备 + 栅栏句柄")
D.Interface("~FVulkanFence() override", "销毁 `VkFence`")
D.Interface("[[nodiscard]] VkFence GetVkFence() const", "原生栅栏句柄")
D.SetAccess("private")
D.Field("VkDevice Device = VK_NULL_HANDLE", "设备")
D.Field("VkFence Fence = VK_NULL_HANDLE", "原生栅栏")

D.Class("FVulkanSemaphore", Base="FRHISemaphore", Desc="信号量实现（原生 `VkSemaphore`）。")
D.SetAccess("public")
D.Interface("FVulkanSemaphore(VkDevice InDevice, VkSemaphore InSemaphore)", "构造：设备 + 信号量句柄")
D.Interface("~FVulkanSemaphore() override", "销毁 `VkSemaphore`")
D.Interface("[[nodiscard]] VkSemaphore GetVkSemaphore() const", "原生信号量句柄")
D.SetAccess("private")
D.Field("VkDevice Device = VK_NULL_HANDLE", "设备")
D.Field("VkSemaphore Semaphore = VK_NULL_HANDLE", "原生信号量")

D.Class("FVulkanQueryPool", Base="FRHIQueryPool", Desc="查询池实现（原生 `VkQueryPool`）。")
D.SetAccess("public")
D.Interface("FVulkanQueryPool(VkDevice InDevice, VkQueryPool InPool)", "构造：设备 + 查询池句柄")
D.Interface("~FVulkanQueryPool() override", "销毁 `VkQueryPool`")
D.Interface("[[nodiscard]] VkQueryPool GetVkQueryPool() const", "原生查询池句柄")
D.SetAccess("private")
D.Field("VkDevice Device = VK_NULL_HANDLE", "设备")
D.Field("VkQueryPool Pool = VK_NULL_HANDLE", "原生查询池")

D.Class("FVulkanAccelerationStructure", Base="FRHIAccelerationStructure",
        Desc="加速结构实现：**本体走自己的存储缓冲**（`VkAccelerationStructureKHR` 只是缓冲上的"
             "一段句柄），所以同时持有 `StorageBuffer`（+ 分配）与 AS 句柄，"
             "设备地址也缓存下来供建 TLAS 实例用。构造时把几何描述 move 进受保护的基类字段 —— "
             "build 需要它（顶点 / 索引缓冲与计数）。")
D.SetAccess("public")
D.Interface("FVulkanAccelerationStructure(VkDevice InDevice, VkAccelerationStructureKHR InAccel, "
            "VkBuffer InStorageBuffer, VmaAllocation InAllocation, "
            "FVulkanMemoryAllocator* InAllocator, std::uint64_t InDeviceAddress, "
            "FRHIRayTracingGeometryDesc InGeometryDesc)",
            "构造：设备 / AS 句柄 / 存储缓冲 + 分配 / 分配器 / 设备地址 / 几何描述（move 进基类）")
D.Interface("~FVulkanAccelerationStructure() override", "销毁 AS + 存储缓冲并归还分配")
D.Interface("[[nodiscard]] VkAccelerationStructureKHR GetVkAccelerationStructure() const", "原生 AS 句柄")
D.Interface("[[nodiscard]] VkBuffer GetVkStorageBuffer() const", "AS 的存储缓冲")
D.Interface("[[nodiscard]] std::uint64_t GetDeviceAddress() const", "AS 的设备地址（建 TLAS 实例用）")
D.SetAccess("private")
D.Field("VkDevice Device = VK_NULL_HANDLE", "设备")
D.Field("VkAccelerationStructureKHR Accel = VK_NULL_HANDLE", "原生加速结构")
D.Field("VkBuffer StorageBuffer = VK_NULL_HANDLE", "承载 AS 的缓冲")
D.Field("VmaAllocation Allocation = nullptr", "存储缓冲的分配")
D.Field("FVulkanMemoryAllocator* Allocator = nullptr", "所属分配器")
D.Field("std::uint64_t DeviceAddress = 0", "缓存的设备地址")

D.Class("FVulkanRayTracingPipeline", Base="FRHIRayTracingPipeline", Desc="光追管线实现。")
D.SetAccess("public")
D.Interface("FVulkanRayTracingPipeline(VkDevice InDevice, VkPipeline InPipeline, "
            "VkPipelineLayout InLayout)",
            "构造：设备 + 管线 + 布局")
D.Interface("~FVulkanRayTracingPipeline() override", "销毁 `VkPipeline`")
D.Interface("[[nodiscard]] VkPipeline GetVkPipeline() const", "原生管线句柄")
D.Interface("[[nodiscard]] VkPipelineLayout GetVkPipelineLayout() const", "原生管线布局句柄")
D.SetAccess("private")
D.Field("VkDevice Device = VK_NULL_HANDLE", "设备")
D.Field("VkPipeline Pipeline = VK_NULL_HANDLE", "原生管线")
D.Field("VkPipelineLayout Layout = VK_NULL_HANDLE", "原生管线布局")

D.Class("FVulkanTextureView", Base="FRHITextureView", Desc="纹理视图实现（原生 `VkImageView`）。")
D.SetAccess("public")
D.Interface("FVulkanTextureView(VkDevice InDevice, VkImageView InView)", "构造：设备 + 视图句柄")
D.Interface("~FVulkanTextureView() override", "销毁 `VkImageView`")
D.Interface("[[nodiscard]] VkImageView GetVkImageView() const", "原生图像视图句柄")
D.SetAccess("private")
D.Field("VkDevice Device = VK_NULL_HANDLE", "设备")
D.Field("VkImageView View = VK_NULL_HANDLE", "原生图像视图")

D.Class("FVulkanDescriptorSetLayout", Base="FRHIDescriptorSetLayout", Desc="set 布局实现。")
D.SetAccess("public")
D.Interface("FVulkanDescriptorSetLayout(VkDevice InDevice, VkDescriptorSetLayout InLayout)",
            "构造：设备 + 布局句柄")
D.Interface("~FVulkanDescriptorSetLayout() override", "销毁 `VkDescriptorSetLayout`")
D.Interface("[[nodiscard]] VkDescriptorSetLayout GetVkLayout() const", "原生布局句柄")
D.SetAccess("private")
D.Field("VkDevice Device = VK_NULL_HANDLE", "设备")
D.Field("VkDescriptorSetLayout Layout = VK_NULL_HANDLE", "原生 set 布局")

D.Class("FVulkanPipelineLayout", Base="FRHIPipelineLayout", Desc="管线布局实现。")
D.SetAccess("public")
D.Interface("FVulkanPipelineLayout(VkDevice InDevice, VkPipelineLayout InLayout)",
            "构造：设备 + 布局句柄")
D.Interface("~FVulkanPipelineLayout() override", "销毁 `VkPipelineLayout`")
D.Interface("[[nodiscard]] VkPipelineLayout GetVkLayout() const", "原生管线布局句柄")
D.SetAccess("private")
D.Field("VkDevice Device = VK_NULL_HANDLE", "设备")
D.Field("VkPipelineLayout Layout = VK_NULL_HANDLE", "原生管线布局")

D.Class("FVulkanDescriptorPool", Base="FRHIDescriptorPool", Desc="描述符池实现。"
        "**池也是释放单位**：归还单个 set 可以，但整池销毁会一次回收其全部 set。")
D.SetAccess("public")
D.Interface("FVulkanDescriptorPool(VkDevice InDevice, VkDescriptorPool InPool)", "构造：设备 + 池句柄")
D.Interface("~FVulkanDescriptorPool() override", "销毁 `VkDescriptorPool`")
D.Interface("[[nodiscard]] VkDescriptorPool GetVkPool() const", "原生池句柄")
D.SetAccess("private")
D.Field("VkDevice Device = VK_NULL_HANDLE", "设备")
D.Field("VkDescriptorPool Pool = VK_NULL_HANDLE", "原生描述符池")

D.Class("FVulkanDescriptorSet", Base="FRHIDescriptorSet",
        Desc="描述符集实现：**不需要设备指针**（归还 set 走 `vkFreeDescriptorSets`，"
             "池已带上下文），所以它只包一个原生句柄。")
D.SetAccess("public")
D.Interface("FVulkanDescriptorSet(VkDescriptorSet InSet)", "构造：set 句柄（隐式转换友好：单参数）")
D.Interface("[[nodiscard]] VkDescriptorSet GetVkSet() const", "原生 set 句柄")
D.SetAccess("private")
D.Field("VkDescriptorSet Set = VK_NULL_HANDLE", "原生描述符集")

D.Class("FVulkanRenderPass", Base="FRHIRenderPass",
        Desc="渲染通道实现。**所有权可关**（`bOwnsHandle`）：交换链的渲染通道由交换链自己释放，"
             "RHI 只借用它来 `BeginRenderPass` —— 若这里也释放一次就是双重释放，"
             "所以非拥有实例析构时不做任何事。")
D.SetAccess("public")
D.Interface("FVulkanRenderPass(VkDevice InDevice, VkRenderPass InPass, bool bInOwnsHandle = true)",
            "构造：设备 + 通道 + 是否拥有句柄（交换链传 false）")
D.Interface("~FVulkanRenderPass() override", "拥有时销毁 `VkRenderPass`；非拥有时不动它")
D.Interface("[[nodiscard]] VkRenderPass GetVkPass() const", "原生渲染通道句柄")
D.SetAccess("private")
D.Field("VkDevice Device = VK_NULL_HANDLE", "设备")
D.Field("VkRenderPass Pass = VK_NULL_HANDLE", "原生渲染通道")
D.Field("bool bOwnsHandle = true", "是否拥有句柄（false = 交换链拥有，本对象只借用）")

D.Class("FVulkanFramebuffer", Base="FRHIFramebuffer",
        Desc="帧缓冲实现。与渲染通道同样的所有权开关：交换链帧缓冲由交换链释放，"
             "本类只是非持有的 RHI 视图。")
D.SetAccess("public")
D.Interface("FVulkanFramebuffer(VkDevice InDevice, VkFramebuffer InFB, bool bInOwnsHandle = true)",
            "构造：设备 + 帧缓冲 + 是否拥有句柄")
D.Interface("~FVulkanFramebuffer() override", "拥有时销毁 `VkFramebuffer`；非拥有时不动它")
D.Interface("[[nodiscard]] VkFramebuffer GetVkFramebuffer() const", "原生帧缓冲句柄")
D.SetAccess("private")
D.Field("VkDevice Device = VK_NULL_HANDLE", "设备")
D.Field("VkFramebuffer FB = VK_NULL_HANDLE", "原生帧缓冲")
D.Field("bool bOwnsHandle = true", "是否拥有句柄（false = 交换链拥有）")

# ══════════════════════════════════════════════════════════════════════════════
# Private/VulkanCommandList.h —— Vulkan 队列 + 命令列表实现
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Private/VulkanCommandList.h", Title="VulkanCommandList.h —— 队列 + 命令列表实现",
         Desc="公开录制面的后端实现：`FVulkanQueue`（一个原生队列 + 它的族索引 / 是否是回落队列）"
              "与 `FVulkanCommandList`（一个 `VkCommandBuffer` 的完整包装）。\n"
              "命令列表这里承担两条纪律：\n"
              "**类型纪律** —— `AssertType` / `AssertNotTransfer` 在 Debug 把「在传输列表里录绘制」"
              "这类错误**当场**打出来，而不是让驱动返回 `VK_ERROR` 或静默丢弃。\n"
              "**已绑定状态缓存** —— `PushConstants` / `BindDescriptorSets` 需要管线布局，"
              "所以最近一次绑定的图形 / 计算管线被记住；这就是「录制的顺序即语义」的直接体现。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("RHI/RHICommandList.h", "公开录制面（本文件实现它）")
D.Row("cassert", "`assert`：类型纪律在 Debug 生效、Release 消失（零开销）")
D.Row("vulkan/vulkan.h", "原生类型：`VkCommandBuffer` / `VkQueue` / `PFN_vkCmd*`")

D.Class("FVulkanQueue", Base="FRHIQueue",
        Desc="逻辑队列端点的实现：一个原生 `VkQueue` + 它的族索引 + 「是否回落到别的族」。"
             "`Configure` 由设备在选队列族时调用一次，之后只读 —— 因此这里没有锁："
             "端点在初始化后就是不可变数据。")
D.SetAccess("public")
D.Interface("FVulkanQueue() = default", "空构造（真正的绑定在 `Configure` 里）")
D.Interface("void Configure(ERHIQueueType InLogicalType, VkQueue InNativeQueue, "
            "std::uint32_t InFamilyIndex, bool bInNativeFallback)",
            "绑定逻辑类型 / 原生队列 / 族索引 / 是否回落（设备初始化期一次调用）")
D.Interface("[[nodiscard]] ERHIQueueType GetType() const override", "本端点的逻辑类型")
D.Interface("[[nodiscard]] bool IsNativeFallback() const override",
            "是否复用了非独立原生队列（`FRHI::Submit` 据此决定往哪个原生队列提交）")
D.Interface("[[nodiscard]] VkQueue GetVkQueue() const", "原生队列句柄")
D.Interface("[[nodiscard]] std::uint32_t GetFamilyIndex() const", "队列族索引")
D.Interface("virtual void Submit(FRHICommandList* const* CmdLists, std::uint32_t Count, "
            "FRHISemaphore* const* WaitSemaphores, std::uint32_t WaitCount, "
            "FRHISemaphore* const* SignalSemaphores, std::uint32_t SignalCount, "
            "FRHIFence* SignalFence) override",
            "把一组命令列表提交到原生队列（`vkQueueSubmit`）：等待 / 发出信号量 + 可选栅栏。"
            "**串行责任在调用方**")
D.SetAccess("private")
D.Field("ERHIQueueType LogicalType = ERHIQueueType::Graphics", "逻辑类型")
D.Field("VkQueue NativeQueue = VK_NULL_HANDLE", "原生队列句柄")
D.Field("std::uint32_t FamilyIndex = 0", "队列族索引")
D.Field("bool bNativeFallback = false", "是否回落到非独立原生队列")

D.Class("FVulkanCommandList", Base="FRHICommandList",
        Desc="命令列表实现：一个 `VkCommandBuffer` + 它的池 + 设备 + 分配器（上传 staging 时要用）"
             "+ 光追函数指针。**能力边界在运行期由 `Type` 判定**，不是靠分成三个子类 —— "
             "改类型只是一个枚举，分成子类会让「建列表」的调用点被迫知道更多。")
D.SetAccess("public")
D.Nested("FRTRuntime", Kind="struct",
         Desc="光追设备函数指针（KHR 扩展要动态取）：`BuildAccel` / `CopyAccel` / `TraceRays`。"
              "集中成一小份传给每个列表，于是录制的时候不需要再回设备查询。")
D.Interface("FVulkanCommandList(ERHICommandListType InType, VkDevice InDevice, VkCommandPool InPool, "
            "VkCommandBuffer InBuffer, FVulkanMemoryAllocator* InAllocator, const FRTRuntime& InRT)",
            "构造：类型 / 设备 / 池 / 命令缓冲 / 分配器（上传用）/ 光追函数")
D.Interface("~FVulkanCommandList() override",
            "析构：销毁 `VkCommandBuffer` 并把它归还原生池（out-of-line）")
D.Interface("[[nodiscard]] ERHICommandListType GetType() const override", "本列表类型")
D.Interface("[[nodiscard]] VkCommandBuffer GetVkCommandBuffer() const", "原生命令缓冲句柄")
D.Interface("[[nodiscard]] VkCommandPool GetVkCommandPool() const", "原生命令池句柄")
D.Interface("virtual void Begin() override", "`vkBeginCommandBuffer`（一次录制会话开始）")
D.Interface("virtual void End() override", "`vkEndCommandBuffer`")
D.Interface("virtual void CopyBuffer(FRHIBuffer* Src, std::uint64_t SrcOffset, FRHIBuffer* Dst, "
            "std::uint64_t DstOffset, std::uint64_t Size) override",
            "缓冲拷贝（传输命令）")
D.Interface("virtual void UpdateBuffer(FRHIBuffer* Buffer, std::uint64_t Offset, std::uint64_t Size, "
            "const void* Data) override",
            "写缓冲。**host 可见直写、设备本地走 staging**：staging 的释放走延迟队列"
            "（录制的拷贝还没执行，不能立刻释放）")
D.Interface("virtual void CopyBufferToTexture(FRHIBuffer* Src, FRHITexture* Dst, "
            "std::uint64_t SrcOffset) override",
            "缓冲 → 纹理（含必要的布局转换）")
D.Interface("virtual void CopyTextureToBuffer(FRHITexture* Src, FRHIBuffer* Dst, "
            "std::uint64_t DstOffset) override",
            "纹理 → 缓冲")
D.Interface("virtual void FillBuffer(FRHIBuffer* Buffer, std::uint64_t Offset, std::uint64_t Size, "
            "std::uint32_t Data) override",
            "`vkCmdFillBuffer`")
D.Interface("virtual void TransitionBuffer(FRHIBuffer* Buffer, ERHIResourceState OldState, "
            "ERHIResourceState NewState) override",
            "缓冲屏障（粗粒度状态 → stage / access 掩码由这里的映射表给出）")
D.Interface("virtual void TransitionTexture(FRHITexture* Texture, ERHIResourceState OldState, "
            "ERHIResourceState NewState) override",
            "纹理屏障（含布局转换）")
D.Interface("virtual void BeginRenderPass(FRHIRenderPass* RenderPass, FRHIFramebuffer* Framebuffer, "
            "std::uint32_t Width, std::uint32_t Height, const float ClearColor[4], "
            "bool bHasDepthStencil, float DepthClear, std::uint32_t StencilClear) override",
            "传统渲染通道开始（清理值在这里变成 `VkClearValue`）")
D.Interface("virtual void EndRenderPass() override", "`vkCmdEndRenderPass`")
D.Interface("virtual void BeginRendering(const FRHIRenderingAttachmentInfo* ColorAttachments, "
            "std::uint32_t ColorCount, const FRHIRenderingAttachmentInfo* DepthAttachment, "
            "std::uint32_t Width, std::uint32_t Height) override",
            "动态渲染开始（附件信息按 `VkRenderingAttachmentInfo` 组装）")
D.Interface("virtual void EndRendering() override", "`vkCmdEndRendering`")
D.Interface("virtual void SetViewport(float X, float Y, float Width, float Height, float MinDepth, "
            "float MaxDepth) override",
            "`vkCmdSetViewport`")
D.Interface("virtual void SetScissor(std::int32_t X, std::int32_t Y, std::uint32_t Width, "
            "std::uint32_t Height) override",
            "`vkCmdSetScissor`")
D.Interface("virtual void BindGraphicsPipeline(FRHIGraphicsPipeline* Pipeline) override",
            "绑定管线；**同时记住它**（后面 push constant 要用它的布局）")
D.Interface("virtual void BindVertexBuffer(std::uint32_t Binding, FRHIBuffer* Buffer, "
            "std::uint64_t Offset) override",
            "`vkCmdBindVertexBuffers`")
D.Interface("virtual void BindIndexBuffer(FRHIBuffer* Buffer, std::uint64_t Offset, "
            "bool bIndex32) override",
            "绑定索引缓冲（位宽决定 `VK_INDEX_TYPE_UINT16` / `UINT32`）")
D.Interface("virtual void Draw(std::uint32_t VertexCount, std::uint32_t InstanceCount, "
            "std::uint32_t FirstVertex, std::uint32_t FirstInstance) override",
            "`vkCmdDraw`")
D.Interface("virtual void DrawIndexed(std::uint32_t IndexCount, std::uint32_t InstanceCount, "
            "std::uint32_t FirstIndex, std::int32_t VertexOffset, "
            "std::uint32_t FirstInstance) override",
            "`vkCmdDrawIndexed`（`VertexOffset` 有符号，可为负）")
D.Interface("virtual void DrawIndirect(FRHIBuffer* ArgsBuffer, std::uint64_t ArgsOffset, "
            "std::uint32_t DrawCount, std::uint32_t Stride) override",
            "`vkCmdDrawIndirect`")
D.Interface("virtual void DrawIndexedIndirect(FRHIBuffer* ArgsBuffer, std::uint64_t ArgsOffset, "
            "std::uint32_t DrawCount, std::uint32_t Stride) override",
            "`vkCmdDrawIndexedIndirect`")
D.Interface("virtual void DrawIndirectCount(FRHIBuffer* ArgsBuffer, std::uint64_t ArgsOffset, "
            "FRHIBuffer* CountBuffer, std::uint64_t CountOffset, std::uint32_t MaxDrawCount, "
            "std::uint32_t Stride) override",
            "`vkCmdDrawIndirectCount`（条数由 GPU 写；`MaxDrawCount` 是安全上界）")
D.Interface("virtual void DrawIndexedIndirectCount(FRHIBuffer* ArgsBuffer, std::uint64_t ArgsOffset, "
            "FRHIBuffer* CountBuffer, std::uint64_t CountOffset, std::uint32_t MaxDrawCount, "
            "std::uint32_t Stride) override",
            "`vkCmdDrawIndexedIndirectCount`")
D.Interface("virtual void BindComputePipeline(FRHIComputePipeline* Pipeline) override",
            "绑定计算管线（同样记下来给 push constant 用）")
D.Interface("virtual void Dispatch(std::uint32_t GroupCountX, std::uint32_t GroupCountY, "
            "std::uint32_t GroupCountZ) override",
            "`vkCmdDispatch`")
D.Interface("virtual void DispatchIndirect(FRHIBuffer* ArgsBuffer, std::uint64_t ArgsOffset) override",
            "`vkCmdDispatchIndirect`")
D.Interface("virtual void BindDescriptorSets(std::uint32_t FirstSet, FRHIDescriptorSet* const* Sets, "
            "std::uint32_t Count, const std::uint32_t* DynamicOffsets, "
            "std::uint32_t DynamicOffsetCount) override",
            "`vkCmdBindDescriptorSets`：**管线布局取自最近绑定的那条管线**（所以先绑管线再绑 set，"
            "与 Vulkan 的要求一致）")
D.Interface("virtual void UpdateDescriptorSet(FRHIDescriptorSet* Set, const FRHIDescriptorWrite* Writes, "
            "std::uint32_t WriteCount) override",
            "录制期立刻写 set（`vkUpdateDescriptorSets`，不是录制的命令）—— 归在列表上只为"
            "「与其它命令一起编排」；**被在飞提交引用的 set 不能在这里写**（由调用方用环形 / 分槽保证）")
D.Interface("virtual void PushConstants(ERHIShaderStage Stages, std::uint32_t Offset, "
            "std::uint32_t Size, const void* Data) override",
            "`vkCmdPushConstants`（布局同样来自最近绑定的管线）")
D.Interface("virtual void BeginQuery(FRHIQueryPool* Pool, std::uint32_t QueryIndex) override",
            "`vkCmdBeginQuery`")
D.Interface("virtual void EndQuery(FRHIQueryPool* Pool, std::uint32_t QueryIndex) override",
            "`vkCmdEndQuery`")
D.Interface("virtual void WriteTimestamp(FRHIQueryPool* Pool, std::uint32_t QueryIndex) override",
            "`vkCmdWriteTimestamp`（时间戳需要设备支持的时间戳有效期）")
D.Interface("virtual void ResetQueryPool(FRHIQueryPool* Pool, std::uint32_t FirstQuery, "
            "std::uint32_t QueryCount) override",
            "`vkCmdResetQueryPool`（池复用前必须重置）")
D.Interface("virtual void BuildAccelerationStructure(FRHIAccelerationStructure* Accel, "
            "FRHIBuffer* ScratchBuffer, std::uint64_t ScratchOffset) override",
            "经 `FRTRuntime::BuildAccel` 录 build（scratch 必须 DeviceAddress + Storage）")
D.Interface("virtual void CopyAccelerationStructure(FRHIAccelerationStructure* Dst, "
            "FRHIAccelerationStructure* Src) override",
            "经 `FRTRuntime::CopyAccel` 录拷贝（压缩 / refit 搬运）")
D.Interface("virtual void TraceRays(FRHIRayTracingPipeline* Pipeline, const FRHIRayTracingSbt& Sbt, "
            "std::uint32_t Width, std::uint32_t Height, std::uint32_t Depth) override",
            "经 `FRTRuntime::TraceRays` 发射光线（SBT 的三个 `VkStridedDeviceAddressRegionKHR` "
            "由 Sbt 的偏移 / 跨距填）")
D.SetAccess("private")
D.Interface("void AssertType(ERHICommandListType Allowed) const",
            "Debug 断言：本列表类型必须是 Allowed（**当场报错**优于驱动返回错误码 / 静默无效）")
D.Interface("void AssertNotTransfer() const", "Debug 断言：本列表不是传输类型（绘制 / 计算的前提）")
D.Field("ERHICommandListType Type = ERHICommandListType::Graphics", "列表类型（能力判定的唯一依据）")
D.Field("VkDevice Device = VK_NULL_HANDLE", "设备")
D.Field("VkCommandPool Pool = VK_NULL_HANDLE", "命令池（销毁时归还缓冲）")
D.Field("VkCommandBuffer Buffer = VK_NULL_HANDLE", "原生命令缓冲")
D.Field("FVulkanMemoryAllocator* Allocator = nullptr", "分配器（上传 staging / 直写 host 可见内存）")
D.Field("FRTRuntime RT", "光追函数指针（录制期用，避免再回设备查）")
D.Field("bool bRecording = false", "是否处于 Begin / End 之间（Debug 校验与重复 Begin 防护）")
D.Field("FRHIGraphicsPipeline* BoundGraphicsPipeline = nullptr",
        "最近绑定的栅格管线（push constant / 描述符需要它的布局）")
D.Field("FRHIComputePipeline* BoundComputePipeline = nullptr", "最近绑定的计算管线（同上）")

# ══════════════════════════════════════════════════════════════════════════════
# Private/VulkanRHI.h —— Vulkan 设备实现（含交换链 / 队列 / 光追）
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Private/VulkanRHI.h", Title="VulkanRHI.h —— Vulkan 设备实现",
         Desc="`IDynamicRHI` 的 Vulkan 实现：实例 / 物理设备 / 逻辑设备 / 交换链 / 队列 / 命令池 / "
              "同步对象 / 光追扩展函数指针，全在这一个类里。**它不在 Maho 的公开 API 面上** —— "
              "公开面只有 `IRHI`，所以这里的每个 `Vk*` 句柄都出不了这个 DLL。\n"
              "初始化被拆成一串 `Create*` 步骤（每步一个 `bool`）：这样失败点自解释"
              "（「哪一步没成」一眼可见），而且交换链重建可以复用其中的 `DestroySwapchainResources` "
              "+ 后半段，不必写第二份初始化路径。\n"
              "两个刻意的设计点：\n"
              "**每张交换链镜像一个 `RenderFinishedSemaphores`**（按当前镜像下标取）—— "
              "呈现用的信号量必须与被呈现的镜像配对，共用一个会与上一次呈现产生竞态。\n"
              "**交换链帧缓冲 / 渲染通道以非持有 RHI 视图暴露**（`SwapchainFramebufferRHI` / "
              "`SwapchainRenderPassRHI`）：帧命令缓冲要用它们 `BeginRenderPass`，"
              "但所有权仍在交换链手里，RHI 视图析构时什么都不做。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("RHI.h", "`IDynamicRHI` / `FRHIInitDesc` / `FRHIFactory`（本类的基类与接口）")
D.Row("VulkanCommandList.h", "`FVulkanQueue` / `FVulkanCommandList` / `FVulkanMemoryAllocator` 前向")
D.Row("VulkanMemory.h", "`FVulkanMemoryAllocator`（本类私有持有）")
D.Row("VulkanResources.h", "创建资源时的 `FVulkanxxx` 派生类")
D.Row("memory / vector", "`unique_ptr`（分配器）/ 交换链镜像等数组")
D.Row("vulkan/vulkan.h", "全部原生类型与 `PFN_vk*` 扩展函数指针")

D.Class("FVulkanRHI", Base="IDynamicRHI",
        Desc="Vulkan 设备。**最小可用实现**：一个图形族 + 可选独立计算 / 传输族、"
             "一条帧命令缓冲路径、交换链、光追（可选特性，缺失时相关工厂返回 nullptr）。"
             "所有 Vulkan 细节止步于这个类（与它创建的资源对象）。")
D.SetAccess("public")
D.Interface("FVulkanRHI()", "构造：全部句柄置空（真正的创建都在 `Initialize`）")
D.Interface("~FVulkanRHI() override",
            "析构：**幂等关闭**（若宿主没显式 `Shutdown` 也能安全收尾；先 `WaitIdle` 再拆资源）")
D.Interface("virtual bool Initialize(const FRHIInitDesc& Desc) override",
            "逐步建设备：实例 → 调试信使 → 表面 → 物理设备 → 逻辑设备 → 交换链 → 图像视图 → "
            "渲染通道 → 帧缓冲 → 命令池与缓冲 → 队列与池 → 分配器 → 同步对象 → 光追函数。"
            "任一步 false 即整体失败")
D.Interface("virtual void Shutdown() override", "逆序拆除（幂等）：先 `WaitIdle`，再释放资源 / 设备 / 实例")
D.Interface("virtual void BeginFrame() override",
            "等上一帧栅栏 → 取下一个镜像 → 收起上一帧的延迟释放（`FlushDeferredFrees`，"
            "**此刻上一帧栅栏已触发**，所以那些 staging 的拷贝确实做完了）→ 开始录帧命令缓冲")
D.Interface("virtual void EndFrame() override",
            "结束帧命令缓冲 → 提交（等待镜像可用信号量、发出呈现信号量、挂上在飞栅栏）→ 呈现。"
            "尺寸变化（表面过期 / 子最优）就地重建交换链后重试")
D.Interface("virtual void Resize(int Width, int Height) override",
            "记录新尺寸并置 `bFramebufferResized`；真正的重建在帧边界做（不在信号回调里重建）")
D.Interface("virtual void WaitIdle() override", "`vkDeviceWaitIdle`（拆除 / 交换链重建前使用）")
D.Interface("[[nodiscard]] virtual FRHICommandList* GetFrameCommandList() override",
            "返回包住帧命令缓冲的**非持有** RHI 对象（特性借它录 pass，不能 Begin / End）")
D.Interface("virtual void PresentTexture(FRHITexture* Src) override",
            "离屏纹理 blit 到当前镜像（**格式必须与交换链一致**）+ 布局转换，"
            "全程录在帧命令缓冲上，最后交给 `EndFrame` 提交")
D.Interface("[[nodiscard]] virtual ERHIFormat GetSwapchainFormat() const override",
            "交换链格式（离屏场景目标据此选择）")
D.Interface("[[nodiscard]] virtual bool IsInitialized() const override", "是否已完整初始化")
D.Interface("[[nodiscard]] virtual IDynamicRHIMemoryAllocator* GetMemoryAllocator() override",
            "返回 VMA 分配器（`FRHI` 用它判断 host 可见性）")
D.Interface("[[nodiscard]] virtual FRHIQueue& GetGraphicsQueue() override", "逻辑图形队列端点")
D.Interface("[[nodiscard]] virtual FRHIQueue& GetComputeQueue() override", "逻辑计算队列端点")
D.Interface("[[nodiscard]] virtual FRHIQueue& GetTransferQueue() override", "逻辑传输队列端点")
D.Interface("[[nodiscard]] virtual FRHICommandList* CreateCommandList(ERHICommandListType Type) override",
            "按类型从对应族的命令池分配缓冲 + 包成 `FVulkanCommandList`")
D.Interface("virtual void DestroyCommandList(FRHICommandList* CmdList) override", "释放命令列表（归还池）")
D.Interface("[[nodiscard]] virtual FRHIFence* CreateFence(bool bSignaled) override", "建栅栏")
D.Interface("virtual void DestroyFence(FRHIFence* Fence) override", "销毁栅栏")
D.Interface("virtual void WaitForFence(FRHIFence* Fence, std::uint64_t TimeoutNs) override",
            "等待栅栏触发（超时按纳秒）")
D.Interface("[[nodiscard]] virtual bool IsFenceSignaled(FRHIFence* Fence) override", "非阻塞查询")
D.Interface("virtual void ResetFence(FRHIFence* Fence) override", "复位栅栏（复用前必须做）")
D.Interface("[[nodiscard]] virtual FRHISemaphore* CreateGpuSemaphore() override", "建信号量")
D.Interface("virtual void DestroyGpuSemaphore(FRHISemaphore* Semaphore) override", "销毁信号量")
D.Interface("[[nodiscard]] virtual FRHIBuffer* CreateBuffer(const FRHIBufferDesc& Desc) override",
            "建缓冲（用途 → `VkBufferUsageFlags`、内存用途 → VMA 分配参数；按需取设备地址）")
D.Interface("virtual void DestroyBuffer(FRHIBuffer* Buffer) override", "销毁缓冲")
D.Interface("[[nodiscard]] virtual FRHITexture* CreateTexture(const FRHITextureDesc& Desc) override",
            "建纹理（维度 / 层 / mip → `VkImageCreateInfo`）")
D.Interface("virtual void DestroyTexture(FRHITexture* Texture) override", "销毁纹理")
D.Interface("[[nodiscard]] virtual FRHISampler* CreateSampler(const FRHISamplerDesc& Desc) override",
            "建采样器（过滤 / 寻址 / LOD 映射）")
D.Interface("virtual void DestroySampler(FRHISampler* Sampler) override", "销毁采样器")
D.Interface("[[nodiscard]] virtual FRHIShaderModule* CreateShaderModule("
            "const FRHIShaderModuleDesc& Desc) override",
            "建着色器模块（SPIR-V 字节码 → `VkShaderModule`）")
D.Interface("virtual void DestroyShaderModule(FRHIShaderModule* Module) override", "销毁着色器模块")
D.Interface("[[nodiscard]] virtual FRHIGraphicsPipeline* CreateGraphicsPipeline("
            "const FRHIGraphicsPipelineDesc& Desc) override",
            "建栅格管线（顶点输入 / 光栅 / 深度 / 混合全参数展开；**很贵**，调用方要缓存）")
D.Interface("virtual void DestroyGraphicsPipeline(FRHIGraphicsPipeline* Pipeline) override",
            "销毁栅格管线")
D.Interface("[[nodiscard]] virtual FRHIComputePipeline* CreateComputePipeline("
            "const FRHIComputePipelineDesc& Desc) override",
            "建计算管线")
D.Interface("virtual void DestroyComputePipeline(FRHIComputePipeline* Pipeline) override", "销毁计算管线")
D.Interface("[[nodiscard]] virtual FRHIStructuredBuffer* CreateStructuredBuffer("
            "const FRHIStructuredBufferDesc& Desc) override",
            "建结构化缓冲（底层是真缓冲 + 一层 stride 描述）")
D.Interface("virtual void DestroyStructuredBuffer(FRHIStructuredBuffer* Buffer) override",
            "销毁结构化缓冲（连带底层缓冲）")
D.Interface("[[nodiscard]] virtual FRHIBufferView* CreateBufferView(const FRHIBufferViewDesc& Desc) override",
            "建缓冲视图（`VkBufferView`）")
D.Interface("virtual void DestroyBufferView(FRHIBufferView* View) override", "销毁缓冲视图")
D.Interface("[[nodiscard]] virtual FRHITextureView* CreateTextureView("
            "const FRHITextureViewDesc& Desc) override",
            "建纹理视图（mip / 层区间 + 可选换格式）")
D.Interface("virtual void DestroyTextureView(FRHITextureView* View) override", "销毁纹理视图")
D.Interface("[[nodiscard]] virtual FRHIDescriptorSetLayout* CreateDescriptorSetLayout("
            "const FRHIDescriptorSetLayoutDesc& Desc) override",
            "建 set 布局（绑定 + `bPartiallyBound` / `bVariableCount` 映射到对应标志位）")
D.Interface("virtual void DestroyDescriptorSetLayout(FRHIDescriptorSetLayout* Layout) override",
            "销毁 set 布局")
D.Interface("[[nodiscard]] virtual FRHIPipelineLayout* CreatePipelineLayout("
            "const FRHIPipelineLayoutDesc& Desc) override",
            "建管线布局（set 布局数组 + push constant 区间）")
D.Interface("virtual void DestroyPipelineLayout(FRHIPipelineLayout* Layout) override", "销毁管线布局")
D.Interface("[[nodiscard]] virtual FRHIDescriptorPool* CreateDescriptorPool("
            "const FRHIDescriptorPoolDesc& Desc) override",
            "建描述符池（各类型容量 + set 上限 + update-after-bind 标志）")
D.Interface("virtual void DestroyDescriptorPool(FRHIDescriptorPool* Pool) override", "销毁描述符池")
D.Interface("[[nodiscard]] virtual FRHIDescriptorSet* AllocateDescriptorSet(FRHIDescriptorPool* Pool, "
            "FRHIDescriptorSetLayout* Layout) override",
            "从池分配 set（O(1)，但池容量是预付的）")
D.Interface("virtual void FreeDescriptorSet(FRHIDescriptorPool* Pool, FRHIDescriptorSet* Set) override",
            "归还 set 到**创建它的池**")
D.Interface("virtual void UpdateDescriptorSets(const FRHIDescriptorWrite* Writes, "
            "std::uint32_t Count) override",
            "写 set 内容。信息结构体是**栈上临时存储**，活到 `vkUpdateDescriptorSets` 返回即可 —— "
            "这条是不需要命令缓冲的全部依据")
D.Interface("[[nodiscard]] virtual FRHIRenderPass* CreateRenderPass(const FRHIRenderPassDesc& Desc) override",
            "建渲染通道（颜色 / 深度附件 + load / store + 采样数）")
D.Interface("virtual void DestroyRenderPass(FRHIRenderPass* Pass) override", "销毁渲染通道")
D.Interface("[[nodiscard]] virtual FRHIFramebuffer* CreateFramebuffer("
            "const FRHIFramebufferDesc& Desc) override",
            "建帧缓冲（通道 + 附件视图 + 尺寸）")
D.Interface("virtual void DestroyFramebuffer(FRHIFramebuffer* Framebuffer) override", "销毁帧缓冲")
D.Interface("[[nodiscard]] virtual std::uint32_t GetFramebufferWidth() const override",
            "当前交换链宽（重建后立即更新）")
D.Interface("[[nodiscard]] virtual std::uint32_t GetFramebufferHeight() const override",
            "当前交换链高")
D.Interface("[[nodiscard]] virtual FRHIQueryPool* CreateQueryPool(ERHIQueryType Type, "
            "std::uint32_t QueryCount) override",
            "建查询池（遮挡 / 时间戳）")
D.Interface("virtual void DestroyQueryPool(FRHIQueryPool* Pool) override", "销毁查询池")
D.Interface("virtual bool GetQueryPoolResults(FRHIQueryPool* Pool, std::uint32_t FirstQuery, "
            "std::uint32_t QueryCount, std::uint64_t* Results, std::size_t Stride, "
            "bool bWait) override",
            "读查询结果；`bWait` 为真即 `vkWaitForFences` + 读 —— **同步读，别在渲染线程用**")
D.Interface("[[nodiscard]] virtual FRHIRayTracingPipeline* CreateRayTracingPipeline("
            "const FRHIRayTracingPipelineDesc& Desc) override",
            "建光追管线（经 `CreateRayTracingPipelinesKHR`；不支持时 nullptr）")
D.Interface("virtual void DestroyRayTracingPipeline(FRHIRayTracingPipeline* Pipeline) override",
            "销毁光追管线")
D.Interface("[[nodiscard]] virtual FRHIAccelerationStructure* CreateAccelerationStructure("
            "const FRHIRayTracingGeometryDesc& Desc) override",
            "建加速结构：先问 build 尺寸 → 建 DeviceAddress 存储缓冲 → "
            "`vkCreateAccelerationStructureKHR`（句柄落在那段缓冲上）")
D.Interface("virtual void DestroyAccelerationStructure(FRHIAccelerationStructure* Accel) override",
            "销毁加速结构（连存储缓冲与分配一起）")
D.Interface("virtual bool GetAccelerationStructureBuildSizes(const FRHIRayTracingGeometryDesc& Desc, "
            "std::uint64_t& OutAccelSize, std::uint64_t& OutScratchSize) override",
            "`vkGetAccelerationStructureBuildSizesKHR`：本体尺寸 + scratch 尺寸")
D.Interface("[[nodiscard]] virtual FRHIBuffer* CreateShaderBindingTable(FRHIRayTracingPipeline* Pipeline, "
            "const FRHISbtGroup* Groups, std::uint32_t GroupCount, "
            "std::uint32_t* OutRayGenOffset, std::uint32_t* OutRayGenStride, "
            "std::uint32_t* OutHitOffset, std::uint32_t* OutHitStride, "
            "std::uint32_t* OutMissOffset, std::uint32_t* OutMissStride) override",
            "铺 SBT：逐分组取 `vkGetRayTracingShaderGroupHandlesKHR`，按对齐要求摆进一块 "
            "DeviceAddress + Storage 缓冲，并回填各段偏移 / 跨距")
D.Interface("[[nodiscard]] VkInstance GetVkInstance() const", "原生实例（模块内取扩展函数用）")
D.Interface("[[nodiscard]] VkPhysicalDevice GetVkPhysicalDevice() const", "物理设备")
D.Interface("[[nodiscard]] VkDevice GetVkDevice() const", "逻辑设备")
D.Interface("[[nodiscard]] VkQueue GetVkGraphicsQueue() const", "图形原生队列")
D.Interface("[[nodiscard]] std::uint32_t GetGraphicsQueueFamilyIndex() const", "图形族索引")
D.Interface("[[nodiscard]] VkRenderPass GetVkRenderPass() const", "交换链渲染通道（帧命令缓冲用它开通道）")
D.Interface("[[nodiscard]] VkCommandBuffer GetVkCommandBuffer() const", "帧命令缓冲")
D.Interface("[[nodiscard]] std::uint32_t GetSwapchainImageCount() const", "交换链镜像数（同步对象按它配）")
D.Interface("[[nodiscard]] std::uint32_t GetMinImageCount() const", "可接受的最小镜像数（重建时的下界）")
D.SetAccess("private")
D.Interface("bool CreateInstance()", "创建 `VkInstance`（校验层 / 扩展按构建配置启用）")
D.Interface("void CreateDebugMessenger()", "创建调试信使（校验输出接日志；非 Debug 构建为空操作）")
D.Interface("bool CreateSurface()", "从 `NativeWindowHandle` 创建呈现表面（WSI）")
D.Interface("bool PickPhysicalDevice()", "挑物理设备（够用即可：队列族 + 必需扩展 + 交换链能力）")
D.Interface("bool CreateLogicalDevice()", "创建逻辑设备 + 取队列（含计算 / 传输是否独立的判定）")
D.Interface("bool CreateSwapchain()", "创建交换链（格式 / 呈现模式 / 尺寸按表面能力挑）")
D.Interface("void DestroySwapchainResources()", "销毁交换链相关资源；**重建路径复用这一半**，"
                                               "不写第二份初始化")
D.Interface("bool CreateImageViews()", "为每个镜像建图像视图")
D.Interface("bool CreateRenderPass()", "创建交换链渲染通道（颜色附件 = 交换链格式）")
D.Interface("bool CreateFramebuffers()", "为每个镜像建帧缓冲（+ 对应的非持有 RHI 视图）")
D.Interface("bool CreateCommandPoolAndBuffer()", "创建帧命令池与帧命令缓冲")
D.Interface("bool CreateLogicalQueuesAndPools()", "为三个逻辑队列建端点与各自族的命令池")
D.Interface("bool CreateMemoryAllocator()", "创建 VMA 分配器")
D.Interface("bool CreateSyncObjects()", "创建图像可用信号量 + 在飞栅栏")
D.Interface("bool CreateRenderFinishedSemaphores()", "**每镜像一个**呈现完成信号量（与被呈现的镜像配对，"
                                                     "共用一个会与上次呈现竞态）")
D.Interface("bool RecreateSwapchain()",
            "重建：`WaitIdle` → 拆交换链资源 → 重建交换链 / 视图 / 通道 / 帧缓冲 / 命令缓冲。"
            "尺寸为 0（最小化）时等待后重试")
D.Interface("[[nodiscard]] bool IsDeviceSuitable(VkPhysicalDevice InPhysicalDevice)",
            "设备是否够用（交换链支持 + 需要的特性）")
D.Interface("[[nodiscard]] bool FindQueueFamilies(VkPhysicalDevice InPhysicalDevice)",
            "找队列族：图形 / 呈现 / 计算 / 传输，并判定计算 / 传输是否回落")
D.Interface("[[nodiscard]] bool CheckDeviceExtensionSupport(VkPhysicalDevice InPhysicalDevice) const",
            "检查必需扩展（交换链 + 光追可选）")
D.Interface("[[nodiscard]] VkCommandPool GetPoolForType(ERHICommandListType Type) const",
            "按列表类型取对应族的命令池（图形 / 计算 / 传输）")
D.Interface("[[nodiscard]] static VkBufferUsageFlags ToVkBufferUsage(ERHIBufferUsage Usage)",
            "用途位掩码 → `VkBufferUsageFlags`（**后端映射的唯一定义点**）")
D.Interface("[[nodiscard]] static VkImageUsageFlags ToVkImageUsage(ERHITextureUsage Usage)",
            "纹理用途 → `VkImageUsageFlags`")
D.Interface("[[nodiscard]] static VkFormat ToVkFormat(ERHIFormat Format)", "格式 → `VkFormat`")
D.Interface("[[nodiscard]] static VkDescriptorType ToVkDescriptorType(ERHIDescriptorType Type)",
            "描述符类型 → `VkDescriptorType`")
D.Interface("[[nodiscard]] static VkFilter ToVkFilter(ERHIFilter Filter)", "过滤 → `VkFilter`")
D.Interface("[[nodiscard]] static VkSamplerAddressMode ToVkAddressMode(ERHIAddressMode Mode)",
            "寻址模式 → `VkSamplerAddressMode`")
D.Field("void* NativeWindowHandle = nullptr", "窗口句柄（初始化时记住，重建表面用）")
D.Field("int FramebufferWidth = 0", "当前宽")
D.Field("int FramebufferHeight = 0", "当前高")
D.Field("bool bInitialized = false", "是否已完整初始化（`IsInitialized` 的唯一依据）")
D.Field("VkInstance Instance = VK_NULL_HANDLE", "实例")
D.Field("VkDebugUtilsMessengerEXT DebugMessenger = VK_NULL_HANDLE", "调试信使（校验输出）")
D.Field("VkSurfaceKHR Surface = VK_NULL_HANDLE", "呈现表面")
D.Field("VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE", "物理设备")
D.Field("VkDevice Device = VK_NULL_HANDLE", "逻辑设备")
D.Field("VkQueue GraphicsVkQueue = VK_NULL_HANDLE", "图形原生队列")
D.Field("VkQueue PresentQueue = VK_NULL_HANDLE", "呈现原生队列（图形族不同时可能不同）")
D.Field("VkQueue ComputeVkQueue = VK_NULL_HANDLE", "计算原生队列")
D.Field("VkQueue TransferVkQueue = VK_NULL_HANDLE", "传输原生队列")
D.Field("FVulkanQueue GraphicsQueue", "逻辑图形端点")
D.Field("FVulkanQueue ComputeQueue", "逻辑计算端点")
D.Field("FVulkanQueue TransferQueue", "逻辑传输端点")
D.Field("VkSwapchainKHR Swapchain = VK_NULL_HANDLE", "交换链")
D.Field("VkFormat SwapchainImageFormat = VK_FORMAT_UNDEFINED", "交换链格式（离屏目标必须匹配）")
D.Field("VkExtent2D SwapchainExtent{}", "交换链尺寸")
D.Field("std::vector<VkImage> SwapchainImages", "交换链镜像（交换链拥有）")
D.Field("std::vector<VkImageView> SwapchainImageViews", "镜像视图")
D.Field("std::vector<VkFramebuffer> SwapchainFramebuffers", "镜像帧缓冲")
D.Field("std::vector<FRHIFramebuffer*> SwapchainFramebufferRHI",
        "**非持有** RHI 视图：帧命令缓冲 `BeginRenderPass` 要用；所有权仍在交换链，"
        "RHI 视图析构不动它们")
D.Field("FRHIRenderPass* SwapchainRenderPassRHI = nullptr",
        "交换链渲染通道的**非持有** RHI 视图（同上：只借用，不拥有）")
D.Field("VkRenderPass RenderPass = VK_NULL_HANDLE", "交换链渲染通道")
D.Field("VkCommandPool CommandPool = VK_NULL_HANDLE", "帧命令池（图形族）")
D.Field("VkCommandBuffer CommandBuffer = VK_NULL_HANDLE", "帧命令缓冲（每帧复用，靠栅栏保证不再被引用）")
D.Field("FRHICommandList* FrameCommandListRHI = nullptr",
        "包住帧命令缓冲的**非持有**录制面（借给特性录 pass）")
D.Field("VkCommandPool GraphicsCmdPool = VK_NULL_HANDLE", "图形族命令池（按需建列表用）")
D.Field("VkCommandPool ComputeCmdPool = VK_NULL_HANDLE", "计算族命令池")
D.Field("VkCommandPool TransferCmdPool = VK_NULL_HANDLE", "传输族命令池")
D.Field("VkSemaphore ImageAvailableSemaphore = VK_NULL_HANDLE", "图像可用信号量（呈现引擎 → 渲染）")
D.Field("std::vector<VkSemaphore> RenderFinishedSemaphores",
        "**每镜像一个**呈现完成信号量（按 `CurrentImageIndex` 取：与被呈现的镜像配对）")
D.Field("VkFence InFlightFence = VK_NULL_HANDLE", "在飞栅栏（本帧开始前等它，确保帧命令缓冲可复用）")
D.Field("std::uint32_t GraphicsQueueFamilyIndex = 0", "图形族索引")
D.Field("std::uint32_t PresentQueueFamilyIndex = 0", "呈现族索引")
D.Field("std::uint32_t ComputeQueueFamilyIndex = 0", "计算族索引")
D.Field("std::uint32_t TransferQueueFamilyIndex = 0", "传输族索引")
D.Field("std::uint32_t GraphicsQueueIndex = 0", "族内队列下标（图形）")
D.Field("std::uint32_t ComputeQueueIndex = 0", "族内队列下标（计算）")
D.Field("std::uint32_t TransferQueueIndex = 0", "族内队列下标（传输）")
D.Field("bool bComputeNativeFallback = false", "计算是否回落到非独立原生队列（提交要改成图形队列）")
D.Field("bool bTransferNativeFallback = false", "传输是否回落")
D.Field("std::uint32_t CurrentImageIndex = 0", "本帧取得的交换链镜像下标（同步对象按它索引）")
D.Field("float ClearColorR / G / B / A", "交换链通道的清理色（默认不透明黑）")
D.Field("bool bFramebufferResized = false", "尺寸变化标志（在帧边界才重建，不在信号回调里重建）")
D.Field("bool bRayTracingSupported = false", "设备是否支持光追（不支持则相关工厂返回 nullptr）")
D.Field("PFN_vkCreateAccelerationStructureKHR CreateAccelerationStructureKHR = nullptr",
        "光追扩展函数（设备级，初始化时动态取）")
D.Field("PFN_vkDestroyAccelerationStructureKHR DestroyAccelerationStructureKHR = nullptr",
        "销毁 AS 函数")
D.Field("PFN_vkGetAccelerationStructureBuildSizesKHR GetAccelerationStructureBuildSizesKHR = nullptr",
        "查询 build 尺寸函数")
D.Field("PFN_vkCreateRayTracingPipelinesKHR CreateRayTracingPipelinesKHR = nullptr", "建光追管线函数")
D.Field("PFN_vkGetRayTracingShaderGroupHandlesKHR GetRayTracingShaderGroupHandlesKHR = nullptr",
        "取 SBT 组句柄函数")
D.Field("PFN_vkCmdBuildAccelerationStructuresKHR CmdBuildAccelerationStructuresKHR = nullptr",
        "录制 build 函数（转交给命令列表的 `FRTRuntime`）")
D.Field("PFN_vkCmdCopyAccelerationStructureKHR CmdCopyAccelerationStructureKHR = nullptr",
        "录制 AS 拷贝函数")
D.Field("PFN_vkCmdTraceRaysKHR CmdTraceRaysKHR = nullptr", "录制发光线函数")
D.Field("PFN_vkGetBufferDeviceAddressKHR GetBufferDeviceAddressKHR = nullptr",
        "取缓冲设备地址函数（SBT / AS scratch 必需）")
D.Field("std::unique_ptr<FVulkanMemoryAllocator> MemoryAllocator", "VMA 分配器（设备唯一，最后析构）")
