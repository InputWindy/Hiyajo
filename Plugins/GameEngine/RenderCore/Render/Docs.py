# -*- coding: utf-8 -*-
# Render 插件文档内容（由 Tools/plugin_docs.py 执行，docs_builder 以变量 D 注入）。
#
# 分层：Public/ = 渲染特性面对的全部词汇（stage 接口 / RDG 声明 / 绘制协议 / 着色器）；
#       Private/RenderResourcePool.h = RDG 的资源池实现（原子分配层）。
# 内容按头文件逐条声明：Header → Card/Table/Row → Class/Struct → Interface/Field。

# ══════════════════════════════════════════════════════════════════════════════
# Public/RenderApi.h —— Render 模块导出标签
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/RenderApi.h", Title="RenderApi.h —— Render 模块导出标签",
         Desc="Render 把一批多态类型交给别的模块：stage 接口（`IBeginRender` 等，特性实现、"
              "引擎侧驱动）、`FRender`（宿主经它拿画布尺寸）、RDG 的引用类型（`FRDGTextureRef`）。"
              "这些实例可能在**另一个模块**里被构造或析构，所以必须走导出标签。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT`")

D.Card("导出标签")
D.Table("宏", "说明")
D.Row("MAHO_RENDER_API",
      "编译 Render 本模块时 codegen 定义 `MAHO_RENDER_MODULE_EXPORTS` ⇒ `MAHO_EXPORT`，"
      "消费方只拿 `MAHO_IMPORT`。**为什么必须导出整族类：**全内联的派生类（如某个特性的 stage 类）"
      "若不导入就是「vftable 由构造它的模块发射」—— 特性 DLL 构造、引擎 DLL 经基类指针销毁时，"
      "`delete` 会跳进已释放的映像")

# ══════════════════════════════════════════════════════════════════════════════
# Public/RDG.h —— RDG 资源引用 / 渲染目标 / pass 输入参数
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/RDG.h", Title="RDG.h —— RDG 资源引用 / 渲染目标 / pass 输入参数",
         Desc="渲染特性的**声明词汇**：它想用一块离屏资源、想渲染到某个目标、想把哪些资源绑给"
              "着色器，全在这里声明 —— 但**一个 RHI 对象都看不到**。\n"
              "两条关键设计：\n"
              "**引用是池句柄**（`FRDGTextureRef` / `FRDGBufferRef` = 池指针 + 槽位号）："
              "原生的 `FRHITexture` / `FRHIBuffer` 活在资源池里跨帧复用，特性只持引用；"
              "`GetRHI()` / `GetView()` 是**当前帧内有效的解析点**（帧外调用没有意义）。\n"
              "**输入与输出分两半**（`FPassParameter` 输入 vs `FRenderTarget` 输出）：两者方向相反，"
              "所以并排成一个 `FRenderPassDesc`，而**不合并** —— 渲染目标里塞绑定会让「渲染到哪」与"
              "「读什么」的边界含混。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("RenderApi.h", "`MAHO_RENDER_API`")
D.Row("RHI/RHIEnums.h", "load / store / 描述符类型 / 阶段等枚举")
D.Row("RHI/RHIResources.h", "`FRHITextureDesc` / `FRHIxxxDesc`（池创建资源时的描述）")
D.Row("cstdint / utility / variant / vector", "定宽整数 / `std::pair` / `std::variant`（引用 = 纹理或缓冲）/ 容器")

D.Card("ERDGResourceLifetime", "RDG 分配的**原子生命期契约**；上层能力（VMA 别名、引用图）都建在它上面，"
                               "所以形状要保持稳定。")
D.Table("枚举值", "说明")
D.Row("Persistent", "跨帧、身份稳定（按描述命中复用）。原生 + 显存活到池 Shutdown，**不按帧回收**")
D.Row("Transient", "帧内。原生 + 显存**跨帧保留并复用**（没有每帧 `vkCreate` / `vkAllocate`）："
                   "下一帧同描述请求直接拿回；只有描述变了才重建")

D.Class("FRDGTextureRef", Desc="离屏纹理的非 RHI 引用（池 + 槽位号）。默认构造 = 空引用；"
        "拷贝便宜（两个整数），所以可以按值传进绘制协议。")
D.SetAccess("public")
D.Interface("FRDGTextureRef() = default", "空引用（`IsValid()` 为假）")
D.Interface("[[nodiscard]] bool IsValid() const", "池非空且槽位有效")
D.Interface("void Reset()", "清成空引用（不释放原生资源）")
D.Interface("[[nodiscard]] FRHITexture* GetRHI() const",
            "**仅当前帧有效**的解析点到池里的原生纹理（帧外无意义）")
D.Interface("[[nodiscard]] FRHITextureView* GetView() const", "同上，解析到池里的纹理视图")
D.Interface("[[nodiscard]] ERHIFormat GetFormat() const",
            "创建时的格式（池回读缓存的描述）—— 特性据此确定目标格式，不必去问交换链 / RHI")
D.Interface("[[nodiscard]] std::uint32_t GetWidth() const", "创建时的宽")
D.Interface("[[nodiscard]] std::uint32_t GetHeight() const", "创建时的高")
D.SetAccess("private")
D.Interface("FRDGTextureRef(FRHIResourcePool* InPool, std::uint32_t InId)",
            "私有构造：只有池能签发引用（`FRHIResourcePool` 是 friend）")
D.Field("FRHIResourcePool* Pool = nullptr", "签发它的池（身份的一半）")
D.Field("std::uint32_t Id = ~0u", "槽位号（`~0u` = 空）")

D.Class("FRDGBufferRef", Desc="池化 GPU 缓冲的非 RHI 引用。解析语义与 `FRDGTextureRef` 相同。")
D.SetAccess("public")
D.Interface("FRDGBufferRef() = default", "空引用")
D.Interface("[[nodiscard]] bool IsValid() const", "池非空且槽位有效")
D.Interface("void Reset()", "清成空引用（不释放原生资源）")
D.Interface("[[nodiscard]] FRHIBuffer* GetRHI() const", "当前帧内解析点到原生缓冲")
D.SetAccess("private")
D.Interface("FRDGBufferRef(FRHIResourcePool* InPool, std::uint32_t InId)", "私有构造：只有池能签发")
D.Field("FRHIResourcePool* Pool = nullptr", "签发它的池")
D.Field("std::uint32_t Id = ~0u", "槽位号（`~0u` = 空）")

D.Struct("FRenderTarget", Desc="**虚拟输出目标**：用户声明「渲染到哪」。附件用 `FRDGTextureRef` 描述"
        "（离屏），所以特性永远看不到原生帧缓冲 / 交换链；`FRender` 在 `BeginRenderPass` 时把它"
        "解析成具体的渲染通道 + 帧缓冲（带缓存）。一个目标里的**全部附件必须同一个分辨率**"
        "（RHI 需要一个统一的渲染区域尺寸），但尺寸不单独声明 —— 从附件视图读回来。")
D.SetAccess("public")
D.Nested("FAttachment", Kind="struct", Desc="一个附件：引用 + load / store + 清理色。")
D.Field("std::vector<FAttachment> Color", "颜色附件列表（支持 MRT）")
D.Field("FAttachment Depth", "深度附件（`bHasDepth` 为假时无效）")
D.Field("bool bHasDepth = false", "是否设置了深度附件")
D.Field("std::uint32_t SampleCount = 1", "采样数（1 = 不开 MSAA）")
D.Interface("void AddColor(const FAttachment& Attach)", "追加一个颜色附件")
D.Interface("void SetDepth(const FAttachment& Attach)", "设置深度附件并置 `bHasDepth`")
D.Interface("[[nodiscard]] bool IsValid() const",
            "全部附件互不矛盾即有效。**空目标（无颜色无深度）算有效**（「未配置」状态没有尺寸可言）；"
            "一旦有附件，每个附件都必须解析成有效且同一个非零尺寸，否则在录制 pass 之前就判无效")
D.SetAccess("public")
D.Field("FRDGTextureRef View", "（FAttachment）附件引用的离屏纹理")
D.Field("ERHILoadOp LoadOp = ERHILoadOp::Clear", "（FAttachment）进入时的 load 语义")
D.Field("ERHIStoreOp StoreOp = ERHIStoreOp::Store", "（FAttachment）离开时的 store 语义")
D.Field("float ClearColor[4]", "（FAttachment）清理色，默认不透明黑")

D.Alias("FRDGResourceRef", "std::variant<FRDGTextureRef, FRDGBufferRef>",
        "「镜像是纹理还是缓冲」的二选一引用：pass 输入绑定两种都要能表达，"
        "池解析时取当前有效的那个。定义在 RDG.h（而不是 Render.h）是为了让 `FPassParameter` "
        "能引用它而不产生头文件环")

D.Struct("FRDGBinding", Desc="pass 参数里的**一个描述符绑定**：着色器面对的槽位（binding 号 + 类型）"
        "加用户绑定的值（一个 RDG 引用）。**采样器不在这里**（见 `FRDGDescriptorSet::Samplers`）："
        "资源与采样器分开，一个采样器才能被多个绑定共享（UE 式的切分）。用户永不接触 "
        "`FRHIDescriptorSet*`。")
D.Field("ERHIDescriptorType Type = ERHIDescriptorType::CombinedImageSampler", "描述符类型")
D.Field("ERHIShaderStage Stages = ERHIShaderStage::Fragment", "可见阶段")
D.Field("FRDGResourceRef Resource", "用户绑定的纹理 / 缓冲")
D.Field("std::int32_t SamplerIndex = -1", "指向 `FRDGDescriptorSet::Samplers` 的下标；-1 = 不用采样器")
D.Field("std::uint64_t Offset = 0", "缓冲 / uniform 区间起点")
D.Field("std::uint64_t Range = 0", "区间长度（0 = 到末尾）")

D.Card("EDescriptorSetFrequency", "描述符集**内容的更新粒度 / 生命期**（资源身份（set 号、binding）的"
                                  "语义不变量，与后端实现手段解耦）。**粒度必然是「整个 set」而不是"
                                  "单个绑定**：GLSL 把 set 号定死在声明处，所以绑定 / 重写的单位就是整个 set。")
D.Table("枚举值", "说明")
D.Row("Static = 0", "每场景、极少变化 —— 但**仍是可变 set**，「变化最小」不等于只读；默认档")
D.Row("PerFrame", "内容每帧变化（每帧 GPU 场景）。持久可变 set，录制期用 `UpdateDescriptorSet` 更新一次")
D.Row("PerPass", "内容因 pass 而异（basepass vs 后处理）。同一机制，每个绑定它的 pass 各更新一次")
D.Row("PerInstance", "每个网格批次都不同。单个可变 set 表达不了（只有一份内容，所有批次会读到最后一次写入）"
                     "⇒ 必须走 push descriptor 或动态偏移 UBO，**按绘制绑定**")

D.Struct("FRDGDescriptorSet", Desc="pass 参数里的一个 set：set 号 + 绑定表 + 该 set 的采样器池。"
        "系统据此填出 set 布局。")
D.Field("std::uint32_t SetIndex = 0", "set 号（与 GLSL 的 `set =` 一致）")
D.Field("EDescriptorSetFrequency Frequency = EDescriptorSetFrequency::Static",
        "内容更新粒度：Static ⇒ 内容可寻址池；PerFrame / PerPass ⇒ 持久可变 set（录制期写）；"
        "PerInstance ⇒ 按批次绑定。默认 Static，于是没设置过的老 pass 保持原有内容池行为")
