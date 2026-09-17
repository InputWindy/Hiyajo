# -*- coding: utf-8 -*-
# Timer 插件文档内容（由 Tools/plugin_docs.py 执行，docs_builder 以变量 D 注入）。

# ══════════════════════════════════════════════════════════════════════════════
# Public/TimerApi.h —— 模块导出标签
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/TimerApi.h", Title="TimerApi.h —— 模块导出标签",
         Desc="本模块的 DLL 导出标签。独立成头：只要「计时器访问器」声明的模块不必引入 `Timer.h`。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT`（引擎核心提供）")

D.Macro("MAHO_TIMER_API", "MAHO_EXPORT / MAHO_IMPORT（随构建开关切换）",
        Desc="编译 `Timer.dll` 时构建系统定义 `MAHO_TIMER_MODULE_EXPORTS` ⇒ `MAHO_EXPORT`；"
             "消费方为 `MAHO_IMPORT`。\n"
             "**为什么必须有**：`FTimer* GetTimer()` 跨 DLL 返回类型，"
             "`FTimer` 的虚表与删除析构符必须只在本模块生成；"
             "`dllimport` 把「消费方各生成一份」变成编译错误，避免卸载后 `delete` 静默崩。")

# ══════════════════════════════════════════════════════════════════════════════
# Public/Timer.h —— 分层作用域计时
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Timer.h", Title="Timer.h —— 分层作用域计时（服务层）",
         Desc="基于**栈**的作用域性能剖析：`FScopedTimer` 在构造时 `BeginScope`、析构时 `EndScope`，"
              "`FTimer` 把每次弹出累加进一棵按名字分层的计时树，`DumpToString()` 输出文本报告。\n"
              "**为什么用 RAII 作用域而不是手工开始 / 结束**：热路径上最怕的是「提前 return / 抛异常"
              "导致 EndScope 被跳过」，那会让计时栈永久错位。把一对调用绑在对象生命周期上，"
              "正确性由语言保证，调用点也短到可以随手插进任何函数。\n"
              "**为什么是树而不是打平的表**：`Render → Scene → Cull` 这种嵌套关系本身就有信息量"
              "（父层耗时包含子层），打平后会丢掉「谁被谁包含」，"
              "而树可以既打印自身又打印包含关系。\n"
              "**为什么用单调钟**：`std::chrono::steady_clock` 不受系统时间调整影响 —— "
              "用 `system_clock` 的话，一次 NTP 校时就能造出负耗时或巨大尖峰。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("<Maho.h>", "引擎聚合头（`FFrameExtension` / `IPipeline` 等）")
D.Row("TimerApi.h", "`MAHO_TIMER_API` —— 本模块的导出标签")
D.Row("<chrono>", "`steady_clock::time_point` —— **单调**时间点（不受系统时间调整影响）")
D.Row("<cstdint>", "`std::uint32_t Count` —— 调用次数的定宽表示")
D.Row("<map>", "子节点表：`名字 → FNode`（有序 ⇒ 报告输出顺序稳定，两次 dump 可 diff）")
D.Row("<string> / <string_view>", "节点拥有名字；`BeginScope` 入参用 `string_view`（零拷贝）")

D.Card("自由函数")
D.Table("签名", "说明")
D.Row("MAHO_TIMER_API FTimer* GetTimer()",
      "全局计时器访问点：`Initialize` 里发布、`Shutdown` 里清除（未上线时为 `nullptr`）。"
      "函数而非导出裸变量 —— 跨 DLL 才能保证累加到同一棵树")

D.Card("计时节点字段（FTimer::FNode）",
       "节点是**聚合**类型（无自定义构造），全部字段公开给本帧自己使用；"
       "这里列出它们是为了说明每棵计时树到底记住了什么。")
D.Table("字段", "含义")
D.Row("std::string Name", "节点名（作用域名；同名作用域复用同一节点，于是「调用一次」变成「累加一次」）")
D.Row("double TotalSeconds", "累计自身耗时（**不含子节点**的净值，便于看清本层开销）")
D.Row("double MaxSeconds", "单次最大耗时 —— 平均值会掩盖偶发尖峰，它不会")
D.Row("std::uint32_t Count", "调用次数（与 Total 一起才能得到平均值）")
D.Row("std::chrono::steady_clock::time_point Start", "本次进入的时间点（弹出时用它算差值）")
D.Row("FNode* Parent", "父节点 —— **裸指针**：树由 `Root` 唯一拥有，`Parent` 只是回溯链，不参与所有权")
D.Row("std::map<std::string, FNode> Children", "子节点表（按值持有 ⇒ 整棵树随 `Root` 一次析构；"
                                               "`map` 保证遍历顺序稳定）")

D.Class("FTimer", Base="FFrameExtension, IPipeline<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>",
        Desc="计时器本体 —— **一个帧**（挂 6 个 stage）：`Initialize` 发布全局访问点，"
             "`Shutdown` 撤销。\n"
             "**为什么是帧而不是纯静态**：计时树对当前作用域栈（`Current`）有状态，"
             "而这份状态必须只有一份、且有明确的生命周期；"
             "交给有依赖排序的帧持有，「谁在谁之前计时」才有确定性。\n"
             "**为什么要 `Reset`**：计时数据是「一段区间」的采样，"
             "看一帧 / 一段加载的耗时必须能清零 —— 否则累计值随时间单调增长，读不出局部热点。\n"
             "**公平契约**：`BeginScope` 与 `EndScope` 必须配对（用 `FScopedTimer` 就没这个问题），"
             "不配对会让 `Current` 指针错位，之后的计时全部挂到错的父节点下。")
