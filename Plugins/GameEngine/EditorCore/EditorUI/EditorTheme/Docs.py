# EditorTheme 插件的内容脚本（plugin_docs.py 会 exec 本文件，docs_builder 已注入为 D）。
# 内容与磁盘上的 Public/*.h 逐字对应。

# ══════════════════════════════════════════════════════════════════════════════
# Public/EditorThemeApi.h —— 导出宏
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/EditorThemeApi.h", Title="EditorThemeApi.h —— 导出宏",
         Desc="本插件的导出开关：`MAHO_EDITORTHEME_API` 按 `MAHO_EDITORTHEME_MODULE_EXPORTS` "
              "在 `MAHO_EXPORT` / `MAHO_IMPORT` 之间切换。\n"
              "**当前没有类型挂它**：主题面板只经 DLL 工厂 `CreateFrame()` 按符号名被构造"
              "（面板类型没有别的消费者）。宏按惯例保留。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT` 两个底层标记")

D.Macro("MAHO_EDITORTHEME_API", "MAHO_EXPORT / MAHO_IMPORT（按 MAHO_EDITORTHEME_MODULE_EXPORTS 切换）",
        Desc="面板类当前不带它。**面板真正跨 DLL 调用的是 `ExampleEditorTheme.h` 的那批函数** —— "
             "它们由编辑器宿主的模块导出（见那一页），本头不重复声明任何东西。")

# ══════════════════════════════════════════════════════════════════════════════
# Public/EditorTheme.h —— 主题调参面板
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/EditorTheme.h", Title="EditorTheme.h —— 主题调参面板",
         Desc="`FEditorTheme` 把编辑器主题的**每一个**颜色槽都摊出来实时调。它挂 `IEditorPanel`"
              "（自身不占任何资源）并拥有一棵持久声明式 UI 树：`Update`（宿主 `NewFrame` 之前的声明期）"
              "重建它，宿主在帧内翻译。面板给每个槽位一个颜色编辑器（按主题的逻辑分组归段）、"
              "给每个数值 style 字段一个拖拽控件，编辑**实时**作用到 live style，"
              "Save / Load 把整块缓冲持久化成一个文本文件（**默认路径也正是启动时被恢复的那个**）。\n"
              "**控件值住在树节点上**：后端在翻译期原地改写它们，所以 `Update` 在声明之前先把它们回读进 "
              "`Colors` / `Styles`（Reset / Load 刚换过缓冲的那一帧例外 —— 那时**缓冲**才是真值）。\n"
              "**为什么这个面板只靠两个扁平缓冲就能完整映射主题**：主题的数据模型是「索引 + 数值」"
              "（见 `ExampleEditorTheme.h`），面板不需要认识任何 ImGui 结构体，"
              "只需要搬两块 float 缓冲并按索引问名字/分组/分量数。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("EditorThemeApi.h", "`MAHO_EDITORTHEME_API`：导出开关")
D.Row("Engine/Frame.h", "`MAHO_DECLARE_FRAME` / `FFrameExtension` / `IPipeline` —— 身份与 stage 列表")
D.Row("ExampleEditor.h", "`FExampleEditor`（宿主上下文）与 `IEditorInit` / `IEditorPanel` / `IEditorShutdown`")
D.Row("UIView.h", "`UI::FUIView`（持久树 + `Edit()`）与 `UI::FUIBuilder`（声明入口）")
D.Row("memory", "`std::unique_ptr<UI::FUIView> View`：面板拥有自己的树")
D.Row("vector", "`Colors` / `Styles` 两块扁平缓冲（长度由主题的槽位数决定）")

D.Card("本面板的原理约定（为什么这么写）")
D.Table("约定", "说明")
D.Row("`Init` 而不是首帧 `Update` 里加载",
      "默认路径取自 `GetEditorThemeDefaultPath()`（引擎决定，面板不自己拼），先铺内建默认、"
      "再尝试从持久文件覆盖，成功才 `Apply*` 到 live style —— "
      "于是「上次关编辑器时的外观」在第一次翻译之前就已经生效，不会闪一帧默认皮肤")
D.Row("回读 → 声明，而不是直接推值",
      "拖拽/取色中的真值在**节点**上，所以先回读进缓冲再按缓冲声明（用户编辑因此跨帧存活）；"
      "`Reset`/`Load` 换了缓冲的那一帧反向：置 `bBuffersAuthoritative`，"
      "本帧**不回读**（否则节点的陈旧值会把刚装好的主题压回去），并把缓冲写进节点")
D.Row("实时预览靠「回读与缓冲不一致」判定",
      "回读时逐个比较旧值与节点值，只要有一处不同就 `bDirty`；`bDirty && LiveApply` 才 `Apply*`。"
      "于是关掉 Live apply 就是纯粹的「改数值但不生效」，与旧版语义一致")
D.Row("事件订阅只在新节点上绑",
      "按钮长期复用（同 id 同类型 = 复用），所以订阅写在 `if (bNew)` 里 —— "
      "逐帧重绑会在节点上累积订阅，一次点击执行多次")
D.Row("分组按「同名连续段」切",
      "颜色与 style 都以 `Get*Group(i)` 分组，面板按名字变化的边界切段 —— "
      "**依赖主题表按组连续**。这是主题表自己的不变量（见 `Private/ImGuiTheme.cpp`），"
      "面板不额外排序（排序会让节点 id 的序号与主题索引脱钩）")

