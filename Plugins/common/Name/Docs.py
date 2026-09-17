# -*- coding: utf-8 -*-
# Name 插件文档内容（由 Tools/plugin_docs.py 执行，docs_builder 以变量 D 注入）。

# ══════════════════════════════════════════════════════════════════════════════
# Public/NameApi.h —— 模块导出标签
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/NameApi.h", Title="NameApi.h —— 模块导出标签",
         Desc="本模块的 DLL 导出标签。独立成头：只需要「名字池访问器」声明的模块"
              "不必连带引入 `Name.h` 的全部内容。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT`（引擎核心提供）")

D.Macro("MAHO_NAME_API", "MAHO_EXPORT / MAHO_IMPORT（随构建开关切换）",
        Desc="编译 `Name.dll` 时构建系统定义 `MAHO_NAME_MODULE_EXPORTS` ⇒ `MAHO_EXPORT`；"
             "消费方为 `MAHO_IMPORT`。\n"
             "**为什么必须有**：`FNamePool* GetNamePool()` 跨 DLL 返回类型，"
             "而池对象的虚表与删除析构符必须只在本模块生成 —— "
             "`dllimport` 让「消费方自己生成一份」变成编译错误，而不是卸载后 `delete` 静默崩。")

# ══════════════════════════════════════════════════════════════════════════════
# Public/Name.h —— 字符串驻留池 + FName 句柄
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Name.h", Title="Name.h —— 字符串驻留池 + FName（服务层）",
         Desc="**驻留**（interning）出来的不可变字符串标识符：`FName` 只存一个池内 id，"
              "相同文本共享同一条池记录，因此比较退化成整数比较。\n"
              "**为什么需要它**：资源键、骨骼名、参数名这类「反复比较、很少新建」的字符串，"
              "用 `std::string` 每次比较都要走一遍字节；换成 id 后 `==` 是 O(1)，"
              "而构造 / 析构也不再各带一次堆分配。\n"
              "**为什么不无脑用 `FName`**：驻留是**永久**的（本设计里池只增不减），"
              "一次性字符串用它反而在池里留下永不回收的垃圾 —— "
              "短命文本请直接用 `std::string`。\n"
              "**为什么 id 是 `std::uint32_t` 且 0 表示 None**：默认构造必须是一个「零成本、可判断」"
              "的无效值，0 天然满足；定宽 32 位让 id 可以安全地序列化 / 跨模块传递。\n"
              "**线程安全**：`Intern` 内部持互斥量；`FName` 自身是纯值类型（无锁）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("NameApi.h", "`MAHO_NAME_API` —— 本模块的导出标签")
D.Row("<Maho.h>", "引擎聚合头（`FFrameExtension` / `IPipeline` 等基础设施）")
D.Row("<Engine/Frame.h>", "`MAHO_DECLARE_FRAME` / stage 接口 —— 帧的声明词汇")
D.Row("<cstdint>", "`std::uint32_t` —— id 的定宽表示（跨模块 / 序列化都靠它）")
D.Row("<functional>", "底部 `std::hash<FName>` 特化所需（把 `FName` 用作哈希键）")
D.Row("<mutex>", "`Intern` 的互斥量 —— 池是共享可变状态，必须串行化**写入**")
D.Row("<string> / <string_view>", "池拥有 `std::string`；`Intern` 入参用 `string_view`（零拷贝观察）")
D.Row("<unordered_map>", "`文本 → id` 的查找表 —— 键是字符串，恰好是哈希容器的主场")
D.Row("<vector>", "id → 文本的反查表：**id 就是下标**，反查是 O(1) 数组索引")

D.Card("自由函数")
D.Table("签名", "说明")
D.Row("MAHO_NAME_API FNamePool* GetNamePool()",
      "全局池访问点：`Initialize` 里发布、`Shutdown` 里清除（未上线时为 `nullptr`）。"
      "返回函数而非导出裸变量 —— 跨 DLL 的裸变量拿不到同一份实例的风险太大")

D.Class("FName",
        Desc="驻留字符串的**值句柄**（32 位 id）。默认构造即 `None`（id == 0）。\n"
             "**为什么是值类型而不是指针 / 引用**：id 可以自由拷贝、按值传参、当作 map 键、"
             "写进存档 —— 池自身的生命周期因此不会泄漏到 API 表面（只要求池在 `Intern` 时活着）。\n"
             "`ToString()` 需要回池查一次（O(1) 数组索引）；正因如此「比大小」用 id、"
             "「要文本」才付出那次查表。")
D.SetAccess("public")
D.Interface("FName() = default",
            "默认构造 = `None`（id 0）—— **不需要访问池**，因此 `FName` 可以用在静态存储期 / "
            "不需要初始化顺序保证的场合")
D.Interface("explicit FName(std::string_view Str)",
            "构造即驻留：向全局池登记文本并取得 id。**`explicit`**：避免字面量被隐式转换，"
            "让「这里要付一次驻留成本」在调用点可见")
