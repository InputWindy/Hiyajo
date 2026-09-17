# EditorConsole 插件的内容脚本（plugin_docs.py 会 exec 本文件，docs_builder 已注入为 D）。
# 内容与磁盘上的 Public/*.h 逐字对应。

# ══════════════════════════════════════════════════════════════════════════════
# Public/EditorConsoleApi.h —— 导出宏
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/EditorConsoleApi.h", Title="EditorConsoleApi.h —— 导出宏",
         Desc="本插件的导出开关：`MAHO_EDITORCONSOLE_API` 按 `MAHO_EDITORCONSOLE_MODULE_EXPORTS` "
              "在 `MAHO_EXPORT` / `MAHO_IMPORT` 之间切换。\n"
              "**当前没有类型挂它**：控制台面板只经 DLL 工厂 `CreateFrame()` 按符号名被构造，"
              "没有别的模块 include 它的类型，所以它不需要导出标记。宏按惯例保留。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT` 两个底层标记")

D.Macro("MAHO_EDITORCONSOLE_API", "MAHO_EXPORT / MAHO_IMPORT（按 MAHO_EDITORCONSOLE_MODULE_EXPORTS 切换）",
        Desc="面板类当前不带它（它只作 DLL 工厂的产物存在）。")

# ══════════════════════════════════════════════════════════════════════════════
# Public/EditorConsole.h —— 编辑器控制台面板
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/EditorConsole.h", Title="EditorConsole.h —— 编辑器控制台面板",
         Desc="`FEditorConsole` 把引擎**活的日志流**镜像进一个由宿主停靠的面板。"
              "面板拥有一棵持久声明式 UI 树（`UI::FUIView`）：`Update`（宿主开帧 UI **之前**的声明期）"
              "重建这棵树，宿主随后在帧内翻译它。**插件从不碰后端**。\n"
              "它挂 `IEditorInit` / `IEditorPanel` / `IEditorShutdown`：`Init` 订阅日志层的监听流"
              "（生产者线程往一把 mutex 保护的 `deque` 推，所以 `LogLine` 在**任意**发射线程上跑都是安全的）；"
              "每帧 `Update` 把 `deque` 排空成「每个可见行一个按等级着色的 `FUISelectable`」——"
              "点选 / Shift 点或拖拽扩选 / `Ctrl+C` 复制选区（`Ctrl+A` 先全选）、工具栏一个实时字符串过滤框，"
              "底部一条 **CVar 命令行**（回车提交；框里收键盘时浮出一列补全；`↑`/`↓` 在列表里走并把高亮名"
              "填回框；没有可补全项时 `↑` 重开同一浮层显示**最近 10 条**执行过的命令，`↑`/`↓` 改走它们；"
              "框未聚焦时箭头留给视图导航）。右键日志区在指针处开上下文菜单，第一项是 Clear"
              "（取代了旧的工具栏 Clear 按钮）。`Shutdown` 在日志层消失**之前**解绑。"
              "与每个编辑器组件一样，它不持有任何后端 / RHI 资源 —— UI 上下文归宿主。\n"
              "**控件值住在树节点上**：后端在翻译期原地改写它们，所以 `Update` 在声明之前先把它们回读进 "
              "`FilterBuffer` / `CvarBuffer`（建议行点选或命令执行那一帧例外：那时**缓冲**才是权威值）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("EditorConsoleApi.h", "`MAHO_EDITORCONSOLE_API`：导出开关")
D.Row("Engine/Frame.h", "`MAHO_DECLARE_FRAME` / `FFrameExtension` / `IPipeline` —— 身份与 stage 列表")
D.Row("ExampleEditor.h", "`FExampleEditor`（宿主上下文）与 `IEditorInit` / `IEditorPanel` / `IEditorShutdown` 三个 stage 接口")
D.Row("Log.h", "`GetLog()` / `FLogMessage` / `FSubscriptionID` / `ELogLevel` —— 订阅与行数据来源")
D.Row("UIView.h", "`UI::FUIView`（持久树 + `Edit()`）与 `UI::FUIBuilder`（声明入口）")
D.Row("deque", "`Lines`（生产者推尾、满则丢头）与 `History`（最近命令，尾最新）")
D.Row("memory", "`std::unique_ptr<UI::FUIView> View`：面板拥有自己的树")
D.Row("mutex", "`LinesMutex`：保护 `Lines` 免受生产者（`LogLine`）并发写的竞态")
D.Row("string", "行文本、命令历史、候选名、过滤器/命令行缓冲")
D.Row("vector", "`LineIds`（稳定的逐行节点 Id）与候选匹配结果")