D.Field("std::vector<FRHISampler*> Samplers", "池拥有的采样器参数（get-or-create），经 `SamplerIndex` 共享")
D.Field("std::vector<std::pair<std::uint32_t, FRDGBinding>> Bindings", "绑定表（binding 号 → 值）")
D.Interface("std::int32_t AddSampler(FRHISampler* Sampler)", "追加采样器并返回它的下标（给 `SamplerIndex` 用）")

D.Struct("FPassParameter", Desc="pass 的**输入半边**（`FRenderTarget` 是输出半边的镜像）："
        "用户声明各 set 的绑定 + push constant，`AddPass` 把它们物化成描述符集并绑定。"
        "绑定的结构（set / binding / 类型）推出一个唯一 feature key ⇒ 池里 get-or-create 到**恰好一个**"
        "缓存的 set 布局。")
D.Field("std::vector<FRDGDescriptorSet> Sets", "各 set 的绑定声明")
D.Field("std::vector<FRHIPushConstantRange> PushConstants", "push constant 区间（必须与着色器约定一致）")
D.Interface("FRDGDescriptorSet& AddSet(std::uint32_t SetIndex)", "追加一个 set 声明并返回它（填绑定用）")
D.Interface("FRDGBinding& Bind(std::uint32_t SetIndex, std::uint32_t Binding, ERHIDescriptorType Type)",
            "在指定 set 里加绑定；set 不存在就先建。返回可填的绑定槽")
D.Interface("std::int32_t AddSampler(std::uint32_t SetIndex, FRHISampler* Sampler)",
            "往指定 set 加采样器，返回下标")
D.Interface("FRDGBinding& Bind(std::uint32_t SetIndex, std::uint32_t Binding, ERHIDescriptorType Type, "
            "FRHISampler* Sampler)",
            "便捷式：加绑定 + 加采样器 + 接上 `SamplerIndex`（一步完成 CombinedImageSampler 的常规写法）")
D.Interface("void AddPushConstants(const FRHIPushConstantRange& R)", "追加 push constant 区间")

D.Struct("FRenderPassDesc", Desc="**一个渲染 pass 的完整声明**，作为单个单位交给 `FRender::AddPass`："
        "输入（`FPassParameter`：set + push constant）与输出（`FRenderTarget`：附件 + 尺寸）并排。"
        "输入与输出方向相反（读 vs 写），所以并排而不合并。")
D.Field("FPassParameter Layout", "输入：描述符 set / push constant（feature key 对应池里唯一布局）")
D.Field("FRenderTarget Target", "输出：附件 + 尺寸（渲染到哪）")

# ══════════════════════════════════════════════════════════════════════════════
# Public/RenderDrawList.h —— 绘制协议（批次 + 绘制列表）
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/RenderDrawList.h", Title="RenderDrawList.h —— 绘制协议（批次 + 绘制列表）",
         Desc="**声明式绘制**：生产者（场景系统、UI 特性）只填数据（几何切片 + 每批次绑定 + 裁剪矩形 + "
              "push constant），`FRender::AddPass` 负责绑定与录制 —— 特性手里没有顶点缓冲、"
              "描述符集、裁剪矩形或绘制命令。\n"
              "几何来源有优先级（**第一个非空者胜**），这条优先级就是这套协议的全部灵活性：\n"
              "1. **pass 级 GPU 顶点缓冲**（`FDrawList::SetVertexBuffer`）：批次只是它的一段切片 —— "
              "ImGui 路径就是这样（每帧合并成一个顶点 / 索引数组，每个绘制命令切一段）；\n"
              "2. **批次自带**的 `FRDGBufferRef`：生产者已经握有 GPU 缓冲（场景三角形路径）；\n"
              "3. 两者都空 ⇒ **着色器用 `gl_VertexIndex` 现场生成图元**（录成 `Draw(VertexCount)`，不绑顶点缓冲）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("RenderApi.h", "`MAHO_RENDER_API`")
D.Row("RDG.h", "`FRDGBufferRef`（几何来源）/ `FRDGDescriptorSet`（每批次绑定）")
D.Row("cstddef / cstdint / vector", "`std::uint8_t` 字节缓冲 / 定宽整数 / 容器")

D.Struct("FDrawBatch", Desc="一个网格批次 —— **绘制协议的单位**：描述一次绘制调用（AddPass 消费它）。"
        "AddPass 不知道谁产生的批次（今天是 `FScene` 硬编码的三角形，将来是真正的场景渲染器）。")
D.Field("FRDGBufferRef VertexBuffer", "批次自带的顶点缓冲；空 ⇒ 用 pass 级缓冲或着色器生成")
D.Field("std::uint32_t VertexOffset = 0", "顶点起始偏移（在**生效的那个**缓冲里）")
D.Field("std::uint32_t VertexCount = 3", "顶点数（默认 3 = 一个三角形）")
D.Field("std::uint32_t InstanceCount = 1", "实例数")
D.Field("FRDGBufferRef IndexBuffer", "索引缓冲；空 ⇒ 非索引绘制")
D.Field("std::uint32_t IndexOffset = 0", "索引起始偏移")
D.Field("std::uint32_t IndexCount = 0", "索引数")
D.Field("bool bIndex32 = true", "索引位宽（与缓冲内容必须一致）")
D.Field("bool bHasScissor = false", "是否带裁剪矩形；false ⇒ 用 pass 的整个目标矩形")
D.Field("std::int32_t ScissorX = 0", "裁剪矩形 X")
D.Field("std::int32_t ScissorY = 0", "裁剪矩形 Y")
D.Field("std::uint32_t ScissorW = 0", "裁剪矩形宽")
D.Field("std::uint32_t ScissorH = 0", "裁剪矩形高")
D.Field("std::vector<FRDGDescriptorSet> Sets",
        "**仅本批次**覆盖 pass 级默认 set 的值。每条绑定引用 RDG 资源，AddPass 按**内容**"
        "（content-addressable get-or-create）解析 —— 所以 ImGui 里换纹理的绘制命令"
        "（每个 `ImDrawCmd` 有自己的 `ImTextureID`）自然换到另一个 set-0 CombinedImageSampler，"
        "而相同资源的重复批次复用同一个池化 set")

D.Class("FDrawList", Desc="**绘制列表（协议）**：一个子 pass 的全部网格批次 + 可选的 pass 级图元缓冲。"
        "一个 `AddPass` 对应一个子 pass，所以一张绘制列表就是那个子 pass 的绘制集合 —— "
        "不需要子 pass 分组键。AddPass 消费它：需要时先上传 CPU 图元数据、解析并绑定每批次 set、"
        "再录绘制。")
D.SetAccess("public")
D.Interface("void Add(const FDrawBatch& Batch)", "追加一个批次（拷贝进列表）")
D.Interface("void Reset()",
            "清空所有字段（GPU 缓冲引用 / push constant / 批次），**保留批次向量的容量**。"
            "用于「成员列表每帧重填」的场景，避免上一帧批次累积。"
            "**注意它只丢引用，不释放 GPU 缓冲**（那些是池的 transient 资源）")
D.Interface("[[nodiscard]] const std::vector<FDrawBatch>& GetBatches() const", "批次列表（只读）")
D.Interface("void SetPushConstants(ERHIShaderStage InStages, std::uint32_t InSize, const void* InData)",
            "设置 pass 级 push constant（如 ImGui 的正交投影 mat4）。**数据被拷贝进来** ⇒ "
            "生产者的缓冲不需要有生命期；AddPass 在批次之前录一次 `PushConstants(stages, 0, size, data)`")
D.Interface("[[nodiscard]] bool HasPushConstants() const", "是否有非空 push constant")
D.Interface("[[nodiscard]] ERHIShaderStage GetPushConstantStages() const", "push constant 的可见阶段")
D.Interface("[[nodiscard]] std::uint32_t GetPushConstantSize() const", "push constant 字节数")
D.Interface("[[nodiscard]] const void* GetPushConstantData() const", "push constant 数据指针")
D.Interface("void SetVertexBuffer(const FRDGBufferRef& InVertexBuffer)",
            "设置 pass 级合并顶点缓冲（**在 InitViews 上传，不在 AddPass**），批次按 "
            "`VertexOffset` / `IndexOffset` 切段。设置后批次必须留空自己的缓冲（优先级 1）")
D.Interface("void SetIndexBuffer(const FRDGBufferRef& InIndexBuffer)", "设置 pass 级合并索引缓冲")
D.Interface("[[nodiscard]] bool HasPrimitiveData() const", "是否有 pass 级图元数据（顶点缓冲有效）")
D.Interface("[[nodiscard]] const FRDGBufferRef& GetVertexBuffer() const", "pass 级顶点缓冲引用")
D.Interface("[[nodiscard]] const FRDGBufferRef& GetIndexBuffer() const", "pass 级索引缓冲引用")
D.SetAccess("private")
D.Field("FRDGBufferRef VertexBuffer", "pass 级合并顶点缓冲（transient，InitViews 时上传）")
D.Field("FRDGBufferRef IndexBuffer", "pass 级合并索引缓冲（transient）")
D.Field("ERHIShaderStage PushStages = ERHIShaderStage::Vertex", "push constant 可见阶段（默认顶点）")
D.Field("std::uint32_t PushSize = 0", "push constant 字节数")
D.Field("std::vector<std::uint8_t> PushData", "push constant 的数据副本（所以生产者缓冲无需存活）")
D.Field("std::vector<FDrawBatch> Batches", "本子 pass 的批次")

# ══════════════════════════════════════════════════════════════════════════════
# Public/ShaderCompiler.h —— 异步 GLSL → SPIR-V 编译服务器
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/ShaderCompiler.h", Title="ShaderCompiler.h —— 异步着色器编译服务器",
         Desc="着色器编译单独跑在**自己的常驻线程**上（`FThreadedServer`）。为什么不是一个普通函数："
              "glslang 编译一个阶段是几十毫秒级，放在主线程就是每帧卡顿，放在渲染线程就是"
              "每帧的 GPU 提交被拖住 —— 所以它必须有自己的线程与队列，并且是**异步 + 显式同步**"
              "（`CompileAsync` 提交，`FlushCompiles` 才是等待点）。\n"
              "内容指纹（`HashShaderWords`）是这套设计的关键一环：着色器模块是短命对象，"
              "所以 PSO 缓存只能按**字节码内容**做键 —— 同一份字节码在不同 pass 里编译出的两个模块，"
              "必须命中同一条缓存。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("RenderApi.h", "`MAHO_RENDER_API`")
D.Row("Core/ThreadedServer.h", "`FThreadedServer`：常驻线程 + FIFO 串行队列（本类的基类）")
D.Row("RHI/RHIEnums.h", "`ERHIShaderStage`（编译哪个阶段）")
D.Row("cstdint / functional / string / vector", "指纹的整数 / 完成回调 / 源码与错误串 / SPIR-V 词数组")

D.Struct("FShaderCompileResult", Desc="一次编译的结果：成功标志 + SPIR-V 词 + 错误日志。"
        "**失败不清空已收集的信息**：日志原样带出来，让上层能把 glslang 的报错直接显示")