D.Class("FEditorTheme", Base="FFrameExtension + IPipeline<IEditorInit, IEditorPanel, IEditorShutdown>",
        Desc="主题调参面板本体。**`IEditorInit` 在这里承载真实工作**（装默认值 + 恢复持久主题），"
             "不是装饰：它跑在安装安全点上，早于第一帧；`IEditorPanel` 每帧重建树；"
             "`IEditorShutdown` 注销视图。它不持有任何后端 / RHI 资源，只持两块 float 缓冲与几个小缓冲。")
D.SetAccess("public")
D.Interface("void Init(FExampleEditor& Editor) override",
            "① 把默认路径拷进 `PathBuffer`；② `ReloadDefaults()` 铺内建默认；"
            "③ 尝试 `LoadEditorTheme(PathBuffer, ...)`，成功就 `ApplyEditorTheme` + "
            "`ApplyEditorThemeStyles` 作用到 live style。`Editor` 参数当前未用 —— "
            "主题是**上下文范围**的样式，不需要从这里够到任何编辑器状态")
D.Interface("void Update(FExampleEditor& Editor) override",
            "声明期（宿主 `NewFrame` 之前）：① `EnsureView`；② 按主题表长度 resize 两块缓冲；"
            "③ 回读（颜色 / 样式 / Live apply 勾选态 / 路径框文本），非权威帧才回读；"
            "④ 在 `Edit()` 里重建整棵树：顶部行（Live apply + 「N colors / N styles」信息）→ 分隔线 → "
            "滚动主体（颜色按组、每个槽位一个 `FUIColorEdit`；分隔线；样式按组、每个字段一个 `FUIDragFloat`，"
            "分量数取 `GetEditorThemeStyleArity`、步长 0.25）→ 分隔线 → 路径行 → "
            "Apply / Reset / Save / Load 四个按钮 → 状态文本（空则隐藏）"
            "（按源码顺序：整棵树在一个 `Edit()` 作用域里一次性声明完）；"
            "⑤ 末尾若 `bDirty && LiveApply` 就 `Apply*` 到 live style（实时预览）")
D.Interface("void Shutdown(FExampleEditor& Editor) override",
            "注销视图并 `View.reset()`。注册表只持裸指针（**从不删除**）⇒ 漏掉注销，"
            "下一次翻译会遍历到已销毁的树；UI 插件已走时（注册表为 nullptr）只需释放自己那份")
D.SetAccess("private")
D.Interface("void ReloadDefaults()",
            "按 `GetEditorThemeColorCount()` / `GetEditorThemeStyleCount()` 重铺两块缓冲，"
            "逐槽问 `Get*Default()` 要内建值。**默认值的唯一来源是引擎模块** —— "
            "面板只负责搬，不自己写死任何颜色，否则主题默认值会有两份、迟早不一致")
D.Interface("UI::FUIView* EnsureView(FExampleEditor& Editor)",
            "惰性建视图 + 注册：**UI 插件在 `Init` 时可能还没起来**，注册表为空就返回 nullptr、"
            "下一帧重试。外壳开窗（含宿主 dockspace id）由宿主通用循环做；"
            "渲染上下文用 `Editor.GetUIRenderContext()` 登记，与本插件节点 id 前缀无关")
D.Field("std::unique_ptr<UI::FUIView> View", "持久 UI 树，归本面板所有并登记在 UI 视图注册表里")
D.Field("std::vector<float> Colors",
        "扁平的 RGBA 缓冲，`count * 4` 个 float（长度见 `GetEditorThemeColorCount`）。"
        "**交错布局**：第 i 槽的四个分量在 `[i*4+0..3]`")
D.Field("std::vector<float> Styles",
        "扁平的样式缓冲，`styleCount * 2` 个 float（长度见 `GetEditorThemeStyleCount`）。"
        "标量字段只用 v0，第二个分量对 `vec2` 才有意义 —— 所以面板必须按 `GetEditorThemeStyleArity` "
        "决定给拖拽控件几个分量")
D.Field("bool LiveApply = true", "「Live apply」勾选态：改动是否立刻作用到 live style（回读自勾选框节点）")
D.Field("bool bBuffersAuthoritative = false",
        "被 Reset / Load 处理器置位：本帧**缓冲**才是真值，所以节点回读不能把它们的（陈旧）值压回来")
D.Field("char PathBuffer[512] = { 0 }",
        "主题文件路径（`Init` 用默认路径播种；用户可在路径框改）。Save / Load 都用它")
D.Field("char StatusBuffer[256] = { 0 }",
        "状态文本（Applied. / Reset to defaults. / Saved: … / Save failed. / Loaded: … / Load failed.）；"
        "空字符串时状态节点被隐藏（隐藏项不占布局）")

D.Card("跨 DLL 边界")
D.Table("符号", "说明")
D.Row("CreateFrame()（`Private/EditorTheme.cpp`）", "宿主子收集器按符号名装载的 C 导出")
D.Row("`ExampleEditorTheme.h` 的 16 个函数", "面板跨 DLL 调用它们（默认值 / 名字 / 分组 / 应用 / 存取）⇒ "
      "由**编辑器宿主**模块导出，本插件不需要自己的导出面")
D.Row("节点 id 前缀 `EditorTheme.`", "同级唯一即可（事件路由走根 → 目标的 Id 路径）；"
      "逐槽位的 id 由 `MakeId(\"EditorTheme.Color.\", i)` 这类前缀 + 主题索引合成")