D.Card("本面板的原理约定（为什么这么写）")
D.Table("约定", "说明")
D.Row("生产者 vs 帧线程",
      "`Init` 注册的回调在**任意发射线程**上跑，它只做「加锁 + 推/丢」，不碰 UI；"
      "唯一读 UI 的地方是帧线程的 `Update`。所以日志洪流只会撑大 `deque`（上限 `MaxLines`），"
      "**绝不会**在渲染线程上做分配式重活")
D.Row("头丢弃要补偿滚动",
      "`MaxLines` 触发丢头时累加 `DroppedCount`，帧线程在快照时结清它 —— "
      "于是「已经向上滚在读旧日志」的用户不会被每次丢头往上顶（旧版靠 `ImGui` 的自动滚动，语义相同）")
D.Row("读上一帧的命中状态",
      "树的事件不携带指针位置，所以「拖拽扩选」「悬停归属」「滚动是否贴底」全都按**上一帧翻译写回**的节点状态判定；"
      "这与旧版在 `BeginChild` 之后读 `IsItemHovered` / `GetScrollY` 的时序等价")
D.Row("两个列表互斥",
      "补全列表与历史列表复用同一个浮层：补全开着时 `↑` 走候选，否则 `↑` 重开历史 —— "
      "所以「点一行」「回车执行」「失焦」都要显式关掉另一个，否则两个列表会同时想占同一个浮层")
D.Row("`↑` 的候选过滤词要钉住",
      "走动会把**整条候选名**填回命令框；若继续拿框里的文本当过滤词，候选立刻只剩它自己、"
      "再被「已经是完整名字」的规则清空 —— 列表当场塌掉。故进入走动时把过滤词钉进 `SuggestNeedle`")
D.Row("提交后要把焦点要回来",
      "后端提交会让输入框退出编辑态（回车即离开），此后 `↑`/`↓`（声明作用域 `NodeActive`）"
      "与继续打字都不再轮到它。所以提交/点选后置 `CvarPendingFocus`，下一帧翻译时把键盘交回命令行")

D.Class("FEditorConsole", Base="FFrameExtension + IPipeline<IEditorInit, IEditorPanel, IEditorShutdown>",
        Desc="控制台面板本体：`FFrameExtension` 声明层 + 按源码顺序挂三个 stage 的有序列表。"
             "**`IEditorInit` 是它的必需品**（不是可选装饰）：订阅必须在图形装载安全点建立、"
             "在卸载安全点解除 —— 否则日志层先走，回调就成了跨模块的悬垂订阅。\n"
             "它不持有任何后端 / RHI 资源：一棵树 + 一个受锁保护的日志缓冲 + 一堆单帧/跨帧的交互位。")
D.SetAccess("public")
D.Interface("void Init(FExampleEditor& Editor) override",
            "订阅日志层的监听流（幂等：已有订阅直接返回）。回调只做「加锁 → 满则丢头并累加 `DroppedCount` → 推尾」，"
            "所以 `LogLine` 在任何线程上都是安全的。日志层未起来时不订阅（不报错）")
D.Interface("void Update(FExampleEditor& Editor) override",
            "声明期（宿主 `NewFrame` **之前**）：只重建本视图的树，不碰后端。这一帧的全部工作顺序："
            "① `EnsureView`；② 回读过滤框 / 命令行文本 + 两个**活跃位**；③ 算大小写无关的过滤词；"
            "④ 在锁下快照可见（过滤后）的行并结清 `DroppedCount`；⑤ 跑挂起的动作"
            "（回车提交的 CVar 命令 / `Ctrl+C` 复制 / `Ctrl+A` 全选）；⑥ 读上一帧的行高、滚动是否贴底、"
            "拖拽扩选、候选匹配；⑦ 在 `Edit()` 里重建整棵树（工具栏 + 日志区 + 命令行 + 浮层 + 上下文菜单）")
D.Interface("void Shutdown(FExampleEditor& Editor) override",
            "解绑日志监听 + 注销视图。顺序不能反、也不能省：注册表只持裸指针（**从不删除**），"
            "日志层的监听表同样持有本对象的回调 —— 任何一个漏解都是跨模块悬垂")
D.SetAccess("private")
D.Nested("FLogEntry", Kind="struct",
         Desc="一条被缓冲的控制台日志。字段：`ELogLevel Level`、`std::string Category`、`std::string Message`。"
              "**时间戳在入队时本地捕获**（如果将来要显示）—— `FLogMessage` 本身只带 Level/Category/Message，"
              "缓冲必须自给自足")