D.Field("bool bSuccess = false", "是否成功（默认失败：没跑完也不当成功）")
D.Field("std::vector<std::uint32_t> Bytecode", "SPIR-V 词（成功时有效）")
D.Field("std::string ErrorLog", "编译错误日志（失败时有效）")

D.Card("辅助函数")
D.Table("签名", "说明")
D.Row("inline std::uint64_t HashShaderWords(const std::uint32_t* Words, std::size_t Count)",
      "着色器**内容指纹**：对 SPIR-V 词做 FNV-1a 64 位。**为什么按内容哈希：**着色器模块是短命的"
      "（跨 pass 会由同一份字节码重新建出不同的模块对象），能当稳定身份的只有内容本身 —— "
      "PSO 缓存正是拿 VS / FS 的这对哈希当键")

D.Struct("FShaderCompileDesc", Desc="一次编译请求：源码 + 阶段 + 入口。**源码是字符串**："
        "今天特性直接内嵌 GLSL 文本，将来可以换成资产系统给的源码。")
D.Field("std::string Source", "GLSL 源码")
D.Field("ERHIShaderStage Stage = ERHIShaderStage::Vertex", "目标阶段")
D.Field("std::string EntryPoint = \"main\"", "入口函数名")

D.Class("FShaderCompilerServer", Base="FThreadedServer",
        Desc="异步编译服务器（自己的线程）。`FRender` 持有它；`CompileAsync` 提交请求，"
             "**回调在调用方线程上触发**（服务器线程跑完编译后把回调投回调用线程的队列）。"
             "于是 GLSL 编译既不占主线程也不占渲染线程。")
D.SetAccess("public")
D.Interface("FShaderCompilerServer()", "构造：不启动线程（`Initialize` 才启动）")
D.Interface("~FShaderCompilerServer() override", "析构：停线程并排空队列（幂等）")
D.Interface("bool Initialize()", "启动编译线程（幂等）。**glslang 缺失时返回 false** —— "
                                "由调用方决定降级策略（例如连特性一起关掉），而不是让编译在后台反复失败")
D.Interface("void CompileAsync(const FShaderCompileDesc& Desc, "
            "std::function<void(const FShaderCompileResult&)> OnDone)",
            "提交一次异步编译；`OnDone` 在**调用线程**上收到结果。同一阶段被多次请求时，"
            "上层（`TShaderHandle`）用状态位合并成一次提交，避免重复编译")
D.Interface("void FlushCompiles()",
            "屏障：阻塞到此前提交的全部编译完成。这是**显式同步点** —— 特性在用它之前调它"
            "（`TShaderHandle::Wait()` 就是这条）")
D.Interface("static FShaderCompileResult CompileStage(const FShaderCompileDesc& Desc)",
            "同步编译（**在当前线程上跑**，不进服务器）—— 启动期 / 工具路径用；"
            "渲染路径绝不该调它")
D.SetAccess("private")
D.Interface("void ProcessCompileJob(const FShaderCompileDesc& Desc, "
            "std::function<void(const FShaderCompileResult&)> OnDone)",
            "服务器线程上的实际编译体（glslang 调用 + 结果投回）")

# ══════════════════════════════════════════════════════════════════════════════
# Public/ShaderParameterStruct.h —— 编译期着色器参数结构（UE 宏族移植）
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/ShaderParameterStruct.h", Title="ShaderParameterStruct.h —— 编译期着色器参数结构",
         Desc="UE 的 `SHADER_PARAMETER_STRUCT` 宏族的忠实移植。**一次声明就是唯一事实来源**："
              "同一个 token 既声明真实的字段，又追加该成员的编译期元数据，所以"
              "**声明顺序 == 元数据顺序**。\n"
              "与 UE 的关键差别：UE 在运行期从着色器反射（`FShaderParameterBindings`）解出描述符槽位，"
              "而 Maho **没有反射钩子**（GLSL → SPIR-V，没有资源名内省）。所以 set / binding / stages "
              "被**烧进宏里**，由 codegen 用 Unity 着色器的反射结果替换那些常量。\n"
              "成员收集机制是 UE 的「成员 id 类型链 + 追加回溯」：预处理器搭出一条开放的 typedef 链"
              "（不需要外部 codegen）。每个 `SHADER_PARAMETER_*` 补完前一个链节点、声明字段、"
              "声明下一个链节点，并重载一个 `zzAppendMemberGetPrev` —— 它压入一条 `FShaderParameterMember` "
              "并返回**前一个**成员的追加函数指针。`END_SHADER_PARAMETER_STRUCT` 从最后一个成员回溯到 "
              "`zzFirstMemberId`（返回 nullptr），再反转以恢复声明顺序。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("RDG.h", "`FRDGTextureRef` / `FRDGBufferRef`（资源成员）/ `FPassParameter`（构建目标）")
D.Row("RHI/RHIEnums.h", "描述符类型 / 阶段枚举")
D.Row("RHI/RHIResources.h", "`FRHISampler*`（CombinedImageSampler 的采样器半边）")
D.Row("algorithm / cstddef / cstdint / vector", "`std::reverse`（回溯后恢复顺序）/ `offsetof` / 定宽整数 / 元数据容器")

D.Card("对齐常量（移植自 UE）")
D.Table("常量", "说明")
D.Row("SHADER_PARAMETER_STRUCT_ALIGNMENT = 16",
      "整个参数结构的对齐（对应 UE 的 `SHADER_PARAMETER_STRUCT_ALIGNMENT`）")
D.Row("SHADER_PARAMETER_ARRAY_ELEMENT_ALIGNMENT = 16", "数组元素对齐（配合 `SHADER_PARAMETER_ARRAY`）")
D.Row("SHADER_PARAMETER_POINTER_ALIGNMENT = sizeof(std::uint64_t)",
      "资源成员（引用 / 指针）的对齐 —— 按指针宽度，和 GPU 侧描述符句柄一致")

D.Card("EShaderParameterType", "成员的基础类型：常量（存进常量块 / push constant）还是资源（描述符）。")
D.Table("枚举值", "说明")
D.Row("Invalid = 0", "不支持的类型（主模板的默认值 ⇒ 未特化的类型会静态断言失败）")
D.Row("Float", "浮点常量 → push constant")
D.Row("UInt32", "无符号整数常量 → push constant")
D.Row("Int32", "有符号整数常量 → push constant")
D.Row("Texture", "`FRDGTextureRef` → SampledImage / CombinedImageSampler 描述符")
D.Row("Buffer", "`FRDGBufferRef` → StorageBuffer 描述符")

D.Struct("FShaderParameterMember", Desc="**一个成员**的编译期元数据：结构布局 + 它绑定的描述符槽位"
        "（由 codegen 烧进来）。set / binding / stages 与布局数据放在一起，正是因为 Maho 没有运行期"
        "反射可以再去把它们捡回来。")
D.Field("const char* Name = nullptr", "字段名（字面量）")
D.Field("const char* ShaderType = nullptr", "着色器侧类型串（如 `Texture2D`）")
D.Field("std::uint32_t Offset = 0", "字段在参数结构里的字节偏移")
D.Field("std::uint32_t Size = 0", "字段字节大小")
D.Field("EShaderParameterType Type = EShaderParameterType::Invalid", "基础类型")
D.Field("ERHIDescriptorType DescriptorType = ERHIDescriptorType::Sampler", "描述符类型（资源成员用）")
D.Field("std::uint32_t Set = 0", "描述符 set 号")
D.Field("std::uint32_t Binding = 0", "描述符 binding 号")
D.Field("ERHIShaderStage Stages = ERHIShaderStage::None", "可见阶段")
D.Field("bool bIsResource = false", "true = 描述符绑定，false = 常量（进 push constant 块）")
D.Field("std::uint32_t SamplerOffset = 0", "配套采样器字段的偏移（CombinedImageSampler 才有，否则 0）")

D.Struct("FShaderParameterStructMetadata", Desc="**一个参数结构**的元数据（由 BEGIN / END 宏定义出的 "
        "`FTypeInfo` 惰性构造）。`Members` 的顺序就是声明顺序 —— 常量块正是按它拼出来的。")
D.Field("const char* StructTypeName = nullptr", "结构类型名（字面量，用作缓存键的一部分）")
D.Field("std::uint32_t Size = 0", "整个结构的字节大小（`sizeof`）")
D.Field("std::vector<FShaderParameterMember> Members", "成员元数据（声明顺序）")

D.Struct("TAlignedTypedef<T, Alignment>", Desc="**对齐类型别名**小工具：`using Type = T alignas(Alignment);`。"
        "**为什么需要它：**在 MSVC / clang 上把 `alignas` 写在类型上并不像写在变量上那样生效，"
        "所以用「产生一个对齐的类型别名」这个 UE 技巧绕过去。")
D.Field("using Type = T alignas(Alignment)", "对齐后的类型（字段声明直接用它）")

D.Struct("TShaderParameterTypeInfo<T>", Desc="常量（原生）成员的类型信息：基础类型 + 对齐驱动字段声明与元数据；"
        "`bIsStoredInConstantBuffer` 区分常量与资源。**主模板刻意保持「无效」**，"
        "而不是悄悄生成垃圾元数据 —— 不支持的类型会撞上 `INTERNAL_SHADER_PARAMETER_CONST` 里的 "
        "`static_assert`，报错点正好落在写错的那一行。")
D.Field("static constexpr EShaderParameterType BaseType = Invalid", "基础类型（主模板无效）")
D.Field("static constexpr ERHIDescriptorType DescriptorType = UniformBuffer", "描述符类型（常量恒为 UBO）")
D.Field("static constexpr bool bIsStoredInConstantBuffer = false", "是否常量成员（主模板为假 ⇒ 会被断言拦下）")
D.Field("static constexpr std::uint32_t Alignment = 1", "字段对齐")
D.Field("using TAlignedType = T", "对齐后的字段类型")

D.Card("TShaderParameterTypeInfo 的可用特化", "只有这三个原生类型被特化（都是 4 字节对齐、"
                                             "进常量块）。要加类型就在这里加特化 —— 主模板会拦住漏掉的写法。")
D.Table("特化", "说明")
D.Row("TShaderParameterTypeInfo<float>", "`BaseType = Float`、`Alignment = 4`、常量成员")
D.Row("TShaderParameterTypeInfo<std::uint32_t>", "`BaseType = UInt32`、`Alignment = 4`、常量成员")
D.Row("TShaderParameterTypeInfo<std::int32_t>", "`BaseType = Int32`、`Alignment = 4`、常量成员")

D.Card("内部宏（宏族骨架）", "这些是 `INTERNAL_` 前缀的实现宏，宏族真正的机制都在这里。"
                            "用户不应直接用它们 —— 用下面的公开宏族。")
D.Table("宏", "说明")
D.Row("INTERNAL_SHADER_PARAMETER_GET_STRUCT_METADATA(StructTypeName)",
      "惰性构造元数据：函数内一个 `static const`，只算一次；`#StructTypeName` 把类型名变成字面量")
D.Row("INTERNAL_SHADER_PARAMETER_STRUCT_BEGIN(StructTypeName, DllStorage)",
      "开结构：加 `alignas` + 公开构造 + 嵌套 `FTypeInfo`（`GetSize` / `GetStructMetadata`）+ "
      "链头 `zzFirstMemberId`（返回 nullptr 的终点）+ 首个 `typedef zzFirstMemberId`")
