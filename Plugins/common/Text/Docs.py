# -*- coding: utf-8 -*-
# Text 插件文档内容（由 Tools/plugin_docs.py 执行，docs_builder 以变量 D 注入）。

# ══════════════════════════════════════════════════════════════════════════════
# Public/TextApi.h —— 模块导出标签
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/TextApi.h", Title="TextApi.h —— 模块导出标签",
         Desc="本模块的 DLL 导出标签。独立成头：只要「文本管理器访问器」声明的模块不必引入 `Text.h`。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT`（引擎核心提供）")

D.Macro("MAHO_TEXT_API", "MAHO_EXPORT / MAHO_IMPORT（随构建开关切换）",
        Desc="编译 `Text.dll` 时构建系统定义 `MAHO_TEXT_MODULE_EXPORTS` ⇒ `MAHO_EXPORT`；"
             "消费方为 `MAHO_IMPORT`。\n"
             "**为什么必须有**：`FTextManager* GetTextManager()` 跨 DLL 返回类型，"
             "而管理器（含 `FText`）的虚表与删除析构符必须只在本模块生成 —— "
             "`dllimport` 把「消费方各自生成一份」变成编译错误，而不是卸载后 `delete` 时静默崩。")

# ══════════════════════════════════════════════════════════════════════════════
# Public/Text.h —— 本地化文本
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Text.h", Title="Text.h —— 本地化文本（句柄 + 目录，服务层）",
         Desc="本地化的两条腿：`FText` 是**句柄**（`{Namespace, Key, Source}`），"
              "`FTextManager` 是**目录 + 当前语言**（`SetCulture` 选语言，`FText::Resolve()` 查翻译并回退到原文）。\n"
              "**为什么用句柄而不是直接存翻译后的字符串**：界面在第一帧构建时就持有文本，"
              "而玩家可能在第 300 帧切语言。句柄只记「哪个命名空间的哪个键」，"
              "切语言后**下一次 `Resolve()` 自然给出新文本** —— 不需要重建任何 UI 组件。\n"
              "**为什么要 `Source`（原文）**：翻译缺失是常态（新加的文案还没来得及翻）。"
              "把原文随句柄一起带着，`Resolve` 就能优雅回退：宁可显示英文，也不要显示一个键名。\n"
              "**为什么 `Namespace` 而不是纯 Key**：`Key` 会在不同系统间撞名（例如都叫 `Title`），"
              "命名空间把「哪个模块的 Title」这件事显式化，也让翻译文件可以按模块拆分。\n"
              "**线程安全**：目录由互斥量保护（`Resolve` 很可能来自渲染线程，"
              "而 `AddTranslation` 可能来自加载线程）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("<Maho.h>", "引擎聚合头（`FFrameExtension` / `IPipeline` 等基础设施）")
D.Row("TextApi.h", "`MAHO_TEXT_API` —— 本模块的导出标签")
D.Row("<mutex>", "`mutable std::mutex` —— 目录的读写都要串行化（读也要，因为读会碰容器）")
D.Row("<string> / <string_view>", "目录拥有文本；查询入口一律 `string_view`（零拷贝）")
D.Row("<unordered_map>", "翻译目录：`查找键 → 文本`。**为什么这里是哈希而不是有序容器**："
                         "查找发生在每帧多次的 UI 求值路径上，键是拼接出来的字符串，哈希更合适")
D.Row("<utility>", "`std::move` —— 按值传入的字符串被**搬**进成员，省一次拷贝")

D.Card("预定义语言（namespace Culture）",
       "三个 `inline constexpr std::string_view` 常量 —— 语言代码是**约定字符串**"
       "（BCP-47 风格子集），不是枚举：省去了「枚举 ↔ 字符串」的双向映射，"
       "新语言不需要改这个头也能用（直接传 `\"fr-FR\"` 即可）。")
D.Table("常量", "值")
D.Row("Culture::English", "`\"en-US\"`")
D.Row("Culture::Chinese", "`\"zh-CN\"`")
D.Row("Culture::Japanese", "`\"ja-JP\"`")

D.Card("自由函数")
D.Table("签名", "说明")
D.Row("MAHO_TEXT_API FTextManager* GetTextManager()",
      "全局管理器访问点：`Initialize` 里发布、`Shutdown` 里清除（未上线时为 `nullptr`）。"
      "函数而非导出裸变量 —— 跨 DLL 才能保证拿到同一份目录")

D.Class("FText",
        Desc="本地化文本**句柄**：只存 `{命名空间, 键, 原文}`，不含翻译结果。\n"
             "**为什么它是可拷贝的值类型**：UI 树、组件、配置里到处要存文本，"
             "如果句柄是指针 / 引用，文本的生命周期就会变成所有人的负担；"
             "值语义让「持有文本」这件事零心智能负担（代价是三个字符串的拷贝，"
             "而它们通常很短且在 UI 构建期只拷一次）。\n"
             "**相等性只比 `{命名空间, 键}`**（不比原文）：同一句话换了个原文写法仍应视为同一个文本 —— "
             "这对「按文本键去重 / 做 diff」很有用。")
D.SetAccess("public")
D.Interface("FText() = default",
            "默认构造（全空）。**为什么不叫 None**：空文本是合法的中间状态，"
            "`Resolve()` 对它回退到空原文，渲染成一个空串，不会崩")
D.Interface("FText(std::string InNamespace, std::string InKey, std::string InSource)",
            "构造句柄：参数按值收 ⇒ 调用方可以 `std::move` 进来，实现内部再搬进成员，"
            "避免「按 const& 收、内部还是得拷一次」的多余拷贝")