D.Field("static constexpr std::size_t MaxLines = 4096", "行缓冲上限；满了丢头（并累加 `DroppedCount` 供滚动补偿）")
D.Field("static constexpr std::size_t MaxHistory = 10", "命令历史上限（`deque` 尾最新；执行新命令时从头裁）")
D.Interface("UI::FUIView* EnsureView(FExampleEditor& Editor)",
            "惰性建视图 + 注册：**UI 插件在 Init 时可能还没起来**，注册表为空就返回 nullptr、下一帧重试。"
            "外壳开窗（含宿主 dockspace id）由宿主通用循环做；渲染上下文用 `Editor.GetUIRenderContext()` 登记")
D.Interface("void ExecuteCvarLine()",
            "跑命令行：把 `\"name [value]\"` 拿去查 ConsoleVariable 注册表 —— 只有名字就打印当前值、"
            "带值就设置。结果**回显进日志缓冲本身**（`Category = \"CVar\"`），与日志层同一条数据流，下一帧可见。"
            "顺带把执行行记进 `History`（与上一条相同不重复记），并把补全走动状态整体作废")
D.Interface("void FillFromList(const std::string& Text)",
            "把一行文本写回命令行：缓冲变成**本帧的权威值**（挡住节点陈旧文本的回读）、"
            "该文本记成「刚选过的名字」（避免补全列表压在自己那一行上重开）、并让命令框把键盘要回去。"
            "历史行点选与候选行点选、以及 `↑`/`↓` 的走动都走它")
D.Interface("void FillFromHistory()",
            "用 `History[HistoryIndex]` 填命令行（`↑`/`↓` 与行点选共用）；同时关掉补全列表 —— "
            "两个列表互斥")
D.Interface("void StepHistory(int Step)",
            "按 `Step`（-1 = 更旧、+1 = 更新）走命令历史。**关着的列表（-1）被「更旧」的一步打开**，"
            "落在最新一条上；两端夹住。只在「过滤框正在收键盘」或「历史为空」时拒绝 —— "
            "命令框自己有没有在收键盘，引擎已在派发前挡掉（两条 `↑`/`↓` 快捷键声明在命令框上，"
            "默认作用域 `NodeActive`），所以命令框闲置时箭头绝不会翻历史")
D.Interface("void StepSuggest(int Step, const std::vector<std::string>& Matches)",
            "在候选 `Matches` 里按 `Step` 走动：进入时**钉住过滤词**并落在最后一行（`↑`）或第一行（`↓`），"
            "两端夹住，每走一步把高亮名填回命令框。**列表保持开着** —— 走动不是挑选")
D.Field("std::unique_ptr<UI::FUIView> View", "持久 UI 树，归本面板所有并登记在 UI 视图注册表里")
D.Field("std::vector<UI::FUIName> LineIds", "稳定的逐行节点 Id（按行号建一次，之后复用）：行数变化不改变既有行的身份")
D.Field("std::mutex LinesMutex", "保护 `Lines`，避免生产者（`LogLine`）与帧线程的快照发生竞态")
D.Field("std::deque<FLogEntry> Lines", "日志行环形缓冲（推尾、满丢头），上限 `MaxLines`")
D.Field("FSubscriptionID ListenerId = 0", "日志层订阅句柄；0 = 未订阅（`Init` 幂等靠它）")
D.Field("int SelAnchor = -1", "选区起点行（锚），-1 = 无选区")
D.Field("int SelEnd = -1", "选区终点行，-1 = 无选区。行选中用「锚 + 终点」表达区间："
        "点选一行设锚=终点；Shift 点或拖拽只移动终点")
D.Field("std::size_t DroppedCount = 0",
        "自上一帧快照以来因 `MaxLines` 被丢掉的头部行数。帧线程在锁下消费它来把滚动视图往下挪，"
        "于是向上滚着读旧日志的人不会被每次丢头往上顶。**由生产者写**")
D.Field("char FilterBuffer[256] = { 0 }",
        "工具栏实时过滤框文本（大小写无关的子串匹配；空 = 全显示）。每次声明前从过滤节点回读")
D.Field("char CvarBuffer[256] = { 0 }",
        "底部 CVar 命令行文本。执行会对 ConsoleVariable 注册表跑 `\"name [value]\"` 并把结果回显进日志面板。"
        "每次声明前从命令节点回读（建议行点选那一帧除外）")
D.Field("bool CvarAuthoritative = false",
        "建议行被点选时置位：本帧**缓冲**（而非节点）是真值 —— 所以既不回读节点的陈旧文本，"
        "又必须把缓冲写进节点")