D.Row("INTERNAL_SHADER_PARAMETER_APPEND_MEMBER(...)",
      "追加成员的公共体：定义下个链节点 → 重载 `zzAppendMemberGetPrev` → 对齐 `static_assert` → "
      "压入一条 `FShaderParameterMember` → 返回**前一个**成员的追加函数指针 → `typedef` 下个链节点")
D.Row("INTERNAL_SHADER_PARAMETER_CONST(TypeInfo, MemberName)",
      "常量成员：`alignas` 声明字段 + 两个 `static_assert`（类型受支持、确实是常量）+ 追加")
D.Row("INTERNAL_SHADER_PARAMETER_CONST_ARRAY(TypeInfo, MemberName, NumElements)",
      "常量数组成员：声明 `Member[N]`，元数据的 size 覆盖整个数组（对应一个 push constant 区间）")
D.Row("INTERNAL_SHADER_PARAMETER_RESOURCE(Type, FieldType, ShaderType, MemberName, Set, Binding, Stages, "
      "DescriptorType)",
      "资源成员：字段按**指针对齐**声明，元数据把 set / binding / stages / 描述符类型一起写进去")
D.Row("INTERNAL_SHADER_PARAMETER_TEXTURE_SAMPLER(ShaderType, TextureMember, SamplerMember, Set, Binding, "
      "Stages)",
      "CombinedImageSampler 成员：声明纹理字段 + 采样器字段两个，元数据记下采样器偏移，"
      "`AddPass` 据此写成一个合并描述符")
D.Row("END_SHADER_PARAMETER_STRUCT()",
      "收尾：补完链 + 定义 `zzGetMembers`（UE 式回溯：从最后成员的追加函数一路回到基类，"
      "每步返回前一个，最后 `std::reverse` 恢复声明顺序）")

D.Macro("BEGIN_SHADER_PARAMETER_STRUCT(StructTypeName)", "结构开始",
       "开始一个编译期着色器参数结构（`alignas(16)` + `FTypeInfo` 元数据入口）。")
D.Macro("SHADER_PARAMETER(MemberType, MemberName)", "常量成员",
       "加一个常量成员 ⇒ 进 push constant 块（阶段默认 `AllGraphics`）。"
       "**为什么常量与资源要分开的宏：**两者在元数据里走不同的字段路径"
       "（`bIsStoredInConstantBuffer` 为真 / 假），合一个宏会让宏体必须做编译期分支。")
D.Macro("SHADER_PARAMETER_ARRAY(MemberType, MemberName, NumElements)", "常量数组成员",
       "加一个定长常量数组 ⇒ 整个数组对应一个 push constant 区间（NumElements 是整数字面量）。")
D.Macro("SHADER_PARAMETER_TEXTURE(ShaderType, MemberName, Set, Binding, Stages)", "采样纹理",
       "加一个纹理成员 ⇒ SampledImage 描述符。`ShaderType` 是渲染侧类型串（如 `Texture2D`），"
       "用于与文件里的声明互证。")
D.Macro("SHADER_PARAMETER_BUFFER(ShaderType, MemberName, Set, Binding, Stages, DescriptorType)", "存储缓冲",
       "加一个缓冲成员 ⇒ StorageBuffer 描述符（`DescriptorType` 允许显式指定，"
       "以便将来切到 Dynamic / AccelerationStructure 变体）。")
D.Macro("SHADER_PARAMETER_TEXTURE_SAMPLER(ShaderType, TextureMember, SamplerMember, Set, Binding, Stages)",
       "纹理 + 采样器",
       "加一个 CombinedImageSampler：**一条绑定同时声明纹理字段与采样器字段**，"
       "元数据把两者的偏移配起来，`AddPass` 写成一个合并描述符。")
D.Macro("END_SHADER_PARAMETER_STRUCT()", "结构结束",
       "结束结构：补完链并定义 `zzGetMembers`（回溯 + 反转得到声明顺序的成员表）。")

D.Struct("TScalarResourceTypeInfo", Desc="资源成员的哨兵 TypeInfo：它的字段只在 "
        "`HasDeclaredResource` 表达式折叠里用到（资源元数据由 `INTERNAL_SHADER_PARAMETER_RESOURCE` "
        "显式写），**必须留在全局作用域**以便宏里的 `::Maho::` 限定能命中它。")
D.Field("static constexpr bool bIsStoredInConstantBuffer = false", "资源成员不是常量")

D.Struct("FShaderParameterBuildResult", Desc="把编译期 `TParameters` 翻译成运行期 `FPassParameter` 的产物："
        "资源成员变成 set 绑定；常量成员按声明顺序拼成**一个连续 push constant 块**"
        "（Maho 没有反射钩子，所以块直接从结构字段拷出来）。push 数据与阶段一并返回，"
        "让 `AddPass` 能在绘制前绑定它。")
D.Field("FPassParameter Layout", "翻译出的 pass 输入布局（set + push constant 区间）")
D.Field("std::vector<std::byte> PushConstantData", "常量成员拼成的 push constant 字节块")
D.Field("ERHIShaderStage PushConstantStages = ERHIShaderStage::None", "push constant 的阶段")
D.Field("bool bHasPushConstant = false", "是否产生了 push constant（没有常量成员就是假）")

D.Card("自由函数")
D.Table("签名", "说明")
D.Row("template <typename TParameters> FShaderParameterBuildResult ShaderParameterBuild("
      "const TParameters& Parameters)",
      "把宏声明的 `TParameters` 翻成 `AddPass` 消费的运行期布局：读编译期元数据"
      "（声明顺序 == 元数据顺序），按偏移把每个成员的**值**拷出来 —— "
      "资源成员 → `Layout.Bind(...)`，常量成员 → 追加进 push constant 块。"
      "**这是声明式参数与运行期描述符之间唯一的桥**")

# ══════════════════════════════════════════════════════════════════════════════
# Public/Render.h —— 渲染层的 stage 能力面 + FRender + 着色器句柄
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Render.h", Title="Render.h —— 渲染 stage 能力面 + FRender",
         Desc="渲染层对特性的**全部词汇**：11 个 stage 接口 + 帧类型 `FRender` + 着色器句柄 "
              "`TShaderHandle`。\n"
              "**FRender 是两层身份**：对宿主引擎它是一个 layer（装载 `IInit` / `ITick` / `IExit` 等"
              "引擎 stage）；对渲染特性它是**自己的收集器**（特性实现 `IBeginRender` / `IRender` / "
              "`IEndRender` / `IPresent`，全部装在 FRender 里并由它调度）。**专属线程在它拥有的部件内部**："
              "RHI（`FRHI`）是渲染服务器、着色器编译器是另一个 `FThreadedServer` —— "
              "`FRender` 自己**不是**服务器。宿主只把 FRender 看作一个 layer。\n"
              "**FRender 同时就是 RDG 资源池的门面**：特性经 `CreateTexture` / `CreateBuffer` 拿 "
              "`FRDG*Ref`，原生对象活在池里跨帧复用；交换链后缓冲**只被帧特性碰**"
              "（经 `RHI->PresentTexture`）。\n"
              "**stage 序列为什么是这个顺序**（`FRenderStages`）：`IInitViews` 先建视图与上传几何"
              "（后续 pass 的输入），`IBeginRender` 做帧首准备（列表 / 常量），`IRender` 才是 pass，"
              "`IEndRender` 收尾，`IPostProcess` 做全屏后处理，`IRenderUI` 合成 UI，`IPresent` "
              "最后 blit 上屏。编辑器构建额外插 `IEditorInput`（最前，抢输入）与 `IEditorCompose`"
              "（UI 之后、present 之前，把编辑器覆盖层画在最上面）。"
              "**一个没被任何已装特性实现的 stage 完全不产生节点**（没有空节点）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("RenderApi.h", "`MAHO_RENDER_API`")
D.Row("Maho.h / Engine/Frame.h / Engine/FrameBuilder.h / Engine/Engine.h",
      "帧系统：`FFrameExtension` / `IPipeline` / `FFrameBuilder` / 引擎 stage 接口")
D.Row("RHI/RHIServer.h", "`IRHI` / `FRHI`：帧特性经 FRender 用它们（特性拿不到裸 `IRHI*`）")
D.Row("RDG.h / RenderDrawList.h", "RDG 引用与绘制协议（AddPass 的参数）")
D.Row("ShaderCompiler.h / ShaderParameterStruct.h", "异步编译服务器 / 编译期参数宏族")
D.Row("Resource.h / AssetTypes.h", "资源系统的资产事件与纹理描述（CPU → GPU 镜像）")
D.Row("algorithm / cstdint / functional / memory / mutex / new / string / unordered_map / variant / vector",
      "容器与工具：`std::function` 的 pass 回调 / `std::mutex`（pass 提交串行）/ "
      "`unordered_map`（镜像表）/ `new`（placement-new 参数）")

D.Card("自由函数（全局）")
D.Table("签名", "说明")
D.Row("MAHO_RENDER_API FRender* GetRender()",
      "渲染实例的全局访问器（**经函数跨 DLL**，不导出裸变量）。渲染层初始化时设置；"
      "别的层（如 ImGui 层）靠它触达 RHI。返回空 = 渲染层还没起来")

D.Card("Detail:: 内部辅助函数（模板）", "解析着色器类型**可选的**静态入口点："
                                       "有就取，没有就用 `main`。`if constexpr (requires ...)` 让"
                                       "「没声明入口」成为一个合法写法。")
D.Table("签名", "说明")
D.Row("template <typename T> const char* GetVertexEntryPoint()",
      "若 `T::GetVertexEntryPoint()` 存在就返回它，否则 `\"main\"`")
D.Row("template <typename T> const char* GetFragmentEntryPoint()",
      "若 `T::GetFragmentEntryPoint()` 存在就返回它，否则 `\"main\"`")

D.Class("IOnInstalled", Desc="**已安装**回调：特性被装进 FRender 后立刻收到一次，用来做「安装后登记」"
        "（例如把自己的名字登记给别的特性）。与 `IInitViews` 分开是因为「装进来了」与"
        "「开始建视图」是两个时刻：前者只跑一次，后者每帧的准备阶段都要跑。")
D.SetAccess("public")
D.Interface("virtual ~IOnInstalled() = default", "虚析构（卸载经基类指针）")
D.Interface("virtual void OnInstalled(FRender&) = 0", "装载完成回调（只一次）")

D.Class("IInitViews", Desc="**建视图 / 准备输入**阶段：帧首建立本帧要用的视图与 GPU 输入"
        "（上传几何、解析镜像、准备常量）。**必须在所有绘制 pass 之前**，因为 `IRender` 的 pass "
        "会把这里建出来的东西当输入。")
D.SetAccess("public")
D.Interface("virtual ~IInitViews() = default", "虚析构")
D.Interface("virtual void InitViews(FRender&) = 0", "每帧的准备阶段（不录绘制）")

D.Class("IBeginRender", Desc="**帧首**阶段：每帧渲染开始前的准备（拿列表、清状态、算本帧的常量）。"
        "**每个渲染特性的 `IBeginRender` 都必须依赖帧特性的 `IFrameBegin`**，否则它的列表获取会与"
        " `ReleaseFrameLists` 抢同一批列表（实测会撞出 `vkFreeCommandBuffers is in use`）。")