D.Interface("[[nodiscard]] std::string_view ToString() const",
            "反查文本（池内 O(1) 索引）。**为什么返回视图**：池里的文本是稳定地址且池只增不减，"
            "所以视图安全；返回 `std::string` 会白送一次拷贝")
D.Interface("[[nodiscard]] bool IsNone() const", "是否为 `None`（id == 0）")
D.Interface("[[nodiscard]] std::uint32_t GetId() const", "取 id —— 序列化 / 跨模块传递用")
D.Interface("[[nodiscard]] static FName FromId(std::uint32_t InId)",
            "从 id 零成本重建 `FName`（**不做查表、不做驻留**）：id 本身就是池内的规范条目。"
            "只对来自 `GetId()` 的 id 有意义（0 即 None）")
D.Interface("[[nodiscard]] bool operator==(const FName& O) const",
            "O(1) 比较 —— **这就是驻留的全部收益**：两个同文本的名字必得同一 id")
D.Interface("[[nodiscard]] bool operator!=(const FName& O) const", "同上（不等）")
D.Interface("[[nodiscard]] bool operator<(const FName& O) const",
            "按 id 排序：给 `std::map<FName, ...>` 之类提供稳定但**与字典序无关**的次序"
            "（只在需要「一致的任一顺序」时使用）")
D.SetAccess("private")
D.Interface("friend class FNamePool", "只许池用 id 直接构造（外部必须经 `FromId`）")
D.Interface("explicit FName(std::uint32_t InId)",
            "私有 id 构造：把「已有 id」与「需要驻留的文本」在类型层面区分开")
D.Field("std::uint32_t Id = 0",
        "池内 id（0 = None）。**类只有这一个成员** ⇒ 拷贝廉价、可当 POD 用")

D.Class("FNamePool", Base="FFrameExtension, IPipeline<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>",
        Desc="全局驻留池 —— **一个帧**（挂 6 个 stage），因此上线 / 下线由调度图排序："
             "所有 `Intern` 都发生在它 `Initialize` 之后、`Shutdown` 之前。\n"
             "**为什么「只增不减」**：释放驻留记录会和 `FName` 的值语义直接冲突 —— "
             "id 可以活得比一次回收更久（它已被拷进别的对象 / 写进存档）。"
             "因此本设计的取舍是：池在生命期内只增长，换来 id 永久有效。\n"
             "**为什么用互斥量**：`Intern` 是写入共享结构的操作，"
             "必须串行化；读取走 `FName` 的 id，不碰池。")
D.SetAccess("private")
D.Interface("MAHO_DECLARE_FRAME(FNamePool)",
            "声明帧身份（稳定字面量名字 + `CreateFrame` 工厂），宿主可断言它先于使用者就绪")
D.Interface("void PreInitialize(FEngineBase&) override", "空：本帧不需要更早的时钟点")
D.Interface("void Initialize(FEngineBase&) override",
            "发布全局池访问点（`GetNamePool()` 从这里开始非空）")
D.Interface("void PostInitialize(FEngineBase&) override", "空")
D.Interface("void PreShutdown(FEngineBase&) override", "空")
D.Interface("void Shutdown(FEngineBase&) override",
            "清空并撤销全局访问点。**注意顺序**：池的释放发生在所有使用者之后（依赖图保证），"
            "因为 `FName` 的 `ToString()` 需要池还在")
D.Interface("void PostShutdown(FEngineBase&) override", "空")
D.SetAccess("public")
D.Interface("FName Intern(std::string_view Str)",
            "驻留一个字符串并返回其规范 `FName`（线程安全）。已存在则直接返回旧 id —— "
            "**同一个文本永远映射到同一个 id**，这是 `==` 能 O(1) 的前提")
D.Interface("[[nodiscard]] std::string_view StringForId(std::uint32_t Id) const",
            "`Intern` 的逆：取该 id 对应的文本。**只增不减**的设计让返回值可以安全地做成视图")
D.SetAccess("private")
D.Interface("void free()",
            "释放池内容（被 `Shutdown` 调用）：清两份容器，保证再次 `Initialize` 从空开始")
D.Field("std::mutex Mutex",
        "保护下面两份容器的**写**路径（`Intern` 可能从多个线程来）")
D.Field("std::vector<std::string> Pool",
        "id → 文本（**id 即下标** ⇒ 反查 O(1)，且 `id == 0` 天然对应「空 / None」）")
D.Field("std::unordered_map<std::string, std::uint32_t> Lookup",
        "文本 → id（`Intern` 的去重查找表）")

D.Struct("std::hash<Maho::Name::FName>", Base="特化",
         Desc="为 `FName` 提供标准哈希 —— 让它可以当 `unordered_map` / `unordered_set` 的键。\n"
              "**为什么直接哈希 id**：id 本身就是池内的唯一编号，"
              "对 `std::uint32_t` 取哈希既稳定又便宜（不必再碰字符串）")
D.SetAccess("public")
D.Interface("std::size_t operator()(const Maho::Name::FName& N) const noexcept",
            "返回 `std::hash<std::uint32_t>{}(N.GetId())`；`noexcept` 是哈希容器要求的契约")
