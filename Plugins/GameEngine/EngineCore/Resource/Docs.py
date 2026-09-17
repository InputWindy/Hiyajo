#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Resource 插件的文档内容。

运行器（Tools/plugin_docs.py）把 docs_builder 作为变量 D 注入本文件 —— 只声明，不 import。
"""

# ══════════════════════════════════════════════════════════════════════════════
# Public/ResourceApi.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/ResourceApi.h", Title="ResourceApi.h —— 插件导出标记",
         Desc="只有导出标记 `MAHO_RESOURCE_API`。它保护的是 `FResource` 这一族类型："
              "资源在「拥有它的类型」的模块里构造，却由资源系统的目录销毁，"
              "而触发导入的调用方可能是一个早于目录被卸载的子插件。"
              "把类型标成 DLL 接口后，消费者必须通过导入表引用 vftable 与删除析构，"
              "编译器无法把即将释放的模块的 vptr 烙进实例 —— 否则崩溃点会落在 `delete` 里，"
              "而且没有任何编译错误提示。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT` 原语；引擎 core 是插件唯一的下层依赖")

D.Macro("MAHO_RESOURCE_API", "MAHO_EXPORT / MAHO_IMPORT",
        "构建 Resource.dll 时（`MAHO_RESOURCE_MODULE_EXPORTS`）取导出，其余消费者取导入。"
        "同一份头在两侧各自正确，无需两套声明")

# ══════════════════════════════════════════════════════════════════════════════
# Public/Resource.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Resource.h", Title="Resource.h —— 类型化的异步资源系统",
         Desc="资源的**搬运与生命周期**在这里，**资产是什么**不在这里（那是 Asset 插件）。"
              "本插件只认识 `FResource` 基类与三个要按类型特化的钩子，"
              "因此它能把「读盘 / 写盘 / 目录 / 异步」做成一套与类型无关的机器。\n"
              "线程模型是这套设计的主轴：读盘与写盘跑在一条**专用 IO 线程**上（`FThreadedServer`），"
              "而解码 / 编码与所有广播都在**游戏线程**（`Tick` 里应用就绪传输）。"
              "这解释了两条看似别扭的规定：`Import<T>` 的 importer 在游戏线程跑（所以它可以安全地碰目录），"
              "而 `Export<T>` 的编码也在调用方线程跑（所以它能同步读目录里的资源），只有最终 `WriteBytes` 交给 IO 线程。\n"
              "虚路径（`Raw/mesh.fbx`）经 `FPaths` 解析成物理路径，目录键则是 `FName`（去掉扩展名的资产路径）—— "
              "路径字符串在跨线程传递时容易失效，`FName` 是稳定身份，所以广播里传的是它而不是指针。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/ThreadedServer.h", "`FThreadedServer`：`FResourceSystem` 的基类之一 —— "
                              "那条专用 IO 线程 + FIFO 串行队列就是它")
D.Row("<Maho.h>", "`FFrameExtension` + `MAHO_DECLARE_FRAME`（帧身份）")
D.Row("Engine/Engine.h", "10 个 stage 接口与 `FEngineBase`（生命周期由引擎循环驱动）")
D.Row("ResourceApi.h", "本插件的导出标记")
D.Row("Core/Delegate.h", "四个 `TMulticastEvent` 广播点：导入 / 卸载 / 创建 / 导出完成")
D.Row("<Name.h>", "`Name::FName`：目录键与广播里的资产身份（稳定、可拷贝、不悬垂）")
D.Row("<cstdint> / <span>", "定宽整型；字节以 `std::span<const std::uint8_t>` 交给 importer —— "
                            "只读视图，因此跨越 importer 边界的**不是**所有权，而是借用")
D.Row("<functional> / <memory>", "传输完成回调与 GPU 回读提供者；`unique_ptr` 持有的私有实现 `FImpl`")
D.Row("<string> / <string_view> / <utility> / <vector>", "路径与键；转发构造参数；导出的字节缓冲")

D.Card("前向声明与模板钩子（未定义的模板 = 编译期的扩展点）")
D.Table("声明", "说明")
D.Row("class FResource / class FResourceSystem;",
      "互指：`FResourceSystem` 的方法签名用到 `FResource`，`FResource` 的实现用到目录，"
      "前向声明让这个循环停在指针层面")
D.Row("struct FTransferState / struct FBulkData / class FTransferHandle;",
      "异步传输机制的三个内部类型**只前向声明** —— 头里用不到它们的布局，"
      "于是 `FImpl` 可以把它们完全藏进 cpp，头不必背它们的依赖")
D.Row("template <typename TResource> struct TResourceImporter / TResourceExporter / "
      "TResourceCreateDesc / TResourceCreator;",
      "四个**故意不定义**的模板：它们是留给资产类型的扩展点。不定义意味着「用到就必须特化」，"
      "缺特化是编译错误而不是运行期空指针")

D.Card("自由函数与别名")
D.Table("签名", "说明")
D.Row("MAHO_RESOURCE_API FResourceSystem* GetResourceSystem()",
      "全局访问器，**跨 DLL 走函数**。头里能看到 `FResourceSystem` 的声明是因为调用方要做 `Import<T>`，"
      "而返回指针而不是引用，是为了「尚未初始化」也能表达（返回 nullptr，而非未定义行为）")
D.Row("using FOnTransferDone = std::function<void(bool bSuccess, std::string_view Error)>",
      "传输完成回调。上游（`FResourceSystem`）构造它并随广播交给监听者；监听者消费完（如已上传并建镜像）"
      "再调用它报告成败 —— **成功后资源系统才会丢掉 CPU 载荷**，所以它是「谁负责释放」的交接点")
D.Row("using FReadbackFn = std::function<bool(const Name::FName& AssetName, FResource& Resource)>",
      "GPU 回填提供者：资源 CPU 载荷已被镜像取走时，导出前先有它把 GPU 数据解码回资源字段。"
      "由 FRender 注入 —— 解码知识留在消费者侧，Resource 因此不必认识任何图形类型。"
      "返回 false 表示未做镜像或回读失败")
D.Row("std::size_t detail::FindLastDot(std::string_view Path)",
      "内部工具：找最后一个点，用于把源路径剥成资产路径。放进 `detail` 命名空间是为了"
      "「公开但不承诺稳定」，让头内联的模板定义能用它而不必引入一份完整实现")

D.Struct("FImportConfig", Desc="导入配置的通用基：一个**虚路径**（如 `Raw/mesh.fbx`）。"
                               "资产路径（目录键）由剥掉扩展名得到 —— "
                               "所以「文件在哪」和「资产叫什么」是同一个字符串的两个视图，不会各自漂移。")
D.SetAccess("public")
D.Field("std::string SourcePath", "虚源路径，例如 `Raw/mesh.fbx`；资产路径由剥掉扩展名推出")

D.Struct("FExportConfig", Desc="导出配置的通用基：一个**物理绝对路径**。导出目标刻意不是虚路径 —— "
                               "写盘目标由调用方明确给出，不需要（也不应）经 `FPaths` 反解。")
D.SetAccess("public")
D.Field("std::string DestinationPath", "物理目标路径，例如 `C:/Out/mesh.fbx`")

D.Class("FResource", Desc="资源基类，所有具体资源派生自它。它只承载两件事：**路径**与"
                          "**载荷是否还在**（`HasBulk` / `ReleaseBulk`）。"
                          "第二件事之所以在基类上，是因为「镜像到 GPU 后丢掉 CPU 数据」是"
                          "全类型通用的生命周期，而不知道具体类型的调用方（资源系统、渲染镜像）"
                          "必须能在不看子类的情况下处理它。\n"
                          "**导出为 DLL 接口**：实例由拥有其类型的模块构造，却由目录销毁，"
                          "而触发导入的调用方可能是先被卸载的子插件 —— 导出标记让每个消费者"
                          "通过导入表引用 vftable 与删除析构，编译器就无法把自己（即将释放）的 "
                          "vptr 烙进实例。构造侧的同一条约束见 `TResourceCreator`。")
D.SetAccess("public")
D.Interface("virtual ~FResource() = default", "虚析构：资源经基类指针删除，"
                                              "删除析构必须在拥有类型的模块里（这正是导出标记的作用）")
D.Interface("explicit FResource(std::string InPath)", "以资产路径构造；`explicit` 防止字符串隐式转成资源")
D.Interface("FResource(FResource&&) = default", "默认移动：目录用 `unique_ptr` 持有，"
                                                "移动让注册/转移不需要拷贝（载荷可能很大）")
D.Interface("FResource& operator=(FResource&&) = default", "默认移动赋值")
D.Interface("FResource(const FResource&) / operator= —— 隐式删除",
            "**声明了移动就不生成拷贝**：资源按值复制既昂贵又会让「目录里唯一一份」的假设失效，"
            "所以删除是想要的语义，而不是疏漏")
D.Interface("[[nodiscard]] std::string_view GetPath() const", "读资产路径（返回视图：路径由本对象持有，"
                                                              "调用方在对象存活期内可零拷贝读）")
D.Interface("virtual void ReleaseBulk()",
            "释放 CPU 载荷（如解码后的像素 / 顶点），由已经取走的消费者（渲染镜像）触发。"
            "默认空实现 —— 只有载荷笨重的类型才重写。在游戏线程调用")
D.Interface("[[nodiscard]] virtual bool HasBulk() const",
            "当前是否还有 CPU 载荷。默认 true；能丢（已镜像到 GPU）的类型重写它，"
            "在 `ReleaseBulk()` 之后返回 false")
D.SetAccess("private")
D.Field("std::string Path", "资产路径（私有 —— 对外只给 `string_view`，避免外部持有一份会过期的拷贝语义）")

D.Class("FResourceSystem", Base="FFrameExtension + IPipeline<IPreInit, IInit, IPostInit, IBeginFrame, "
                                "ITick, IEndFrame, IExit, IPreShutdown, IShutdown, IPostShutdown> "
                                "+ FThreadedServer",
        Desc="资源系统：异步**传输服务器** + 类型化导入导出 + `FName` 目录。"
             "它挂在完整阶段序列上，因为异步传输的收尾必须落在确定的那一帧："
             "`Initialize` 起 IO 线程，`Tick`（每帧）在**游戏线程**应用就绪的传输结果，"
             "`Shutdown` 停线程并清空目录。\n"
             "**为什么 `Tick` 才应用结果**：解码广播（带完成回调）会调用监听者（渲染镜像）并最终决定"
             "是否 `ReleaseBulk`，这些都必须发生在单一游戏线程上，否则监听者要在 IO 线程里碰 GPU。\n"
             "私有实现 `FImpl` 以 `unique_ptr` 存在：句柄 / bulkdata / 待处理队列这些机制完全不进头，"
             "所以它们的依赖（fstream、`FPaths`、锁结构）不会传染给每个包含本头的插件。")
D.SetAccess("public")
D.Interface("MAHO_DECLARE_FRAME(FResourceSystem)",
            "帧身份与工厂符号：`FFrameBuilder` 按 DLL 路径装载它的唯一凭据")
D.Interface("~FResourceSystem() override",
            "析构：停 IO 线程并清掉目录。因为类型带 `MAHO_API` 语义（宿主经基类持有），"
            "删除析构必须与构造同模块")
D.Interface("TMulticastEvent<void(const Name::FName&, FOnTransferDone)> OnAssetImported",
            "**导入完成**（游戏线程）广播。监听者（如渲染镜像）把载荷拷到 GPU，然后调用 `Done` 报告成败 —— "
            "成功时资源系统丢掉 CPU 载荷。资产按 `FName` 用 `Find()` 查回，**不传指针**，所以没有悬垂可言")
D.Interface("TMulticastEvent<void(const Name::FName&, FOnTransferDone)> OnAssetUnloaded",
            "**卸载 / 失效**（游戏线程）广播，负载语义与导入一致，好事先释放 GPU 侧持有物")
D.Interface("TMulticastEvent<void(const Name::FName&, const FResource&)> OnAssetCreated",
            "**运行期创建完成**（游戏线程）广播：资源已注册进目录后发出，引用传递（已常驻，监听者不得接管所有权）。"
            "监听者（FRender）据此建以 `FName` 为键的持久 GPU 镜像")
D.Interface("TMulticastEvent<void(const Name::FName&, bool)> OnAssetExported",
            "**导出完成**（游戏线程）广播：资产名 + 是否成功。写盘在 IO 线程，"
            "但上报要等 `Tick` 把它应用回来，所以成功与「字节真的落盘」严格同序")
D.Interface("void SetReadback(FReadbackFn Fn)",
            "注入 GPU 回读提供者（FRender），使导出在 CPU 载荷已被丢弃时能先回填。渲染初始化时调用一次")
D.Interface("template <typename TResource> bool Import(typename TResourceImporter<TResource>::FConfig Config)",
            "异步导入：`SourcePath` 为空即返回 false；否则算出资产路径、把读盘排给 IO 线程，"
            "**在游戏线程**用 `TResourceCreator<TResource>::Create(AssetPath)` 造实例、"
            "解到实例里、注册进目录、广播 `OnAssetImported`。实例绝不在这里 `make_unique` —— "
            "那会把 vtable 与删除析构烙进调用方模块（可能先被卸载）")
D.Interface("template <typename TResource> bool Export(typename TResourceExporter<TResource>::FConfig Config, "
            "std::string_view AssetPath)",
            "异步导出：按资产路径 + `dynamic_cast` 找到正确类型的资源；若 CPU 载荷已被丢弃且注入了回读提供者，"
            "先回填；然后**在调用方（游戏）线程编码**（同步读目录资源是安全的），"
            "只有 `WriteBytes` 交给 IO 线程")
D.Interface("template <typename TResource> TResource* CreateResource(std::string_view AssetPath, "
            "typename TResourceCreateDesc<TResource>::FConfig Config)",
            "运行期/动态路径：按描述符造一个带 GPU 构建信息（尺寸/格式/usage）的持久资源并注册，"
            "返回可变实例（调用方不持有所有权）。注册完成后**同步**广播 `OnAssetCreated`，"
            "所以监听者在广播里 `Find` 一定能看到它。同路径的旧条目会被覆盖（旧对象销毁但**不广播**）；"
            "`AssetPath` 为空返回 nullptr。逐帧的临时 GPU 资源不走这条路")
D.Interface("bool DestroyResource(std::string_view AssetPath)",
            "从目录移除并销毁：广播 `OnAssetUnloaded` 让监听者释放 GPU 侧持有物；路径不存在返回 false")
D.Interface("[[nodiscard]] const FResource* Find(std::string_view AssetPath) const",
            "查已加载资源；不存在返回 nullptr。返回**只读**指针 —— 目录的写入口只有导入/创建/销毁三条")
D.Interface("[[nodiscard]] FResource* FindMutable(std::string_view AssetPath)",
            "可变查找，专供内部的回填路径（导出的 `Readback`）；不存在返回 nullptr")
D.Interface("[[nodiscard]] const FResource* TryLoad(std::string_view AssetPath)",
            "「先查目录，还没有就返回 nullptr」—— 名字直说它**不**发起加载（没有隐式同步 IO，"
            "异步导入必须显式调用）")
D.Interface("void ForEachResource(const std::function<void(const Name::FName&, const FResource&)>& Fn) const",
            "按目录顺序遍历已加载资源。**遍历期间整个目录锁被持有**，所以 `Fn` 不许回到资源系统内部"
            "（不许在回调里 `Find`/`Import`）—— 这是刻意的强约束，换来的是一条不会中途变化的快照")
D.SetAccess("private")
D.Interface("void PreInitialize(FEngineBase&) override {}", "阶段入口（空实现）")
D.Interface("void Initialize(FEngineBase& Engine) override", "阶段入口：启动 IO 线程（异步机制从这里开始可用）")
D.Interface("void PostInitialize(FEngineBase&) override {}", "阶段入口（空实现）")
D.Interface("void BeginFrame(FEngineBase&) override {}", "阶段入口（空实现）")
D.Interface("void Tick(FEngineBase& Engine) override",
            "阶段入口：在游戏线程应用就绪的 IO 结果并广播 —— 所有监听者回调都发生在这一个点上")
D.Interface("void EndFrame(FEngineBase&) override {}", "阶段入口（空实现）")
D.Interface("void RequestExit(FEngineBase&) override {}", "阶段入口（空实现）")
D.Interface("void PreShutdown(FEngineBase&) override {}", "阶段入口（空实现）")
D.Interface("void Shutdown(FEngineBase& Engine) override",
            "阶段入口：停 IO 线程并清空目录（对称于 Initialize）。作为本层自己的异步队列，"
            "它必须**自己排空**，不能指望调度器替它等")
D.Interface("void PostShutdown(FEngineBase&) override {}", "阶段入口（空实现）")
D.Interface("FResourceSystem()",
            "私有构造：资源系统是**帧**（由 `FFrameBuilder` 装载/卸载），而不是随便 new 的普通对象")
D.Interface("bool EnqueueImport(std::string SourcePath, std::string AssetPath, "
            "std::function<void(std::span<const std::uint8_t>)> OnBulkReady)",
            "把「读文件」排给 IO 线程；字节就绪后在游戏线程回调 `OnBulkReady`（那里才造实例 + 解码）")
D.Interface("std::vector<std::uint8_t> ReadAssetFile(std::string_view SourcePath)",
            "同步整文件读取（`FPaths` 解析 + ifstream）。**实现在 .cpp 里，头因此不引入 "
            "<fstream> / <Paths>**；失败返回空")
D.Interface("bool EnqueueExport(std::vector<std::uint8_t> Bytes, std::string DestinationPath, "
            "Name::FName AssetName)",
            "**编码在调用方线程完成**后，把字节交给 IO 线程 `WriteBytes`；完成回调在游戏线程触发")
D.Interface("const FResource* RegisterResource(std::string AssetPath, std::unique_ptr<FResource> Resource)",
            "入目录（接受所有权）。**必须广播之前注册**：监听者在广播里 `Find`/`FindMutable` 必须已经能看到它。"
            "实现放在 .cpp，因为模板不敢解引用头里不完整的 `FImpl`")
D.Interface("FTransferHandle RequestLoad(std::string Path)", "发起一次传输（句柄 / bulkdata 机制全在 .cpp 里）")
D.Interface("void ProcessReadyIO()", "把 IO 线程标记就绪的传输应用回来（由 `Tick` 调用）")
D.Interface("static bool WriteBytes(std::string_view PhysicalPath, std::span<const std::uint8_t> Bytes)",
            "唯一写盘出口（IO 线程侧）。静态 —— 它不需要任何实例状态，只有路径与字节")
D.Interface("FOnTransferDone MakeTransferDone(std::string AssetPath)",
            "按资产路径造完成回调：成功时**在目录锁下**执行 `ReleaseBulk()`，"
            "使「丢掉载荷」与目录状态在同一临界区里成立")
D.Field("class FImpl; std::unique_ptr<FImpl> Impl",
        "私有实现的锤子：传输状态 / bulkdata / 待处理队列全在 cpp，头里只有这个不完整类型。"
        "`unique_ptr` 同时意味着**拷贝被隐式删除**（资源系统本来也只能有一个）")
D.Field("FReadbackFn Readback", "GPU 回读提供者，由 `SetReadback` 注入（FRender）；未注入时导出无法回填")