D.SetAccess("public")
D.Interface("virtual ~IBeginRender() = default", "虚析构")
D.Interface("virtual void BeginRender(FRender&) = 0", "帧首准备（不录 pass）")

D.Class("IRender", Desc="**绘制的 pass 阶段**：真正录绘制的地方（`AddPass` 在这里调）。"
        "pass 之间的顺序 = 特性调用 `AddPass` 的顺序 —— **没有隐式排序**，"
        "需要顺序就用 stage 依赖显式声明。")
D.SetAccess("public")
D.Interface("virtual ~IRender() = default", "虚析构")
D.Interface("virtual void Render(FRender&) = 0", "录制本特性的 pass")

D.Class("IEndRender", Desc="**绘制收尾**阶段：pass 都录完后的清理 / 汇总（如统计、还原状态）。")
D.SetAccess("public")
D.Interface("virtual ~IEndRender() = default", "虚析构")
D.Interface("virtual void EndRender(FRender&) = 0", "绘制收尾")

D.Class("IPostProcess", Desc="**后处理**阶段：在全屏后处理意义上晚于 `IRender`、早于 UI。"
        "放在 UI 之前是因为 UI 通常不再希望被色调映射 / 泛光二次处理。")
D.SetAccess("public")
D.Interface("virtual ~IPostProcess() = default", "虚析构")
D.Interface("virtual void PostProcess(FRender&) = 0", "后处理 pass")

D.Class("IRenderUI", Desc="**UI 合成**阶段：把 UI（ImGui 等）画到自己的离屏目标上。"
        "合成完的目标通常会用 `SetPresentTarget` 声明为本帧最终上屏目标。")
D.SetAccess("public")
D.Interface("virtual ~IRenderUI() = default", "虚析构")
D.Interface("virtual void RenderUI(FRender&) = 0", "录制 UI 绘制")

D.Class("IEditorInput", Desc="（**仅编辑器构建**）Pass0 输入接管：在编辑器帧里跑在**最前**，"
        "早于游戏 UI 特性的 `IInitViews` 喂 IO / `NewFrame`。编辑器特性实现这个 stage 来抢先尝"
        "Win32 输入、把它重基到视口面板矩形上、再喂给**游戏 UI 上下文**的 IO，"
        "于是游戏 UI 只在视口面板内响应，布局也与显示出来的（面板缩放过的）屏幕表面一致。"
        "声明在 `MAHO_EDITOR_BUILD` 下：没有编辑器特性时这个 stage 没有实现者，被静默跳过（无回归）。")
D.SetAccess("public")
D.Interface("virtual ~IEditorInput() = default", "虚析构")
D.Interface("virtual void EditorInput(FRender&) = 0", "掠取并重基输入到视口面板")

D.Class("IEditorCompose", Desc="（**仅编辑器构建**）Pass3 编辑器最终合成面：跑在游戏 UI（`IRenderUI`）"
        "合成到自己的离屏目标**之后**、`IPresent` 上屏**之前**。编辑器特性实现它来采样游戏 UI 合成结果、"
        "把编辑器覆盖层画在上面、并把**那个**目标设为本帧的上屏目标。原先是编辑器专属 stage，"
        "所以只在 `MAHO_EDITOR_BUILD` 下被选进渲染图；没有编辑器特性时静默跳过，"
        "于是游戏 UI 的目标仍然是上屏目标（无回归）。")
D.SetAccess("public")
D.Interface("virtual ~IEditorCompose() = default", "虚析构")
D.Interface("virtual void EditorCompose(FRender&) = 0", "合成编辑器覆盖层并设定上屏目标")

D.Class("IPresent", Desc="**上屏**阶段：把本帧的最终目标 blit 到交换链后缓冲（`PresentTexture`）。"
        "**必须是最后一个**（在它之前没人能改上屏目标了），也必须是唯一碰交换链的地方。")
D.SetAccess("public")
D.Interface("virtual ~IPresent() = default", "虚析构")
D.Interface("virtual void Present(FRender&) = 0", "blit 最终目标到交换链")

D.Class("IPreUnInstall", Desc="**卸载前**阶段：特性被卸载之前收到通知（释放它持有的池句柄 / 反注册）。"
        "放在卸载流程最前，因为卸载之后它的资源引用就不该再被用。")
D.SetAccess("public")
D.Interface("virtual ~IPreUnInstall() = default", "虚析构")
D.Interface("virtual void PreUnInstall(FRender&) = 0", "卸载前清理")

D.Card("stage 分派声明（MAHO_DECLARE_STAGE_DISPATCH）",
       "每个 stage 接口都要在编译期注册一次「接口类型 → FRender 上的方法」的映射，"
       "桥（`FFrameBridge` / `TFrameDispatch`）才能把「这个特性实现该 stage 吗」变成一个"
       "可查询的闭包。缺失一条声明，对应 stage 就不会被调度（特性静默不跑）。")
D.Table("宏调用", "说明")
D.Row("MAHO_DECLARE_STAGE_DISPATCH(FRender, IOnInstalled, IOnInstalled, OnInstalled)", "装载后回调")
D.Row("MAHO_DECLARE_STAGE_DISPATCH(FRender, IInitViews, IInitViews, InitViews)", "建视图")
D.Row("MAHO_DECLARE_STAGE_DISPATCH(FRender, IBeginRender, IBeginRender, BeginRender)", "帧首准备")
D.Row("MAHO_DECLARE_STAGE_DISPATCH(FRender, IRender, IRender, Render)", "pass 录制")
D.Row("MAHO_DECLARE_STAGE_DISPATCH(FRender, IEndRender, IEndRender, EndRender)", "绘制收尾")
D.Row("MAHO_DECLARE_STAGE_DISPATCH(FRender, IPostProcess, IPostProcess, PostProcess)", "后处理")
D.Row("MAHO_DECLARE_STAGE_DISPATCH(FRender, IRenderUI, IRenderUI, RenderUI)", "UI 合成")
D.Row("MAHO_DECLARE_STAGE_DISPATCH(FRender, IEditorInput, IEditorInput, EditorInput)",
      "编辑器输入接管（仅 `MAHO_EDITOR_BUILD`）")
D.Row("MAHO_DECLARE_STAGE_DISPATCH(FRender, IEditorCompose, IEditorCompose, EditorCompose)",
      "编辑器最终合成（仅 `MAHO_EDITOR_BUILD`）")
D.Row("MAHO_DECLARE_STAGE_DISPATCH(FRender, IPresent, IPresent, Present)", "上屏")
D.Row("MAHO_DECLARE_STAGE_DISPATCH(FRender, IPreUnInstall, IPreUnInstall, PreUnInstall)", "卸载前清理")

D.Class("FRender", Base="FFrameExtension, IPipeline<IPreInit, IInit, IPostInit, IBeginFrame, ITick, "
                        "IEndFrame, IExit, IPreShutdown, IShutdown, IPostShutdown>, FFrameBuilder<FRender>",
        Desc="渲染子系统：宿主引擎里的一个层（装载 `IInit` / `ITick` / `IExit` 等），"
             "同时是渲染特性自己的层收集器。它也是 RDG 资源池的门面。\n"
             "**它自己不干活**：帧工作由 stage 序列（`FRenderStages`）驱动，"
             "`FRender::Tick` 只是把序列交给 `Execute<...>()`；交换链的帧生命期"
             "（取值 / 结束 + 呈现）挂在 `BeginFrame` / `EndFrame` 这两个**引擎 stage** 上，"
             "`EndFrame` 先 `Wait()` 图的栅栏再 `RHI->EndFrame()`，"
             "于是呈现一定等到所有提交完成。")
D.SetAccess("private")
D.Interface("FRender()", "构造私有：宿主经 `CreateFrame` 工厂装载（帧不允许随便 new）")
D.Interface("~FRender() override", "析构：`FFrameBuilder` 收摊（排空图与池）后才释放实例与模块")
D.SetAccess("public")
D.Interface("[[nodiscard]] const std::unordered_map<Name::FName, FRDGResourceRef>& GetMirrors() const",
            "CPU 资产 → GPU 镜像表（UI 特性遍历它，把每个纹理镜像画成一张图，"
            "并按需从池里重新解析 set-0 的 CombinedImageSampler）")
D.Interface("[[nodiscard]] const FRDGResourceRef* GetMirror(const Name::FName& AssetName) const",
            "查某个资产的镜像（没有返回 nullptr）—— 特性据此把资产接进自己的 pass")
D.Interface("[[nodiscard]] FRHISampler* GetMirrorSampler(const Name::FName& AssetName) const",
            "某个资产纹理的采样器镜像（按资产自己的 GPU 采样配置建），绘制时绑定时用；"
            "没有纹理镜像 / 没建采样器时返回 nullptr")
D.Interface("[[nodiscard]] std::uint32_t GetCanvasWidth() const",
            "画布宽 = 交换链几何（场景颜色目标按它建）")
D.Interface("[[nodiscard]] std::uint32_t GetCanvasHeight() const", "画布高")
D.Interface("[[nodiscard]] ERHIFormat GetSwapchainFormat() const", "交换链格式（离屏目标按它建）")
D.Interface("void PresentTexture(const FRDGTextureRef& Texture)",
            "把场景颜色的 RDG 纹理 blit 到交换链后缓冲（帧特性的上屏点）")
D.Interface("void SetPresentTarget(const FRDGTextureRef& Texture)",
            "设置本帧**最终**上屏目标。任何合成最后屏幕表面的 UI 特性在绘制后调用它，"
            "帧特性的 `IPresent` 再把它 blit 上屏。**最后写者胜**：运行时构建里游戏 UI 设 GameRT，"
            "编辑器构建里编辑器 UI 合成后设 EditorRT —— 于是编辑器表面才是被呈现的那个")
D.Interface("[[nodiscard]] FRDGTextureRef GetPresentTarget() const",
            "当前上屏目标（空 = 本帧不上屏）")
D.Interface("[[nodiscard]] FRDGTextureRef CreateTexture(const FRHITextureDesc& Desc, "
            "ERDGResourceLifetime Lifetime = ERDGResourceLifetime::Persistent)",
            "从池里要一块离屏纹理（默认 Persistent）。**特性从不自己建原生对象**")
D.Interface("[[nodiscard]] FRDGBufferRef CreateBuffer(const FRHIBufferDesc& Desc, "
            "ERDGResourceLifetime Lifetime = ERDGResourceLifetime::Persistent)",
            "从池里要一块 GPU 缓冲（默认 Persistent）")
D.Interface("void ReleaseTexture(FRDGTextureRef& Ref)", "归还纹理引用（计数减一；原生按生命期规则留着）")
D.Interface("void ReleaseBuffer(FRDGBufferRef& Ref)", "归还缓冲引用")
D.Interface("void AddPass(ERHICommandListType PassType, std::function<void(FRHICommandList&)> PassFn)",
            "**录一个 pass 并立刻提交**（`AcquireRenderList` + Begin/End + Submit 的语法糖）。"
            "lambda 拿到的命令列表已处于 pass 中：**只录绘制命令，不要自己 Begin / End / Submit**。"
            "pass 在 `AddPass` 调用点提交，所以它跑在特性的 `IRender` stage 上；"
            "pass 之间的顺序 == 调用 `AddPass` 的顺序，要用 stage 依赖保证「必须先提交的那个特性」在前面")