D.Field("char CvarPickedName[256] = { 0 }",
        "最后一次从补全列表选中的名字。框里正好是这个文本时列表**不得自动重开**："
        "点选后框仍持有键盘焦点（后端也还记得活跃项），单凭「活跃 + 非空文本」下一帧就会重开列表，"
        "用户永远离不开它（点哪行都重开，看起来像卡死）。改动文本即自动作废；点另一行则覆盖它")
D.Field("bool CvarRunRequested = false",
        "命令框回车提交时置位。宿主在 `Update` **之前**抽干事件，回调那一刻缓冲还没同步 —— "
        "所以命令在 `Update` 里、紧跟回读之后执行")
D.Field("bool CvarEditing = false",
        "命令行当前是否握着键盘（命令节点的活跃位，每帧回读）。命令框上的 `↑`/`↓` 是默认作用域 "
        "`NodeActive`，引擎已经会在框不活跃时拒绝派发（那时箭头属于视图导航）；这一位是让两个列表老实："
        "补全按它开合、历史在它掉下时关闭")
D.Field("bool FilterEditing = false",
        "工具栏过滤框的同一个活跃位。`NodeActive` 之外的**第二道闸**：过滤框活跃时命令框的 `↑` "
        "本来也不会触发，但显式守卫让 `StepHistory` 的意图一眼可读")
D.Field("bool CvarPendingFocus = false",
        "建议行点选或回车提交时置位：命令节点下次翻译会向后端要键盘焦点（`RequestKeyboardFocus()`），"
        "于是光标留在命令行、可以接着打字。没有它，提交会把框踢出编辑态，"
        "而声明作用域为 `NodeActive` 的 `↑`/`↓` 也随之失效，用户得再点一下框")
D.Field("bool CopyRequested = false",
        "日志面板 `Ctrl+C` 快捷键置位；`Ctrl+A` 只把选中区间拉到全部可见行，"
        "所以「复制全部」= `Ctrl+A` 之后 `Ctrl+C`。真正的工作在 `Update` 里做 —— "
        "只有那里才有本帧的可见行快照")
D.Field("bool SelectAllRequested = false", "`Ctrl+A` 快捷键置位：把选中区间拉到全部可见行")
D.Field("bool CvarDropdownOpen = false",
        "是否该显示补全列表。它会跨越「点击落在」的那一帧（点击会在事件被抽干**之前**让框失活），"
        "只在框被清空、输入了完整名字、或失去焦点时才关闭")
D.Field("std::deque<std::string> History", "命令历史：最近 `MaxHistory` 条执行过的行，**尾最新**")
D.Field("int HistoryIndex = -1",
        "`↑` 列表的高亮行，-1 = 列表关着。行复用补全浮层（二者互斥）；最新一条在最下面，"
        "于是 `↑` 是「往回走、离开命令框」的方向")
D.Field("int SuggestIndex = -1", "补全列表被 `↑`/`↓` 走动时的高亮行，-1 = 未在走动")
D.Field("char SuggestNeedle[256] = { 0 }",
        "走动进入补全列表时**钉住**的过滤词（见上面「过滤词要钉住」一条）。空 = 未在走动")
D.Field("int SuggestOffset = 0",
        "走动时候选列表的窗口原点（最上可见行的下标），**故意跨帧保留**。"
        "每帧按「把高亮钉在窗口边缘」重算：若让内容跟着按键滑动，`↑`（行 -1）会拖着窗口走，"
        "屏幕上整列表反而**下移**，`↑` 与 `↓` 看起来一模一样。保留它则高亮在窗口内移动、"
        "只在要离开窗口时窗口才挪一行。随走动状态一起复位")
D.Field("int PendingStep = 0",
        "`↑`/`↓` 快捷键回调记下的步子。回调在事件抽干期跑、**早于** `Update` 收集候选，"
        "所以这一步属于哪个列表（补全还是历史）由 `Update` 决定")
D.Field("bool bContextMenuOpen = false",
        "日志区的上下文菜单（取代旧的工具栏 Clear 按钮）：右键日志面板即打开。"
        "后端在右键发生的那一帧把右键与指针位置写进面板运行期状态（`EUIInputFlags::ContextMenu`），"
        "`Update` 在**下一帧**读到，与上面那些命中状态的读法完全一致")
D.Field("UI::FUIVector2 ContextMenuAnchor{}", "上下文菜单的锚点（后端写回的指针位置）")

D.Card("跨 DLL 边界")
D.Table("符号", "说明")
D.Row("CreateFrame()（`Private/EditorConsole.cpp`）", "宿主子收集器按符号名装载的 C 导出")
D.Row("日志层订阅（`ListenerId`）", "订阅表里存着指向本对象的回调 ⇒ 卸载期必须先解绑（见 `Shutdown`）")