D.SetAccess("public")
D.Interface("MAHO_DECLARE_FRAME(FTimer)",
            "声明帧身份（稳定字面量名字 + `CreateFrame` 工厂）—— 让别处能按名字声明"
            "「我在计时器就绪之后才计时」")
D.Interface("void BeginScope(std::string_view Name)",
            "压入一个作用域：在 `Current` 的子表里按名字取（或新建）节点，"
            "把 `Current` 挪到它并把 `Start` 记为现在")
D.Interface("void EndScope()",
            "弹出当前作用域：累计本次耗时到 `TotalSeconds`、更新 `MaxSeconds` 与 `Count`，"
            "再把 `Current` 退回 `Parent`")
D.Interface("void Reset()",
            "清空全部累计数据（回到只有根节点的初始状态）—— 采样的起点由调用方决定")
D.Interface("[[nodiscard]] std::string DumpToString() const",
            "把计时树格式化成文本（毫秒），形如 `Render: 1.23 ms (n calls, avg, max)`。"
            "**为什么返回字符串而不是直接打印**：输出目的地由调用方决定"
            "（日志、编辑器面板、控制台），计时器不替它们选 sink")
D.SetAccess("private")
D.Interface("void PreInitialize(FEngineBase&) override", "空：本帧不需要更早的时钟点")
D.Interface("void Initialize(FEngineBase& Engine) override",
            "发布全局访问点（`GetTimer()` 从这里开始非空）")
D.Interface("void PostInitialize(FEngineBase&) override", "空")
D.Interface("void PreShutdown(FEngineBase&) override", "空")
D.Interface("void Shutdown(FEngineBase& Engine) override",
            "撤销全局访问点 —— 在所有计时使用者结束之后（依赖图保证）")
D.Interface("void PostShutdown(FEngineBase&) override", "空")
D.SetAccess("protected")
D.Interface("FTimer() = default",
            "protected 构造：实例只由本模块的帧工厂 / 宿主流程创建，外部只能经 `GetTimer()` 取用")
D.Nested("struct FNode", Kind="struct",
         Desc="计时树节点（聚合类型）：名字、累计 / 最大耗时、调用次数、进入时间点、父指针、子表。"
              "**为什么按值持有子节点（`map<string, FNode>`）**：整棵树因此是「一个 `Root` 就拥有一切」"
              "的所有权结构，析构不需要递归 delete；`Parent` 只作回溯用，是裸指针。")
D.Field("FNode Root{\"Root\"}",
        "树的根（固定叫 `Root`）。**为什么根是值成员**：它是零成本的哨兵节点，"
        "让「顶层作用域」不必写特例分支（`Current` 永远非空）")
D.Field("FNode* Current = &Root",
        "当前作用域指针（初始指向根）。**为什么用裸指针**：它指向整棵树里的某个节点，"
        "不拥有任何东西；用 `unique_ptr` / 共享指针反而会让「树的所有权唯一」这件事变得模糊")

D.Class("FScopedTimer",
        Desc="RAII 作用域计时器：构造即 `BeginScope(Name)`，析构即 `EndScope()`：\n"
             "`void Render() { Timer::FScopedTimer Scope(\"Render\"); /* 工作 */ }` —— "
             "进入即开始，离开自动结束。\n"
             "**为什么禁止拷贝与赋值**：两个对象对应一次 `BeginScope` / `EndScope`，"
             "拷贝会让「构造 / 析构」的次数与实际的进入 / 离开次数脱钩（多出或少掉一次弹出），"
             "计时栈随即错位 —— 因此直接在类型层面禁掉，而不是靠注释提醒。\n"
             "**为什么它自己不含数据**：所有状态都在 `FTimer` 那一棵树里，"
             "这个对象只是「进入 / 离开」这对动作的载体 ⇒ 它的大小可以忽略，"
             "放进热路径也不必担心额外开销。")
D.SetAccess("public")
D.Interface("explicit FScopedTimer(std::string_view Name)",
            "构造：`GetTimer()->BeginScope(Name)`。**`explicit`**：避免从 `const char*` 隐式造出"
            "一个临时作用域计时器（那会立刻析构、等于什么都没测）")
D.Interface("~FScopedTimer()",
            "析构：`GetTimer()->EndScope()`。**这是关键的一半** —— "
            "即便函数提前 return 或抛异常，栈展开也一定会走析构，弹出不会丢")
D.Interface("FScopedTimer(const FScopedTimer&) = delete",
            "禁拷贝构造：拷贝会产生「两个对象、一次进入」的失衡（见类说明）")
D.Interface("FScopedTimer& operator=(const FScopedTimer&) = delete",
            "禁赋值：赋值更是把两个不同生命期的对象混成一个，必须禁")