D.Interface("void AddPass(ERHICommandListType PassType, FRHIGraphicsPipelineDesc PipelineDesc, "
            "const FRenderPassDesc& Pass, const FDrawList& DrawList)",
            "**声明式绘制列表 pass**：消费一张 `FDrawList` 而不是录制 lambda。"
            "pass 级 GPU 顶点 / 索引缓冲已由生产者（`InitViews`，存成 `FRDGBufferRef`）创建并上传；"
            "AddPass 先绑 pass 级 set，再对每个批次：绑它的 per-batch set（内容可寻址 —— "
            "同一个资源的重复批次复用同一个池化 set）、绑几何、设裁剪、push 常量、绘制。"
            "**于是特性手里没有任何 RHI 对象**，只填 `FDrawList`")
D.Interface("template <typename TParameters> void AddPass(ERHICommandListType PassType, "
            "FRHIGraphicsPipelineDesc PipelineDesc, const FRenderTarget& Target, "
            "const TParameters* Parameters, std::function<void(FRHICommandList&)> PassFn)",
            "**编译期 FParameters 版 pass**：把宏声明的参数结构的编译期元数据翻成运行期 "
            "`FPassParameter`，建管线，并在绘制 lambda 之前绑好 push constant 块"
            "（值取自结构成员）。**为什么收指针：**参数是池里 placement-new 的帧内对象")
D.Interface("template <typename TParameters> void AddPass(ERHICommandListType PassType, "
            "FRHIGraphicsPipelineDesc PipelineDesc, const FRenderTarget& Target, "
            "const TParameters* Parameters, const FDrawList& DrawList)",
            "同上，但绘制来自绘制列表：布局同样按元数据建，push constant 数据取自 "
            "`FDrawList::SetPushConstants`")
D.Interface("template <typename TParameters> [[nodiscard]] TParameters* AllocParameters()",
            "从 FRender 的资源池分配一个宏声明的参数结构并 placement-new（对应 UE 的 "
            "`GraphBuilder.AllocateParameters<T>()`）：特性填完成员再交给 `AddPass`。"
            "**内存是帧内的**（下个 `BeginFrame` 回收）⇒ 必须在分配它的那一帧里消费掉，调用方永不释放。"
            "模板只转发到一个非模板桥（`AllocParameterBytes`），"
            "于是这个头（只前向声明了 `FRHIResourcePool`）**不会在不完整类型上实例化成员模板**"
            "（那会触发 MSVC 内部错误）")
D.Interface("[[nodiscard]] FRHIDescriptorSetLayout* GetOrCreateDescriptorSetLayout("
            "const FRHIDescriptorSetLayoutDesc& Desc)",
            "池拥有的 set 布局：按绑定结构做可寻址 get-or-create。特性拿到的是借用句柄，"
            "原生生命期归池（Shutdown 时销毁）")
D.Interface("[[nodiscard]] FRHISampler* CreateSampler(const FRHISamplerDesc& Desc)",
            "池拥有的采样器：按描述 get-or-create（相同描述共享一个原生）")
D.Interface("[[nodiscard]] FRHIDescriptorSet* GetOrCreateDescriptorSet("
            "FRHIDescriptorSetLayout* Layout, const FRHIDescriptorSetLayoutDesc& LayoutDesc, "
            "const FRHIDescriptorWrite* Writes, std::uint32_t WriteCount)",
            "池拥有的 set：按键（布局 + 引用的资源）可寻址 get-or-create。**内容在分配时就写好**"
            "（`IRHI::UpdateDescriptorSets`，设备级操作、不是录制的 `vkCmd`）⇒ "
            "特性只持句柄，池在 Shutdown 销毁池与 set")
D.Interface("[[nodiscard]] FRHIDescriptorSet* GetOrCreateMutableDescriptorSet("
            "FRHIDescriptorSetLayout* Layout, const FRHIDescriptorSetLayoutDesc& LayoutDesc)",
            "池拥有的**可变** set：仅按布局 get-or-create，**每个布局只分配一次**（分配时不写内容）。"
            "这是 Static / PerFrame / PerPass 参数 set 的**唯一实现路径** —— "
            "pass 在录制期用 `FRHICommandList::UpdateDescriptorSet` 重写内容。"
            "**所以 Static 也是可变的**，只是更新频率远低于 PerFrame / PerPass")
D.Interface("template <typename T> [[nodiscard]] TShaderHandle<T> TryGetShader()",
            "着色器资源：异步编译 + 显式同步句柄。**每个 T 的第一次调用**把 VS / FS 编译提交给"
            "着色器服务器线程（`CompileAsync`，离开渲染线程）并返回一个 `TShaderHandle<T>`；"
            "句柄的 `Wait()` 阻塞到那次编译完成 —— 特性通常在 `IBeginRender` 里 `TryGetShader` + "
            "`Wait()`，然后在 `IRender` 里对着就绪的模块发 pass。后续调用返回同一个缓存句柄（不重编）。"
            "**没有回退路径**：`Wait()` 之前 `GetVertex` / `GetFragment` 返回空模块 ⇒ 用之前必须 Wait。"
            "T 的契约（全静态）：`GetVertexSource()` / `GetFragmentSource()`（返回 nullptr = 无该阶段）+ "
            "可选的 `GetVertexEntryPoint()` / `GetFragmentEntryPoint()`")
D.Interface("void PreInitialize(FEngineBase&) override", "（引擎 stage `IPreInit`）最早的准备")
D.Interface("void Initialize(FEngineBase& Engine) override",
            "（引擎 stage `IInit`）起 RHI（有窗口才起）+ 着色器服务器 + 资源池，"
            "装载自己的渲染特性，并登记资产事件订阅")
D.Interface("void PostInitialize(FEngineBase& Engine) override", "（`IPostInit`）初始化收尾")
D.Interface("void PreShutdown(FEngineBase&) override", "（`IPreShutdown`）卸载特性前的准备")
D.Interface("void Shutdown(FEngineBase& Engine) override",
            "（`IShutdown`）卸订阅 → 排空异步工作 → 关设备 → 释放池。"
            "**先排空自己的异步工作**（着色器编译、录制任务的池）再拆，这是层与调度器的契约")
D.Interface("void PostShutdown(FEngineBase&) override", "（`IPostShutdown`）收尾")
D.Interface("void BeginFrame(FEngineBase& Engine) override",
            "（引擎 stage `IBeginFrame`）**交换链帧开始**：`RHI->BeginFrame()`（取值 + 开录），"
            "随后推进资源池（`BeginResourcePool`）。顺序不能反：资源回收依赖上一帧栅栏已等过")
D.Interface("void Tick(FEngineBase& Engine) override",
            "（引擎 stage `ITick`）**跑渲染图**：把 `FRenderStages` 交给 `Execute<...>()` "
            "（重查帧集 / 建批 / 提交）。FRender 自己不做帧工作")
D.Interface("void EndFrame(FEngineBase& Engine) override",
            "（引擎 stage `IEndFrame`）**先 `Wait()` 图的栅栏**再 `RHI->EndFrame()`（提交 + 呈现）—— "
            "于是呈现等到所有提交完成，不会有「帧已呈现但 pass 还在录」")
D.Interface("void RequestExit(FEngineBase& Engine) override", "（引擎 stage `IExit`）收到退出请求时的处理")
D.SetAccess("private")
D.Interface("template <typename T> friend class TShaderHandle",
            "`TShaderHandle` 靠这个友元驱动着色器编译：它能触达私有的 "
            "`GetOrCreateShaderModule` / `WaitShaderCompiles`")
D.Interface("void AddPass(ERHICommandListType PassType, FRHIGraphicsPipelineDesc PipelineDesc, "
            "const FRenderPassDesc& Pass, std::function<void(FRHICommandList&)> PassFn)",
            "**PSO 解析版 pass（内部实现点）**：解析管线（走池的 PSO 缓存）→ 开动态渲染 → "
            "**隐式绑定图形管线** → 跑绘制 lambda。特性既不查管线 / 布局，也不自己 "
            "`BindGraphicsPipeline`。公开的编译期 FParameters 模板最终转发到这里")
D.Interface("FShaderCompilerServer* GetShaderCompiler() const",
            "异步编译服务器（内部：特性只经 `TryGetShader<T>` 触达着色器）")
D.Interface("[[nodiscard]] void* AllocParameterBytes(std::size_t Size, std::size_t Align)",
            "`AllocParameters<T>` 的非模板桥：从（只在 .cpp 里完整的）池里 bump 分配帧内字节")
D.Interface("void BeginResourcePool()",
            "推进 RDG 资源池：过期 transient + 回收本帧的命令列表。由帧宿主的 `BeginFrame` "
            "在**交换链栅栏等待之后**调用（那些提交已完成，没有在飞的 GPU 引用）")
D.Interface("void WaitShaderCompiles()",
            "阻塞到此前异步编译完成（着色器服务器的静默屏障）。"
            "内部：特性经 `TShaderHandle::Wait()` 到达这里 —— 那就是「用之前先同步」的点")
D.Interface("[[nodiscard]] FRHIShaderModule* GetOrCreateShaderModule(const FRHIShaderModuleDesc& Desc)",
            "PSO 缓存：按描述 get-or-create 着色器模块。**身份按字节码内容匹配**，"
            "于是同一个 pass 跨帧共享一份已编译模块")
D.Interface("[[nodiscard]] FRHIPipelineLayout* GetOrCreatePipelineLayout("
            "const FRHIPipelineLayoutDesc& Desc)",
            "PSO 缓存：按描述 get-or-create 管线布局")
D.Interface("[[nodiscard]] FRHIGraphicsPipeline* GetOrCreateGraphicsPipeline("
            "const FRHIGraphicsPipelineDesc& Desc)",
            "PSO 缓存：按描述 get-or-create 栅格管线（描述里带字节码指纹，所以相同字节码的"
            "两个 pass 命中同一条缓存）。池拥有原生生命期，特性只持句柄")
D.Nested("FRenderStages", Kind="alias",
         Desc="本层的 stage 序列（`TTypeList<...>`）—— `Tick` 把它交给 `Execute<...>()`。"
              "编辑器构建多出 `IEditorInput` / `IEditorCompose` 两个 stage；"
              "**没被任何已装特性实现的 stage 完全不产生节点**（没有空节点）")
D.Field("std::unique_ptr<FRHI> RHI", "渲染服务器（**不是被调度的 stage**：引擎调度 stage，不调度服务器）")
D.Field("std::unique_ptr<FShaderCompilerServer> ShaderCompiler", "异步 GLSL → SPIR-V 编译服务器")
D.Field("std::unique_ptr<FRHIResourcePool> ResourcePool", "RDG 资源池（原生资源都在这里）")
D.Field("std::mutex PassSubmitMutex", "保护每 pass 提交栅栏表（图节点可能在线程池上并发录 pass）")
D.Field("std::vector<FRHIFence*> PendingPassFences",
        "**每 pass 提交串行化**的依据：`AddPass` 录完就立刻提交（无栅栏），"
        "而后一个 pass 会复用前一个**仍在飞**的提交读到的资源（它的可变 set、或一块 transient 缓冲），"
        "没有排序就会「重写 GPU 还在读的描述符集 / 释放还在读的缓冲」"
        "（VUID-vkUpdateDescriptorSets-None-03047 / VUID-vkDestroyBuffer-buffer-00922）。"
        "所以每次 per-pass 提交都带自己的栅栏，下一个 `AddPass` 先等掉此前所有栅栏再录")