D.Interface("[[nodiscard]] std::string_view GetNamespace() const", "命名空间（视图：成员字符串稳定）")
D.Interface("[[nodiscard]] std::string_view GetKey() const", "键")
D.Interface("[[nodiscard]] std::string_view GetSource() const",
            "原文 —— 翻译缺失时的回退文本，也常常被界面拿去当「调试名」")
D.Interface("[[nodiscard]] std::string Resolve() const",
            "按**当前语言**查翻译，缺失则回退原文。返回 `std::string`（不是视图）："
            "结果可能来自目录，而目录随时可能被 `AddTranslation` 改动，视图会悬空")
D.Interface("[[nodiscard]] bool operator==(const FText& Other) const",
            "只比 `{命名空间, 键}` —— 见类说明：同一个文本换了原文写法仍是同一个文本")

D.Class("FTextManager", Base="FFrameExtension, IPipeline<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>",
        Desc="本地化管理器 —— **一个帧**（挂 6 个 stage）：`Initialize` 里发布全局访问点并定下默认语言，"
             "`Shutdown` 里撤销。语言目录是运行期可变状态，交给有依赖排序的帧持有，"
             "「谁在谁之前设语言」才有确定性。\n"
             "**为什么默认语言是 `en-US`**：`CurrentCulture` 有初始值，"
             "因此**在任何语言被设置之前** `Resolve()` 也是安全的 —— "
             "它只会回退到原文，而不是未定义行为。\n"
             "**为什么查找要按 `(命名空间, 键, 语言)` 三元组**：目录键实际是把三者拼成一个字符串，"
             "这与「一个 FText 只能属于一个语言」的表达能力一致，也避免了嵌套容器。")
D.SetAccess("public")
D.Interface("MAHO_DECLARE_FRAME(FTextManager)",
            "声明帧身份（稳定字面量名字 + `CreateFrame` 工厂）—— 让别处可以按名字声明"
            "「我在文本目录就绪之后才查文本」")
D.Interface("[[nodiscard]] std::string_view GetCulture() const",
            "当前语言代码。**返回视图的语义**：它指向成员 `CurrentCulture`，"
            "在下次 `SetCulture` 之前有效 —— 调用方若要留存请自行拷贝")
D.Interface("void SetCulture(std::string InCulture)",
            "切换当前语言。**为什么按值收**：要存进成员，按值传让调用方可以搬移；"
            "切换本身**不触碰已有 `FText`** —— 它们在下一次 `Resolve()` 时自然查到新语言")
D.Interface("void AddTranslation(std::string_view InNamespace, std::string_view InKey, std::string_view InCulture, std::string Text)",
            "登记一条翻译（线程安全）。键由三者拼成 ⇒ **同键重复登记即覆盖**，"
            "这正是「热重载翻译文件」实现的方式")
D.Interface("void LoadTranslationsFromJson(std::string_view JsonText)",
            "从 JSON 数组批量装载翻译，形如：\n"
            "`[{\"Namespace\": \"...\", \"Key\": \"...\", \"Culture\": \"...\", \"Text\": \"...\"}, ...]`\n"
            "**为什么是 JSON**：翻译要能被非程序员编辑，且要能从构建流程 / 外部工具生成；"
            "JSON 是这类「人写、机器读」场景最省事的选择（解析走引擎固定的 Json 依赖）")
D.Interface("[[nodiscard]] const std::string* FindTranslation(std::string_view InNamespace, std::string_view InKey, std::string_view InCulture) const",
            "查一条翻译，缺失返回 `nullptr`（线程安全）。**为什么返回指针而不是 `optional<string>`**："
            "目录里的字符串已经是稳定存储，返回指针省一次拷贝；"
            "「可能没有」由 `nullptr` 表达即可（调用方只需判空）")
D.SetAccess("private")
D.Interface("void PreInitialize(FEngineBase&) override", "空：本帧不需要更早的时钟点")
D.Interface("void Initialize(FEngineBase& Engine) override",
            "发布全局访问点（`GetTextManager()` 从这里开始非空）；默认语言此时已是 `en-US`")
D.Interface("void PostInitialize(FEngineBase&) override", "空")
D.Interface("void PreShutdown(FEngineBase&) override", "空")
D.Interface("void Shutdown(FEngineBase& Engine) override",
            "撤销全局访问点。**目录的清空必须晚于所有使用者** —— 由依赖图保证"
            "（`FText::Resolve()` 需要目录还在）")
D.Interface("void PostShutdown(FEngineBase&) override", "空")
D.SetAccess("protected")
D.Interface("FTextManager() = default",
            "protected 构造：实例只由本模块的帧工厂 / 宿主流程创建，"
            "外部只能经 `GetTextManager()` 取用（保证「进程内唯一目录」）")
D.Field("mutable std::mutex Mutex",
        "保护目录的读写。**`mutable` 的原因**：查找是 `const` 成员，但仍需上锁 —— "
        "读一个可能被并发修改的容器同样要互斥")
D.Field("std::string CurrentCulture = \"en-US\"",
        "当前语言，**带默认值**（`Resolve` 在任何设置之前也安全：只回退原文）")
D.Field("std::unordered_map<std::string, std::string> Catalog",
        "翻译目录：拼好的 `(命名空间, 键, 语言)` → 文本。"
        "**为什么键是拼接字符串**：避免三层嵌套容器，`FindTranslation` 一次哈希即可命中")