D.Field("std::unordered_map<Name::FName, FRDGResourceRef> GpuMirrors",
        "资产名 → RDG 镜像资源（纹理或缓冲），由资源池拥有（Persistent 的一直活到池 Shutdown）。"
        "在资产导入回调里建、在卸载回调里释放。**纹理镜像的 UI 描述符句柄归 `FUIFeature`**，"
        "FRender 只拥有 GPU 镜像本身")
D.Field("FSubscriptionID AssetImportedSub = 0",
        "订阅资源系统「资产已导入」事件的**自己的**订阅 id —— **刻意保存 id 而不是 "
        "`RemoveAll()`**：后者会把别的插件注册在同一事件上的处理器一起摘掉")
D.Field("FSubscriptionID AssetUnloadedSub = 0", "订阅「资产已卸载」")
D.Field("FSubscriptionID AssetCreatedSub = 0", "订阅「资产已创建」")
D.Field("std::unordered_map<Name::FName, FRHISampler*> GpuSamplers",
        "资产名 → GPU 采样器镜像（按纹理的 GPU 采样配置建，经池 get-or-create 共享），"
        "`GetMirrorSampler` 解析它；卸载时擦除")
D.Field("FRDGTextureRef PresentTarget",
        "本帧最终上屏目标：由**最后合成的 UI 特性**（`RenderUI`）写，由帧特性的 `IPresent` 读 —— "
        "典型的跨特性状态")
D.Interface("void OnAssetMirrorImported(const Name::FName& AssetName, Resource::FOnTransferDone Done)",
            "「资产已导入」监听：把 CPU 资产镜像到 GPU（上传像素），然后经 `Done` 报告完成，"
            "好让资源系统丢掉 CPU 数据块")
D.Interface("void OnAssetMirrorUnloaded(const Name::FName& AssetName, Resource::FOnTransferDone Done)",
            "「资产已卸载」监听：释放 GPU 镜像并擦掉表项")
D.Interface("void OnAssetMirrorCreated(const Name::FName& AssetName, const Resource::FResource& Resource)",
            "「资产已创建」监听（资源系统 `CreateResource`）：按资源的描述字段建一个 Persistent GPU 镜像"
            "（**不传像素** —— 它是运行期占位），用资产名做键。特性经 `GetMirror(FName)` 解析。"
            "帧内（transient）GPU 资源不走这条路")
D.Interface("[[nodiscard]] bool ReadbackMirror(const Name::FName& AssetName, "
            "Resource::FResource& OutResource)",
            "GPU 回填（`SetReadback` 提供者）：导出前把 GPU 镜像解码回资源的 CPU 字段。"
            "没有镜像、或当前 RHI 没有 CPU 回读路径时返回 false")
D.Interface("[[nodiscard]] static ERHIFormat FormatMirror(Resource::ETexturePixelFormat Fmt, bool bSRGB)",
            "资产像素格式 → RHI 格式（镜像的格式映射点）")
D.Interface("[[nodiscard]] static ERHITextureDimension DimensionMirror(Resource::ETextureDimension Dim)",
            "资产纹理维度 → RHI 维度")
D.Interface("bool UploadTextureMirror(const Name::FName& AssetName, Resource::FTexture& Tex)",
            "按资产的 CPU 像素建一个 Persistent RDG 纹理并上传（经一块 transient staging 缓冲，"
            "一次传输提交），并把镜像存进表")

D.Class("TShaderHandle<T>", Desc="着色器句柄：对 T 的**异步编译**的显式同步包装。"
        "每个 T 的所有实例**共享一个** `FShaderState`（原生模块与字节码只建一次、跨帧复用）。"
        "特性在用之前调 `Wait()` —— 那就是「用之前先同步」的点（靠刷着色器服务器实现）。"
        "**没有回退路径**：`Wait()` 之前取值全是空。\n"
        "编译完全由 `FRender::TryGetShader`（友元）驱动；本类只暴露同步与取值。"
        "原生模块（`VModule` / `FModule`）是在**调用线程**上经池的 PSO 缓存惰性创建的，"
        "所以特性拿到的只是普通模块指针，不持有原生生命期。")
D.SetAccess("private")
D.Interface("explicit TShaderHandle(FRender* InOwner)", "私有构造：只有 `FRender::TryGetShader<T>` 能建")
D.SetAccess("public")
D.Interface("TShaderHandle() = default", "默认构造（空句柄：取值全空，`Wait()` 直接返回状态）")
D.Interface("TShaderHandle(const TShaderHandle&) = default", "可拷贝（共享同一份 T 的状态）")
D.Interface("TShaderHandle& operator=(const TShaderHandle&) = default", "可赋值")
D.Interface("bool Wait()",
            "阻塞到此前的全部异步编译完成，再报告请求的阶段是否都成功。"
            "**特性用模块之前必须调它**（「用之前先同步」点）")
D.Interface("[[nodiscard]] bool IsReady() const", "请求的阶段是否已成功编译完（非阻塞）")
D.Interface("[[nodiscard]] FRHIShaderModule* GetVertex()",
            "顶点模块（经池建一次并缓存）。无 VS / 未就绪时为 nullptr")
D.Interface("[[nodiscard]] FRHIShaderModule* GetFragment()", "片段模块（同上）")
D.Interface("[[nodiscard]] std::uint64_t GetVertexHash() const", "顶点阶段的**内容哈希**（无 / 未就绪为 0）")
D.Interface("[[nodiscard]] std::uint64_t GetFragmentHash() const", "片段阶段的内容哈希")
D.SetAccess("private")
D.Nested("FShaderState", Kind="struct",
         Desc="每个 T 一份的编译 / 就绪状态（所有句柄共享）：字节码、指纹、入口、已建模块与三个完成位。")
D.Interface("static FShaderState& GetState()",
            "函数内 `static` 的每个 T 一份状态（**跨 DLL 唯一性的关键**：模板实例按类型合并，"
            "而状态在函数静态存储里）")
D.Field("std::mutex Mutex", "（FShaderState）保护状态：编译回调在别的线程上写")
D.Field("bool bSubmitted = false", "（FShaderState）是否已提交过编译（合并重复请求）")
D.Field("bool bReady = false", "（FShaderState）请求的阶段是否都已就绪")
D.Field("bool bFailed = false", "（FShaderState）是否已失败（不再重试）")
D.Field("bool bVComplete = false", "（FShaderState）顶点阶段回调已完成")
D.Field("bool bFComplete = false", "（FShaderState）片段阶段回调已完成")
D.Field("std::vector<std::uint32_t> VS, FS", "（FShaderState）两阶段的 SPIR-V 词")
D.Field("std::uint64_t VHash = 0, FHash = 0", "（FShaderState）两阶段的内容指纹（PSO 缓存键）")
D.Field("std::string VEntry = \"main\", FEntry = \"main\"", "（FShaderState）两阶段的入口名")
D.Field("FRHIShaderModule* VModule = nullptr", "（FShaderState）惰性建出的顶点模块（池持有原生）")
D.Field("FRHIShaderModule* FModule = nullptr", "（FShaderState）惰性建出的片段模块")
D.Field("FRender* Owner = nullptr", "句柄持有的 FRender（取模块 / 等待编译都经它）")

# ══════════════════════════════════════════════════════════════════════════════
# Private/RenderResourcePool.h —— RDG 资源池（原子分配层）
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Private/RenderResourcePool.h", Title="RenderResourcePool.h —— RDG 资源池（原子分配层）",
         Desc="持有 `FRDG*Ref` 背后的原生 `FRHITexture` / `FRHIBuffer`。这是**原子分配层**："
              "它只根据「生命期类别 + 描述 + 引用计数」决定原生对象（与显存）**何时**被创建、复用、释放；"
              "它不知道 pass、布局转换或帧处理顺序 —— 那些层建在它上面。\n"
              "分配契约（稳定，上层能力都建在它上面）：\n"
              "**Persistent** = 身份即描述。首次请求在某个槽位建原生；引用计数 > 0 时槽位活跃；"
              "降到 0 就转为不活跃，但**原生 + 显存保留**（供后来同描述的请求复用）直到池 Shutdown；"
              "**描述不再匹配的 Persistent 槽位永不被回收**（它是长生命周期目标）。\n"
              "**Transient** = 帧内身份即描述。帧内与 Persistent 同样复用；但 `BeginFrame` 会把每个 "
              "transient 槽位置为不活跃（**不销毁原生与显存**），于是下一帧同描述的请求直接拿回去。"
              "**只有描述变了才重建**（不活跃槽位遇到不同描述 ⇒ 丢掉旧原生、建新的）。"
              "回收只发生在宿主等过上一帧栅栏之后 ⇒ 没有在飞命令还在引用它们。\n"
              "这就是「没有每帧 `vkCreate` / `vkAllocate`」的那一步。"
              "下一层（非重叠 transient 的 VMA 别名）需要尺寸 / 对齐 + 引用图，"
              "并且要把「建原生」与「绑显存」拆开 —— 这两件事现在都还没暴露。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("RDG.h", "`FRDGTextureRef` / `FRDGBufferRef` / `ERDGResourceLifetime`（本池签发的引用）")
D.Row("RHI/RHIServer.h", "`IRHI`：真正的 `Create*` / `Destroy*` 都经它（池不直接认识 Vulkan）")
D.Row("cstddef / mutex / new / string / vector", "字节数 / 保护列表的锁 / placement-new / 字节码头（模块内容键）/ 容器")

D.Class("FRHIResourcePool", Desc="RDG 资源池（`FRender` 私有持有）。它同时承担三件事："
        "资源生命周期（persistent / transient 槽位）、PSO 缓存（布局 / 管线 / 模块 / set / 采样器）、"
        "以及帧内 bump 分配器（`FParameters` 结构体）。三者的共同点都是「按描述去重、"
        "原生生命期归池」。")
D.SetAccess("public")
D.Interface("explicit FRHIResourcePool(IRHI* InRHI)", "构造：只绑 RHI（不建任何资源）")
D.Interface("FRDGTextureRef CreateTexture(const FRHITextureDesc& Desc, ERDGResourceLifetime Lifetime)",
            "要一块纹理：先找可复用的不活跃槽（描述 + 生命期类别都匹配），命中就复活，"
            "否则新建原生 + 视图并占一个槽")
D.Interface("FRDGBufferRef CreateBuffer(const FRHIBufferDesc& Desc, ERDGResourceLifetime Lifetime)",
            "要一块缓冲（同上逻辑）")
D.Interface("void ReleaseTexture(FRDGTextureRef& Ref)", "归还纹理引用（计数减一；清零入参引用）")
D.Interface("void ReleaseBuffer(FRDGBufferRef& Ref)", "归还缓冲引用")
D.Interface("[[nodiscard]] FRHICommandList* AcquireRenderList()",
            "**要一张全新的图形命令列表**给渲染特性的 pass。池会跟踪它以便延迟销毁："
            "特性在自己的 stage 里录制并提交，但列表**直到下一个 `BeginFrame`**（宿主等过上一帧的"
            "交换链栅栏之后）才销毁 —— 与 transient 资源同一条「按栅栏对齐的生命期」规则："
            "已提交的列表可能还在 GPU 上执行")
D.Interface("[[nodiscard]] FRHIPipelineLayout* GetOrCreatePipelineLayout("
            "const FRHIPipelineLayoutDesc& Desc)",
            "PSO 缓存：按描述 get-or-create 管线布局 —— 「没有每特性重复编译管线」的那一步。"
            "（当前数量级下线性扫描就够 —— 一个池里也就几条；哈希索引是显然的下一步升级）")
D.Interface("[[nodiscard]] FRHIDescriptorSetLayout* GetOrCreateDescriptorSetLayout("
            "const FRHIDescriptorSetLayoutDesc& Desc)",
            "PSO 缓存：set 布局（管线布局的依赖，所以也必须缓存）")
D.Interface("[[nodiscard]] FRHIGraphicsPipeline* GetOrCreateGraphicsPipeline("
            "const FRHIGraphicsPipelineDesc& Desc)",
            "PSO 缓存：栅格管线（描述里带 VS / FS 字节码指纹 ⇒ 相同字节码 + 状态共享一个原生）")
D.Interface("[[nodiscard]] FRHIShaderModule* GetOrCreateShaderModule(const FRHIShaderModuleDesc& Desc)",
            "PSO 缓存：着色器模块，**按内容（字节码副本 + 阶段 + 入口）做键**，不按指针身份 —— "
            "模块会从同一份字节码跨 pass 重建，必须共享一个原生")
D.Interface("[[nodiscard]] FRHISampler* CreateSampler(const FRHISamplerDesc& Desc)",
            "池**拥有**的采样器（按描述 get-or-create），特性持借用句柄。与纹理 / 缓冲同规则："
            "原生生命期归池（Shutdown 销毁），相同描述共享一个原生")
D.Interface("[[nodiscard]] FRHIDescriptorSet* GetOrCreateDescriptorSet("
            "FRHIDescriptorSetLayout* Layout, const FRHIDescriptorSetLayoutDesc& LayoutDesc, "
            "const FRHIDescriptorWrite* Writes, std::uint32_t WriteCount)",
            "池**拥有**的 set（按布局 + 引用资源 get-or-create）。未命中时：按布局的绑定建一个池 → "
            "分配 set → 经 `IRHI::UpdateDescriptorSets` 写内容（设备级，不是录制的 `vkCmd`）→ "
            "记下引用的资源备依赖跟踪。返回借用句柄；池在 Shutdown 销毁池 + set。**内容可寻址**："
            "相同 (布局, 写入) 共享一个 set")
D.Interface("[[nodiscard]] FRHIDescriptorSet* GetOrCreateMutableDescriptorSet("
            "FRHIDescriptorSetLayout* Layout, const FRHIDescriptorSetLayoutDesc& LayoutDesc)",
            "池**拥有**的**可变** set（**只按布局** get-or-create，每个布局分配一次）。"
            "这是 Static / PerFrame / PerPass 参数 set 的**唯一实现路径**：分配时不写内容，"
            "由 pass 在录制期用 `FRHICommandList::UpdateDescriptorSet` 重写。"
            "**频率只决定内容多久变一次，永远不决定机制。** 调用方在更新它时不能有在飞的提交读它 —— "
            "今天靠每帧 `BeginFrame` 的栅栏等待兜住（引擎当前是单帧图），"
            "多帧在飞（ring）是下一步升级。返回借用句柄；池在 Shutdown 销毁池 + set")
D.Interface("[[nodiscard]] FRHITexture* GetTexture(const FRDGTextureRef& Ref) const",
            "引用 → 原生纹理（当前帧内解析）")
D.Interface("[[nodiscard]] FRHITextureView* GetTextureView(const FRDGTextureRef& Ref)",
            "引用 → 原生纹理视图（附件 / 描述符绑定用）")
D.Interface("[[nodiscard]] const FRHITextureDesc& GetTextureDesc(const FRDGTextureRef& Ref) const",
            "引用 → **创建时那份描述**（格式 / 尺寸）。特性据此解析渲染目标，不必伸手去 RHI / 交换链")
D.Interface("[[nodiscard]] FRHIBuffer* GetBuffer(const FRDGBufferRef& Ref) const", "引用 → 原生缓冲")
D.Interface("void BeginFrame()",
            "**帧边界的两件事**：回收全部 transient 槽位（保留原生 + 显存，绝不每帧销毁）+ "
            "销毁上一帧提交的命令列表。由宿主在**等过上一帧栅栏之后**的帧首调用")
D.Interface("void Shutdown()", "销毁全部原生资源（关池）。顺序：管线 → 着色器模块 → 管线布局 → "
                              "set 布局（**set 布局必须比引用它的管线布局活得久**）→ 采样器 / set / 纹理 / 缓冲")
D.Interface("template <typename TParameters> [[nodiscard]] TParameters* AllocParameters()",
            "从池的**帧内**分配器 bump 分配一个宏声明的参数结构并 placement-new。"
            "返回的指针**只在当前帧有效**（bump 分配器在下一个 `BeginFrame` 复位）⇒ "
            "特性必须在分配它的那一帧里消费掉。对应 UE 的 `GraphBuilder.AllocateParameters<T>()`："
            "调用方永不管理这块内存，**帧边界就是生命期**")
D.Interface("[[nodiscard]] void* AllocateFrameTransient(std::size_t Size, std::size_t Align)",
            "从帧内 bump 池分配 `Size` 字节、按 `Align` 对齐。当前块放不下就**增长池（从不释放）**；"
            "每个块在下一个 `BeginFrame` 复位其 used 高水位（整池回收给下一帧）。"
            "`FRender::AllocParameters<T>()` 经非模板桥转发到这里，"
            "好让公开的渲染头（只前向声明池）永远不在不完整类型上实例化成员模板")
D.SetAccess("private")
D.Nested("FTextureEntry", Kind="struct", Desc="一个纹理槽位：描述 + 原生 + 视图 + 生命期类别 + 引用计数 + 是否活跃。")
D.Nested("FBufferEntry", Kind="struct", Desc="一个缓冲槽位（同上，无视图）。")
D.Nested("FPipelineLayoutEntry", Kind="struct",
         Desc="PSO 缓存项：**产生该原生的描述**。复用 = 找到描述与请求相等的项；"
              "保留描述是为了后来的同描述请求能匹配（也让池在 Shutdown 时能重新读到它）。")
D.Nested("FGraphicsPipelineEntry", Kind="struct", Desc="栅格管线缓存项（描述 + 原生）。")
D.Nested("FDescriptorSetLayoutEntry", Kind="struct",
         Desc="set 布局缓存项。**为什么要缓存：**布局是管线布局的依赖 —— 管线布局原生引用了它的 set "
              "布局，所以池 Shutdown 必须按「管线 → 管线布局 → set 布局」的顺序销毁"
              "（set 布局必须比引用它的管线布局活得久）。")
D.Nested("FShaderModuleEntry", Kind="struct",
         Desc="着色器模块缓存项：**按内容做键**（字节码副本 + 阶段 + 入口），不按指针身份。"
              "模块会从同一份字节码跨 pass 重建，必须共享一个原生；Shutdown 顺序是"
              "「管线 → 着色器模块 → 管线布局 → set 布局」。")
D.Nested("FSamplerEntry", Kind="struct", Desc="采样器缓存项（描述 + 原生）—— 与纹理 / 缓冲一样归池所有。")
D.Nested("FDescriptorSetEntry", Kind="struct",
         Desc="描述符集项：池 + set + 布局 + 写入列表。`Writes` 既是**内容寻址的键**，"
              "也是「引用了哪些资源」的记录。")
D.Interface("[[nodiscard]] std::int32_t FindReusableTexture(const FRHITextureDesc& Desc, "
            "ERDGResourceLifetime Lifetime) const",
            "找一个描述与生命期类别都匹配的**不活跃**槽位；没有就返回 -1（调用方去建新的）")
D.Interface("[[nodiscard]] std::int32_t FindReusableBuffer(const FRHIBufferDesc& Desc, "
            "ERDGResourceLifetime Lifetime) const",
            "缓冲版的复用查找")
D.Interface("[[nodiscard]] std::uint32_t AllocTextureSlot()",
            "占一个纹理槽位（优先用空闲表里的，否则追加）并返回下标 —— **引用里的 Id 就是它**")
D.Interface("[[nodiscard]] std::uint32_t AllocBufferSlot()", "占一个缓冲槽位并返回下标")
D.Interface("void DestroyTextureEntry(FTextureEntry& Entry)",
            "丢掉槽位的原生 + 视图（**只在不活跃槽位的描述变了时用**）")
D.Interface("void DestroyBufferEntry(FBufferEntry& Entry)", "同上，缓冲版")
D.Field("IRHI* RHI = nullptr", "底层 RHI（池的所有原生创建 / 销毁都经它，自己不认识 Vulkan）")
D.Field("std::vector<FTextureEntry> Textures", "纹理槽位表（下标 = 引用里的 Id）")
D.Field("std::vector<std::uint32_t> FreeTextureSlots", "空闲槽位表（新请求优先复用）")
D.Field("std::vector<FBufferEntry> Buffers", "缓冲槽位表")
D.Field("std::vector<std::uint32_t> FreeBufferSlots", "空闲缓冲槽位表")
D.Field("std::vector<FRHICommandList*> PendingRenderLists",
        "本帧取到的命令列表，**下一个 `BeginFrame` 才销毁**（已提交的列表可能还在 GPU 上跑）")
D.Field("std::mutex RenderListsMutex", "保护待销毁列表（**特性会在池的工作线程上取列表**）")
D.Field("std::vector<FPipelineLayoutEntry> PipelineLayouts", "PSO 缓存：管线布局（按描述做键）")
D.Field("std::vector<FDescriptorSetLayoutEntry> DescriptorSetLayouts", "PSO 缓存：set 布局（管线布局的依赖）")
D.Field("std::vector<FShaderModuleEntry> ShaderModules", "PSO 缓存：着色器模块（按字节码内容做键）")
D.Field("std::vector<FGraphicsPipelineEntry> GraphicsPipelines", "PSO 缓存：栅格管线（按描述做键）")
D.Field("std::vector<FSamplerEntry> Samplers", "池拥有的采样器（按描述 get-or-create）")
D.Field("std::vector<FDescriptorSetEntry> DescriptorSets",
        "池拥有的描述符池 + set（内容可寻址）")
D.Field("std::vector<FDescriptorSetEntry> MutableDescriptorSets",
        "池拥有的可变 set（按布局做键，**录制期写内容**）")
D.Nested("FFrameChunk", Kind="struct",
         Desc="帧内 bump 池的一块：一块字节缓冲 + 用到的偏移。"
              "参数结构都很小（几个描述符 + push constant 标量），所以按**固定块**分配并在帧之间复用"
              "（从不释放、从不 realloc ⇒ 未失效的指针在它那一帧里始终有效）。")
D.Field("std::vector<std::byte> Data", "（FFrameChunk）块数据")
D.Field("std::size_t Used = 0", "（FFrameChunk）块内 bump 偏移")
D.Field("std::vector<FFrameChunk> FrameChunks",
        "帧内参数池：`BeginFrame` 复位每个块的 used 高水位 ⇒ 整池回收给下一帧，"
        "**这就是约束参数生命期的帧边界**")
D.Field("std::size_t FrameChunkCursor = 0", "上次服务的块（分配从它往后找）")
D.Field("std::mutex FrameAllocMutex", "保护 bump 分配（**特性会从池的工作线程上分配参数**）")
