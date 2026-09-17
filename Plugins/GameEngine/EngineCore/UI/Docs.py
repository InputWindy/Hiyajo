#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""UI 插件的文档内容。

运行器（Tools/plugin_docs.py）把 docs_builder 作为变量 D 注入本文件 —— 只声明，不 import。

按 UI.h 的总览顺序分节声明：公开头（UIApi → UIViewRegistry）→ 组件类型（Widgets/）→
私有实现（Private/，不在公开头里出现 imgui.h 的那几件）。
"""

# ══════════════════════════════════════════════════════════════════════════════
# Public/UIApi.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/UIApi.h", Title="UIApi.h —— 插件导出标记",
         Desc="只有导出标记 `MAHO_UI_API`。本插件的大部分实体（`FUIBuilder` 及其全部组件子类、"
              "`IUITranslator`、`FUIView` …）都由**别的模块**构造与持有（编辑器面板、游戏侧系统），"
              "而这些模块的生命周期与 UI.dll 并不整齐重合。把类型标成 DLL 接口意味着"
              "vftable 与删除析构被钉在本模块，消费者只能通过导入表引用 —— "
              "这是「组件树节点在别处 new、在这里 delete」能成立的前提。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT` 原语；引擎 core 是插件唯一的下层依赖")

D.Macro("MAHO_UI_API", "MAHO_EXPORT / MAHO_IMPORT",
        "构建 UI.dll 时（`MAHO_UI_MODULE_EXPORTS`）取导出，其余消费者取导入")

# ══════════════════════════════════════════════════════════════════════════════
# Public/UITypes.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/UITypes.h", Title="UITypes.h —— UI 的公共值类型与五态",
         Desc="整个 UI 的**词汇表**：Id、向量、矩形、边距、五态、命中结果。"
              "它只依赖 Name 插件（`FUIName = Name::FName`）与定宽整型，"
              "因此可以被任何一层安全包含 —— 包括那些不该看见 ImGui 的面板插件。\n"
              "Id 复用 Name 的**内化字符串池**是关键决定：Id 比较是 O(1) 指针比较，"
              "而且一个 Id 就是一条稳定身份，天然适合当事件路径的项与锚点。"
              "同级唯一、跨级可重名（不同层级的同名节点靠路径消歧）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("UIApi.h", "导出标记")
D.Row("<Name.h>", "`Name::FName` —— Id 的载体（内化池，O(1) 比较）")
D.Row("<cstddef> / <cstdint>", "五态计数与枚举底层类型（跨后端传递的位掩码必须定宽）")

D.Alias("FUIName", "Name::FName", "组件 Id：复用 Name 插件的内化字符串池。同级唯一；跨级可重名"
                                  "（事件路由走路径，而不是靠全局唯一名）")

D.Struct("FUIVector2", Desc="二维向量。所有 UI 度量（尺寸、位置、指针坐标）都用它 —— "
                            "公开头不出现 ImGui 的 `ImVec2`，这是「公开头不含 imgui.h」的具体体现。")
D.SetAccess("public")
D.Field("float X = 0.f", "X")
D.Field("float Y = 0.f", "Y")

D.Struct("FUIRect", Desc="矩形（左上角 + 宽高）。它是布局、命中与绘制的共同货币："
                         "布局引擎算出的矩形、翻译器回写的运行期矩形、弹层锚点都用同一个类型，"
                         "于是「局部坐标 vs 屏幕坐标」的差别只能靠命名与注释区分，不会靠两个结构体。")
D.SetAccess("public")
D.Field("float X = 0.f, Y = 0.f, W = 0.f, H = 0.f", "左上角与宽高")
D.Interface("[[nodiscard]] FUIVector2 Min() const", "左上角")
D.Interface("[[nodiscard]] FUIVector2 Max() const", "右下角")
D.Interface("[[nodiscard]] bool Contains(FUIVector2 P) const", "点是否在矩形内（命中测试）")
D.Interface("[[nodiscard]] FUIRect Intersect(const FUIRect& O) const", "与本矩形求交（裁剪矩形）")
D.Interface("[[nodiscard]] bool IsEmpty() const", "宽或高 ≤ 0。**零尺寸是合法状态**"
                                                 "（点锚点、浮层节点），因此不能被当作错误")

D.Struct("FMargin", Desc="四边边距（顺序 Left/Top/Right/Bottom，与 CSS 一致）。"
                         "提供 1 / 2 / 4 参构造糖，因为「四边相同」「横竖各一」「逐边」三种写法在布局里都常见。")
D.SetAccess("public")
D.Field("float Left = 0.f, Top = 0.f, Right = 0.f, Bottom = 0.f", "四条边的宽度")
D.Interface("FMargin() = default", "全 0")
D.Interface("FMargin(float All)", "四边同值")
D.Interface("FMargin(float H, float V)", "水平 = H、垂直 = V")
D.Interface("FMargin(float L, float T, float R, float B)", "逐边给出")

D.Enum("EUIState", Base="std::uint8_t",
       Desc="组件的五个交互态。**枚举值即状态组数组下标**（`FUIStyle::States`），"
            "所以顺序不能改 —— 改顺序等于把所有既有样式的含义都挪位。"
            "`Count` 是哨兵，用于确定数组长度 `kUIStateCount`。")
D.Field("Normal", "常态")
D.Field("Hovered", "悬停")
D.Field("Pressed", "按下")
D.Field("Selected", "选中")
D.Field("Disabled", "禁用")
D.Field("Count", "哨兵：非状态，仅用于计数")

D.Macro("kUIStateCount", "static_cast<std::size_t>(EUIState::Count)",
        "状态组数组长度。由枚举哨兵推出，而不是写死 5 —— 加一个状态时只有枚举要改")

D.Struct("FUIHitResult", Desc="一次命中测试的结果：翻译阶段算出交互意图后回写到节点的运行期状态，"
                              "回调则留给所有者线程抽干执行（`FUIView::DrainEvents()`）。"
                              "**语义是「本帧发生了什么」，不是「现在是什么状态」** —— "
                              "所以它一次性、不跨帧累积。")
D.SetAccess("public")
D.Field("bool bHovered = false", "指针在本控件上")
D.Field("bool bPressed = false", "本帧按下")
D.Field("bool bClicked = false", "本帧完成一次点击")
D.Field("bool bSubmitted = false", "输入框本帧回车提交")
D.Field("bool bDragging = false", "本帧处于拖拽中")
D.Field("bool bReleased = false", "本帧释放")

# ══════════════════════════════════════════════════════════════════════════════
# Public/UIStyle.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/UIStyle.h", Title="UIStyle.h —— 颜色 / 分态覆盖 / 解析结果",
         Desc="样式分三层：**覆盖**（`FUIStateStyle`，字段全 `std::optional`，未置位 = 交给回退链下一级）、"
              "**实例样式**（`FUIStyle`，五个状态组 + 字体/图标快捷槽）、"
              "**解析结果**（`FUIResolvedStyle`，每帧每节点算一次的纯值）。\n"
              "为什么覆盖层用 `optional` 而不是「值 + 布尔」：回退是**逐项**的，"
              "「没声明」与「声明成某个值」必须能区分（例如 `Radius = 0` 是一个明确的覆盖，"
              "不是「没声明」），`optional` 让这个区分成为类型层面的事实而不是约定。\n"
              "解析结果刻意用**纯值**：`Font`/`Icon` 只解析到引用（`FUIName`），"
              "真正的渲染资源由后端按引用去解析 —— 于是样式解析不需要任何图形后端，"
              "可以脱 GPU 做自动化验证。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("UITypes.h", "`FUIColor` 之外的度量类型（`FMargin` / `FUIName` / `EUIState`）")
D.Row("<array>", "`std::array<FUIStateStyle, kUIStateCount>` —— 五态数组，长度编译期确定")
D.Row("<cstdint>", "`FUIColor::Hex` 的位打包入参")
D.Row("<optional>", "覆盖项的存在性：**回退链的判据**（见头说明）")
D.Row("<string_view>", "颜色等常量文本的入参（不复制）")

D.Struct("FUIColor", Desc="线性 RGBA 颜色。提供 Hex / WithAlpha / Lerp 三个常用操作 —— "
                          "都放在这里而不是散在调用点，因为「0xRRGGBB 怎么拆」这种约定只应有一处。")
D.SetAccess("public")
D.Field("float R = 1.f, G = 1.f, B = 1.f, A = 1.f", "四分量，0..1")
D.Interface("static FUIColor Hex(std::uint32_t RGB, float InA = 1.f)", "由打包整数 0xRRGGBB 构造（可选 alpha）")
D.Interface("[[nodiscard]] FUIColor WithAlpha(float InA) const", "换 alpha 的拷贝（不改自身）")
D.Interface("[[nodiscard]] FUIColor Lerp(const FUIColor& O, float T) const", "线性插值（动画 / 主题过渡）")
D.Interface("[[nodiscard]] bool operator==(const FUIColor& O) const", "逐分量精确比较"
                                                                     "（缓存判重用；不做 epsilon）")

D.Struct("FUIStateStyle", Desc="**一个状态组**里可覆盖的样式项。未置位（`optional` 为空）= "
                               "交给回退链的下一级。颜色/尺寸是**字面量**，字体/图标是**资源引用**"
                               "（`FUIName`）—— 两者走同一条回退链，因此字体也能逐状态换（如禁用态换灰体、"
                               "选中态换实心图标），而不需要另开一套机制。")
D.SetAccess("public")
D.Field("std::optional<FUIColor> Fill", "背景填充")
D.Field("std::optional<FUIColor> Stroke", "描边")
D.Field("std::optional<FUIColor> Text", "文本 / 前景")
D.Field("std::optional<float> StrokeWidth", "描边宽度")
D.Field("std::optional<float> Radius", "圆角")
D.Field("std::optional<float> FontSize", "字号")
D.Field("std::optional<FMargin> Padding", "内边距")
D.Field("std::optional<FUIName> Font", "字体资源引用（可逐状态换）")
D.Field("std::optional<FUIName> Icon", "图标资源引用（可逐状态换）")
D.Field("std::optional<float> IconSize", "图标边长（内容尺寸为 Content 时参与测量）")

D.Struct("FUIStyle", Desc="实例样式：五个状态组 + 两个「四态共用」的字体/图标快捷槽。"
                          "字段全部 optional —— `FUIStyle{}` 就是「什么都没覆盖」，"
                          "所以「清空样式」是一个零成本操作，`FTypeStyleCache` 的重建也就可以直接赋值空样式。")
D.SetAccess("public")
D.Field("std::array<FUIStateStyle, kUIStateCount> States", "五态状态组（下标即 `EUIState`）")
D.Field("std::optional<FUIName> Font", "`States[0..4].Font` 的便捷写法（只覆盖 Normal 之外的空项）")
D.Field("std::optional<FUIName> Icon", "同上，图标")
D.Interface("FUIStateStyle& operator[](EUIState S)", "按下标取状态组（写）")
D.Interface("const FUIStateStyle& operator[](EUIState S) const", "按下标取状态组（读）")
D.Interface("void SetFillAll(FUIColor C)", "五态同填充色")
D.Interface("void SetTextAll(FUIColor C)", "五态同文本色")
D.Interface("void SetPaddingAll(FMargin M)", "五态同内边距")
D.Interface("void SetFontAll(FUIName F)", "五态同字体")
D.Interface("void SetIconAll(FUIName I)", "五态同图标")
D.Interface("[[nodiscard]] bool IsEmpty() const", "无任何覆盖（缓存 / 短路判据）")

D.Struct("FUIResolvedStyle", Desc="解析结果：**纯值**，翻译期直接消费（每帧每节点算一次）。"
                                  "`Font`/`Icon` 只解析到「引用」，真正的渲染资源由翻译后端按引用去解析 —— "
                                  "这一层因此完全不认识任何图形后端，既利于脱 GPU 测试，"
                                  "也让「同一棵树在两个不同后端上解析出同样样式」成为可知的事实。")
D.SetAccess("public")
D.Field("FUIColor Fill{ 0, 0, 0, 0 }", "填充（默认透明 —— 未声明就不画底）")
D.Field("FUIColor Stroke{ 0, 0, 0, 0 }", "描边")
D.Field("FUIColor Text{ 1, 1, 1, 1 }", "文本")
D.Field("float StrokeWidth = 0.f", "描边宽度")
D.Field("float Radius = 0.f", "圆角")
D.Field("float FontSize = 14.f", "字号")
D.Field("FMargin Padding{}", "内边距")
D.Field("FUIName Font{}", "字体资源引用（None = 后端缺省字体）")
D.Field("FUIName Icon{}", "图标资源引用（None = 无图标）")
D.Field("float IconSize = 16.f", "图标边长")
D.Field("EUIState State = EUIState::Normal", "本节点本帧生效的状态组（诊断 / 调试绘制用）")

# ══════════════════════════════════════════════════════════════════════════════
# Public/UITheme.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/UITheme.h", Title="UITheme.h —— 全局主题 token 与换代广播",
         Desc="主题是样式回退链的**最后一站**：组件类型的默认样式与节点实例都没覆盖时，落到这里。"
              "它是可变对象（编辑器主题面板直接改它），因此需要一条「改完了」的通知："
              "`NotifyThemeChanged()` 递增**换代计数器**（`GetUIThemeStamp()`），"
              "组件库的静态默认样式缓存按计数器惰性重建 —— "
              "没有这条通知，旧 token 编出来的颜色会永久留在静态缓存里。\n"
              "字号走**档位集合**而不是连续值：字体图集按 `(字体引用, 档位)` 各烘一份，"
              "节点上的任意字号绘制前吸附到最近档（`SnapFontSize`）。"
              "代价是改档位集合要重启才生效（图集只在启动烘一次），"
              "收益是运行期不会因为一个临时字号把图集撑爆。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("UIApi.h", "导出标记（本头的每个类型/函数都跨 DLL）")
D.Row("UIStyle.h", "`FUIColor` / `FUIStyle`（主题的字段类型）")
D.Row("UITypes.h", "`FUIName` 资源引用")
D.Row("<Core/Delegate.h>", "`FSubscriptionID`（主题变更订阅票据）")
D.Row("<functional>", "订阅回调类型")
D.Row("<vector>", "字号档位集合")

D.Card("自由函数")
D.Table("签名", "说明")
D.Row("FUITheme& GetUITheme()",
      "取唯一主题实例。**与 `GetUIViewRegistry()` 同形：跨 DLL 走导出函数，没有 `static Get()`、"
      "没有 `TSingleton`** —— 头里只有声明，实例在本 DLL 内一处")
D.Row("FSubscriptionID SubscribeThemeChanged(std::function<void()> Handler)",
      "订阅主题变更（返回值供 Unbind）。面板据此重建自己的样式缓存")
D.Row("void NotifyThemeChanged()",
      "主题改完后调用：**广播变更并递增换代计数器**。改主题不调它 = 缓存不会重建")
D.Row("std::uint32_t GetUIThemeStamp()",
      "主题换代计数器。组件库的类型默认样式按它惰性重建（见 `FTypeStyleCache`）")
D.Row("float SnapFontSize(float Size)",
      "把任意字号吸附到最近档位。**不修改输入值**；档位集合为空时原样返回。"
      "跨档只记一条诊断 —— 因为字体图集里没有那一档，画不出来就只能退到最近的）")

D.Struct("FUITheme", Desc="全局主题 token：颜色、圆角、描边宽、缺省字号、字号档位集合、"
                          "以及一组资源引用（字体与内置图标）。带 `MAHO_UI_API` 是因为它**按引用跨 DLL**"
                          "（`GetUITheme()` 返回引用，改它的面板在另一个 DLL 里）。\n"
                          "字段分组本身是信息：`Control*` 是按钮/控件的中间灰，`Field*` 是输入框/滑条那一层近黑，"
                          "两支刻意分开 —— 合成的主题面板正是靠这个区分才不会把按钮底与输入框底画成同一个颜色。")
D.SetAccess("public")
D.Field("FUIColor PanelFill{ 0.10, 0.10, 0.11, 1 }", "面板底色")
D.Field("FUIColor PanelStroke{ 0.22, 0.22, 0.24, 1 }", "面板描边")
D.Field("FUIColor ControlFill{ 0.16, 0.16, 0.18, 1 }", "控件底（按钮 / 可点项）")
D.Field("FUIColor ControlHover{ 0.22, 0.22, 0.25, 1 }", "控件悬停")
D.Field("FUIColor ControlPress{ 0.28, 0.28, 0.31, 1 }", "控件按下")
D.Field("FUIColor FieldFill{ 0.027, 0.027, 0.027, 1 }", "字段底（输入框 / 滑条 / 拖拽 / 颜色选择，弹层窗口底）")
D.Field("FUIColor FieldHover{ 0.055, 0.055, 0.055, 1 }", "字段悬停")
D.Field("FUIColor FieldStroke{ 0.16, 0.16, 0.16, 1 }", "字段描边")
D.Field("FUIColor Accent{ 0.26, 0.59, 0.98, 1 }", "强调色（选中 / 焦点）")
D.Field("FUIColor Text{ 0.86, 0.86, 0.88, 1 }", "文本")
D.Field("FUIColor TextDisabled{ 0.45, 0.45, 0.47, 1 }", "禁用文本")
D.Field("FUIColor Border{ 0.28, 0.28, 0.30, 1 }", "通用边框")
D.Field("FUIColor Separator{ 0.24, 0.24, 0.26, 1 }", "分隔线（`FUISeparator` 的缺省色）")
D.Field("float Radius = 4.f", "缺省圆角")
D.Field("float StrokeWidth = 1.f", "缺省描边宽")
D.Field("float FontSize = 14.f", "缺省字号（必须是档位集合里的一档）")
D.Field("std::vector<float> FontSizeSteps{ 11, 13, 14, 16, 20, 28 }",
        "字号档位集合：图集按 `(字体引用, 档位)` 各烘一份。改集合 = 改主题，但**重启后才生效**"
        "（图集只在启动烘制）")
D.Field("FUIName Font{}", "全局缺省字体（None = 后端缺省字体）")
D.Field("FUIName FontMono{}", "等宽字体（日志 / 控制台）")
D.Field("FUIName IconChevron{}", "折叠 / 树展开箭头")
D.Field("FUIName IconCheck{}", "复选框勾")
D.Field("FUIName IconFolder{}", "内容浏览器目录")
D.Field("FUIName IconAsset{}", "内容浏览器资产")
D.Field("FUIName IconSearch{}", "搜索框")
D.Field("FUIName IconClose{}", "关闭 / 清空按钮")
D.Field("FUIName IconDragHandle{}", "拖拽手柄")

# ══════════════════════════════════════════════════════════════════════════════
# Public/UILayout.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/UILayout.h", Title="UILayout.h —— 布局参数（尺寸模式 + 排布）",
         Desc="节点的布局参数：方向、尺寸模式、外边距、内边距、间距、对齐、裁剪。"
              "摆放算法**只在 `FUILayoutEngine` 里实现一次**，节点不重写 —— "
              "本头因此只有数据与几个链式 setter，没有任何算法。\n"
              "尺寸用「模式 + 值」而不是单一浮点：`Content`（量内容）/ `Fixed` / `Fill`（父内容区减边距）/ "
              "`Fraction`（父内容区乘系数）四种语义差异很大，塞进一个 float 就得靠哨兵值区分"
              "（例如 0 表示 Fill、负数表示比例），那种约定无法自解释。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("UITypes.h", "`FMargin`（外边距 / 内边距都是它）")

D.Enum("EUIDirection", Base="std::uint8_t", Desc="主轴方向：子项是横向排还是纵向排。")
D.Field("Row", "横向排列")
D.Field("Column", "纵向排列（节点默认）")

D.Enum("EUISizeMode", Base="std::uint8_t",
       Desc="尺寸模式。`Content` = 量内容；`Fixed` = 绝对值；`Fill` = 父内容区减去 Margin；"
            "`Fraction` = 父内容区 × Value。")
D.Field("Content", "由内容决定（叶子量文本/图标，容器累加子节点）")
D.Field("Fixed", "固定像素值（用 `Value`）")
D.Field("Fill", "填满父内容区（`Value` 忽略）")
D.Field("Fraction", "父内容区的比例（`Value` 为系数）")

D.Struct("FUILength", Desc="「模式 + 值」的尺寸描述。四个静态工厂让声明处写出意图"
                           "（`FUILength::Fill()` 比构造一个 `{EUISizeMode::Fill, 0}` 清楚得多）。")
D.SetAccess("public")
D.Field("EUISizeMode Mode = EUISizeMode::Content", "尺寸模式")
D.Field("float Value = 0.f", "模式对应的数值（Content/Fill 时忽略）")
D.Interface("static FUILength Content()", "量内容")
D.Interface("static FUILength Fixed(float V)", "固定值")
D.Interface("static FUILength Fill()", "填满父内容区")
D.Interface("static FUILength Fraction(float F)", "按比例")

D.Enum("EUIAlign", Base="std::uint8_t", Desc="子项在交叉轴上的对齐（Stretch 会拉满整条交叉轴）。")
D.Field("Start", "起始边")
D.Field("Center", "居中")
D.Field("End", "结束边")
D.Field("Stretch", "拉伸填满（默认水平对齐）")

D.Struct("FUILayout", Desc="节点布局参数。默认值给的是「纵向堆叠、宽度拉伸、高度量内容」这种最常见形态，"
                           "因此大多数容器不需要写任何布局代码。四个链式 setter 只是 "
                           "`Layout().SetXxx` 的糖，写在节点声明之后。")
D.SetAccess("public")
D.Field("EUIDirection Direction = EUIDirection::Column", "主轴方向")
D.Field("FUILength Width{} / Height{}", "本节点尺寸（默认量内容）")
D.Field("FMargin Margin{}", "与本节点外部的间距（外）")
D.Field("FMargin Padding{}", "内容区内缩（内）")
D.Field("float Spacing = 0.f", "同级子项间距")
D.Field("EUIAlign HorizontalAlign = EUIAlign::Stretch", "水平对齐（默认拉伸）")
D.Field("EUIAlign VerticalAlign = EUIAlign::Start", "垂直对齐（默认起始边）")
D.Field("bool bClipChildren = false", "子内容超出本矩形则裁剪")
D.Interface("FUILayout& SetDirection(EUIDirection D)", "设主轴方向（链式）")
D.Interface("FUILayout& SetSize(FUILength W, FUILength H)", "设宽高（链式）")
D.Interface("FUILayout& SetPadding(FMargin P)", "设内边距（链式）")
D.Interface("FUILayout& SetSpacing(float S)", "设子项间距（链式）")

# ══════════════════════════════════════════════════════════════════════════════
# Public/UIEvent.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/UIEvent.h", Title="UIEvent.h —— 事件类型 / 事件集 / 快捷键",
         Desc="交互事件的两段式设计：**翻译线程只入队**（`FUIEventRecord`），"
              "**所有者线程抽干后广播**（`FUIView::DrainEvents()`）。"
              "这不是风格选择，而是唯一能成立的做法：翻译线程是渲染线程，"
              "而业务回调会改树（要取独占锁）—— 在翻译中直接回调就是翻译期间改正在被读的树。\n"
              "记录里带 **Id 路径**（根→目标）而**不带节点指针**：跨线程持有裸指针在结构变更后会悬垂，"
              "而路径在结构不变时是稳定的，所有者按路径查回节点即可。\n"
              "快捷键按 **chord 分组**派发（`FUIShortcutGroup`，键是 `FUIKeyChord::ToString()`）："
              "同一节点声明多条快捷键因此天然互不串台，回调本身也就不需要载荷 —— "
              "此前是共用一条多播，回调得自己从载荷里认领是哪条键。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("UIApi.h", "导出标记")
D.Row("UITypes.h", "`FUIName`（目标 Id 与路径项）")
D.Row("<Core/Delegate.h>", "`TMulticastEvent`（事件集与快捷键组就是它）与 `FSubscriptionID`")
D.Row("<functional> / <map> / <memory> / <string> / <string_view> / <vector>",
      "处理器签名、按 chord 分组的 `std::map`、组对象的 `unique_ptr`（Delegate 内含 mutex 不可移动）、"
      "文本载荷与 Id 路径")

D.Macro("kUIPayloadType", "inline constexpr char kUIPayloadType[] = \"MahoUIName\"",
        "拖放载荷的类型键（后端 payload type 字符串，须 ≤32 字符）。载荷内容是 `FUIName` 的字符串形式，"
        "所以「树内拖放」跨后端共用同一个键")

D.Enum("EUIEventType", Base="std::uint8_t",
       Desc="事件种类。它与各事件集里的 Delegate 一一对应 —— 记录先按类型走对某个节点，"
            "再由节点广播给该类型的订阅者。")
D.Field("Clicked", "点击（按钮 / 菜单项 / 可选项）")
D.Field("ValueChanged", "数值变化（滑块 / 拖拽）")
D.Field("Toggled", "勾选 / 折叠 / 树节点开合")
D.Field("TextChanged", "文本变化（输入框）")
D.Field("SelectionChanged", "选中项变化")
D.Field("DragStarted", "拖拽开始")
D.Field("DragDropped", "落下（载荷见 `FUIEventRecord::Payload`）")
D.Field("TreeNodeToggled", "树节点开合（树自己的事件通道）")
D.Field("PopupClosed", "弹层关闭")
D.Field("Submitted", "输入框回车提交")
D.Field("Shortcut", "声明式快捷键命中（见 `FUIKeyChord`）")

D.Enum("EUIModifiers", Base="std::uint8_t",
       Desc="修饰键位掩码（按位组合）。命中那一刻的键盘状态随事件一起送到所有者线程，"
            "业务在回调里读 `FUIBuilder::GetLastModifiers()`（多选 Shift/Ctrl 点选）。")
D.Field("None = 0", "无修饰键")
D.Field("Shift = 1u << 0", "Shift")
D.Field("Ctrl = 1u << 1", "Ctrl")
D.Field("Alt = 1u << 2", "Alt")

D.Card("自由函数")
D.Table("签名", "说明")
D.Row("constexpr EUIModifiers operator|(EUIModifiers A, EUIModifiers B)",
      "位或：组合修饰键。`constexpr` 让它在静态声明里也能用")
D.Row("constexpr bool HasModifier(EUIModifiers Set, EUIModifiers Flag)",
      "判断某个修饰键是否置位（位与）。集中一处，避免各处自己写 cast + `&`")

D.Enum("EUIKey", Base="std::uint8_t",
       Desc="**命名键**：没有字符可表达的键（↑/↓）。字符键走 `FUIKeyChord::Key`，二者二选一。"
            "只在后端有对应物理键时才有意义（v1 后端 = ImGui）。")
D.Field("None = 0", "无命名键")
D.Field("Up", "↑")
D.Field("Down", "↓")

D.Card("自由函数（命名键显示名）")
D.Table("签名", "说明")
D.Row("constexpr const char* EUIKeyName(EUIKey K)",
      "命名键的显示名（`FUIKeyChord::ToString` 用它）。返回裸指针：名字是静态字面量，不存在所有权问题")

D.Struct("FUIKeyChord", Desc="一条键盘快捷键：一个键 + 修饰键组合。字符键（A-Z / 0-9，ASCII，"
                             "大小写等价）填 `Key`，无字符的键（↑/↓）填 `Named` —— 两者二选一。"
                             "`Named` 排在最后是为了让既有的 `{ 'C', EUIModifiers::Ctrl }` "
                             "聚合初始化继续成立（加字段不动已有代码）。\n"
                             "`ToString()` 有双重身份：既是给用户看的写法（`\"Ctrl+Shift+C\"`），"
                             "也是**派发分组的查找键** —— 分组键与显示名是同一个函数，"
                             "于是不可能出现「分组键与显示名两套拼接规则」这种漂移。")
D.SetAccess("public")
D.Field("char Key = '\\0'", "字符键（大小写等价）；`\\0` 表示没有")
D.Field("EUIModifiers Mods = EUIModifiers::None", "修饰键")
D.Field("EUIKey Named = EUIKey::None", "命名键：只在 `Key == '\\0'` 时生效")
D.Interface("[[nodiscard]] bool IsNone() const", "既没有字符键也没有命名键（未设置）")
D.Interface("[[nodiscard]] static FUIKeyChord NamedKey(EUIKey In, EUIModifiers M = EUIModifiers::None)",
            "命名键的构造糖：省掉两个空位")
D.Interface("[[nodiscard]] std::string ToString() const",
            "显示名（`\"Ctrl+Shift+C\"` / `\"Up\"`）：既是派发分组的键，也是面板上显示的写法")

D.Enum("EUIShortcutScope", Base="std::uint8_t",
       Desc="快捷键的**派发作用域**：声明在哪个节点上，不等于「随时都该响应」。")
D.Field("NodeActive", "默认：只有**这个节点自己**正拿着输入时才匹配 —— "
                      "于是「输入框没进入编辑态就别响应 ↑/↓」是引擎语义，各面板不必自写闸门")
D.Field("Anywhere", "只要键盘焦点在本视图窗口、且当前没有输入框在收键盘就匹配。"
                    "容器级（面板）快捷键用它 —— 容器自己永远不会变成活跃项，否则那条 chord 永不命中")

D.Struct("FUIShortcutDecl", Desc="节点上的一条快捷键声明：键组合 + 作用域。"
                                 "翻译期按**声明序**逐条匹配，先命中者入队（顺序即优先级）。")
D.SetAccess("public")
D.Field("FUIKeyChord Chord{}", "键组合")
D.Field("EUIShortcutScope Scope = EUIShortcutScope::NodeActive", "作用域")

D.Struct("FUIEventRecord", Desc="翻译线程 → 所有者线程的一条事件。带 Id 路径（根→目标）以消歧同名节点，"
                               "**不含节点指针**：所有者线程按路径查树后再回调"
                               "（结构不变则路径稳定）。一个记录承载所有事件的载荷类型，"
                               "用不到的分量留在默认值 —— 与 `MInputEvent` 同一种取舍："
                               "多几个字节换来读代码时不必查表。")
D.SetAccess("public")
D.Field("FUIName Target{}", "目标节点 Id（= 路径末项）")
D.Field("std::vector<FUIName> Path", "根 → 目标的 Id 链（不含根）")
D.Field("EUIEventType Type = EUIEventType::Clicked", "事件类型")
D.Field("FUIName Payload{}", "拖放载荷（`DragDropped`）")
D.Field("float Value = 0.f", "滑块值 / 数值载荷")
D.Field("bool bFlag = false", "复选 / 选中 / 展开态")
D.Field("EUIModifiers Modifiers = EUIModifiers::None", "命中时的 Shift/Ctrl/Alt")
D.Field("std::string Text", "文本变更载荷")

D.Alias("FUIEventHandler", "std::function<void(FUIBuilder&)>", "点击类事件处理器（多播：同一事件可有多个订阅者）")
D.Alias("FUIFloatEventHandler", "std::function<void(FUIBuilder&, float)>", "数值事件处理器")
D.Alias("FUIBoolEventHandler", "std::function<void(FUIBuilder&, bool)>", "布尔事件处理器")
D.Alias("FUITextEventHandler", "std::function<void(FUIBuilder&, std::string_view)>", "文本事件处理器")
D.Alias("FUINameEventHandler", "std::function<void(FUIBuilder&, FUIName)>", "拖放载荷处理器")

D.Struct("FUIShortcutGroup", Desc="一条快捷键的订阅组：**一个 chord 一组**。"
                                  "命中该 chord 的事件只广播到这一组 —— 组本身就是身份，回调不需要载荷。")
D.SetAccess("public")
D.Field("TMulticastEvent<void(FUIBuilder&)> Handlers", "该 chord 的全部订阅者")

D.Struct("FUIEvents", Desc="节点事件集：**多播 Delegate**（Core 的 `TMulticastEvent`，header-only、线程安全）。"
                           "翻译线程**从不**执行它们 —— 只入队 `FUIEventRecord`，由所有者在 `DrainEvents()` 里广播。\n"
                           "它在 `FUIBuilder` 里是 `unique_ptr`（懒分配）：没订阅就不付 mutex + vector 的钱；"
                           "也正因为它内含 mutex（不可移动）而**不能**当节点的值成员。")
D.SetAccess("public")
D.Field("TMulticastEvent<void(FUIBuilder&)> Clicked", "按钮 / 菜单项")
D.Field("TMulticastEvent<void(FUIBuilder&, float)> ValueChanged", "滑块 / 拖拽")
D.Field("TMulticastEvent<void(FUIBuilder&, bool)> Toggled", "复选 / 折叠 / 树节点")
D.Field("TMulticastEvent<void(FUIBuilder&, std::string_view)> TextChanged", "输入框文本变化")
D.Field("TMulticastEvent<void(FUIBuilder&, std::string_view)> Submitted", "输入框回车提交")
D.Field("TMulticastEvent<void(FUIBuilder&, bool)> SelectionChanged", "选中项变化")
D.Field("TMulticastEvent<void(FUIBuilder&, FUIName)> DragDropped", "落点收到的载荷")
D.Field("TMulticastEvent<void(FUIBuilder&)> PopupClosed", "弹层关闭")
D.Field("std::map<std::string, std::unique_ptr<FUIShortcutGroup>> Shortcuts",
        "快捷键订阅：键 = `FUIKeyChord::ToString()`。值是 `unique_ptr` —— Delegate 内含 mutex（不可移动），"
        "只能指过去")

D.Alias("FUIEventSubscription", "FSubscriptionID", "订阅票据（Core 的别名，便于节点 API 读数）")

# ══════════════════════════════════════════════════════════════════════════════
# Public/UIResource.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/UIResource.h", Title="UIResource.h —— 资源解析注入 + 字体图集登记",
         Desc="UI 插件**不依赖 `Resource` / `Render`**：树里只放引用（`FUIName`），"
              "把引用变成渲染资源的能力由**拥有者注入**（先例：`FResourceSystem::SetReadback`）。"
              "这样 UI 的依赖方向保持干净，也让它能被没有任何渲染后端的测试宿主驱动。\n"
              "字体登记**必须带上下文记账**：编辑器与游戏各有独立 ImGui 上下文与图集，"
              "同一个 `ImFont*` 只能喂给造它的那个上下文。只按 `(引用, 档位)` 记账会让后烘的一侧"
              "覆盖前一侧的条目，另一侧随即拿到别家图集的字体（字形纹理坐标错乱）—— "
              "所以 `Context` 是键的一部分，`ImGuiContext*` 以不透明指针传入。\n"
              "「全烘」策略（N 已定）：启动时把「主题字体 × 档位」一次性烘完并登记，运行期不再改图集。"
              "因此 `BakeUIThemeFonts()` 必须在 `CreateContext()` 之后、**首次取图集数据之前**调用 —— "
              "图集在首次取数据时才真正烘成，之后新增的条目进不去本帧图集。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("UIApi.h", "导出标记（下面每个函数都跨 DLL）")
D.Row("UITypes.h", "`FUIName` 资源引用")
D.Row("<cstdint>", "`std::uintptr_t` 原生句柄（不能是 `void*`：它是「整数编号」语义，"
                   "便于在容器里当键）与尺寸")
D.Row("<functional>", "`FUIResourceResolver`")

D.Struct("FUIResolvedResource", Desc="一次解析的结果（ImGui 实现里 `NativeHandle` 即 `ImTextureID` / `ImFont*`）。"
                                     "`Name` 回指引用，供后端做缓存键与诊断 —— "
                                     "于是「这份资源是哪来的」在调试时不必靠猜。")
D.SetAccess("public")
D.Field("bool bValid = false", "是否可用（false = 暂未就绪，按缺省外观绘）")
D.Field("FUIName Name{}", "回指的引用（缓存键 / 诊断）")
D.Field("std::uintptr_t NativeHandle = 0", "原生句柄（不透明整数）")
D.Field("std::uint32_t Width = 0", "宽（像素）")
D.Field("std::uint32_t Height = 0", "高（像素）")

D.Alias("FUIResourceResolver", "std::function<FUIResolvedResource(const FUIName& Resource, bool bIsFont)>",
        "查询某项 `FName` 引用的渲染资源：`bIsFont = true` 查字体，`false` 查纹理"
        "（图标 / 图片 / 渲染目标镜像）。`bValid = false` 表示暂未就绪（加载中 / 不存在）："
        "后端按缺省外观绘，**不阻塞帧**")

D.Card("自由函数（解析器注入）")
D.Table("签名", "说明")
D.Row("void SetUIResourceResolver(FUIResourceResolver Resolver)",
      "渲染侧注入解析能力（先例：`FResourceSystem::SetReadback`）。UI 插件因此不依赖 "
      "`Resource` / `Render`：引用进树，能力由拥有者注入")
D.Row("bool HasUIResourceResolver()", "是否已注入（宿主可用它决定「这一帧能不能画资源」）")
D.Row("FUIResolvedResource ResolveUIResource(const FUIName& Resource, bool bIsFont)",
      "统一解析入口（后端调用）：**未注入解析器时返回 `bValid=false` 的结果，不抛不崩**。"
      "解析器在锁外调用，允许它回头再进 UI 的公开 API")

D.Card("自由函数（字体图集登记 —— 键是 (上下文, 字体引用, 档位)）")
D.Table("签名", "说明")
D.Row("void RegisterUIFont(void* Context, const FUIName& Font, float Size, void* NativeFont)",
      "登记一份已烘好的字体条目（`ImFont*` 以不透明句柄传入）")
D.Row("void ClearUIFonts(void* Context = nullptr)",
      "清空某个上下文的登记（**该 ImGui 上下文销毁前**调用；`Context` 为 nullptr 时清空全部）。"
      "不清会在上下文销毁后留下指向已释放图集的条目")
D.Row("void* FindUIFont(void* Context, const FUIName& Font, float Size)",
      "取已登记条目：先按 `(Context, Font, SnapFontSize(Size))` 精确命中，再退到 `(Context, Font, 0)` "
      "兜底（`Size <= 0` = 任意字号都用这一份）。未命中返回 nullptr，后端用 ImGui 缺省字体绘制")
D.Row("void BakeUIThemeFonts()",
      "启动全烘：把**当前 ImGui 上下文**按「主题字体 × 档位」各烘一份并登记（主题字体为 None 时即后端缺省字体）。"
      "由持有上下文的宿主在 `CreateContext()` 之后、首次取图集数据之前调用一次。"
      "命名字体（如主题 `FontMono`）由宿主自行再调 `RegisterUIFont(当前上下文, …)` 补登记")

# ══════════════════════════════════════════════════════════════════════════════
# Public/UIClipboard.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/UIClipboard.h", Title="UIClipboard.h —— 剪贴板能力注入",
         Desc="文本剪贴板是**平台能力**，但 UI 插件不依赖 `Platform`（否则任何用 UI 的地方都会被拖上窗口层）。"
              "于是走与资源解析同一条路：**能力由拥有窗口的宿主注入**，UI 只提供一个稳定的读写入口。"
              "未注入时读返回空串、写被忽略 —— 让「没有剪贴板的后端」成为合法配置而不是崩溃点。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("UIApi.h", "导出标记")
D.Row("<functional>", "两个处理器类型")
D.Row("<string> / <string_view>", "读返回 `std::string`（所有权给调用方），写入参用 `string_view`（不复制）")

D.Alias("FUIClipboardGetHandler", "std::function<std::string()>", "宿主注入的剪贴板读取")
D.Alias("FUIClipboardSetHandler", "std::function<void(std::string_view)>", "宿主注入的剪贴板写入")

D.Card("自由函数")
D.Table("签名", "说明")
D.Row("void SetUIClipboardHandlers(FUIClipboardGetHandler Get, FUIClipboardSetHandler Set)",
      "注入 / 覆盖处理器（传空清空）")
D.Row("std::string GetUIClipboardText()", "读剪贴板文本；**未注入时返回空串，不抛不崩**")
D.Row("void SetUIClipboardText(std::string_view Text)", "写剪贴板文本；未注入时忽略")

# ══════════════════════════════════════════════════════════════════════════════
# Public/UIRender.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/UIRender.h", Title="UIRender.h —— 翻译后端接口（IUITranslator）",
         Desc="**语义原语 → 后端绘制调用**的唯一接口。它公开有两个理由："
              "换后端（当前唯一实现是 ImGui），以及让**记录式翻译器**能用于布局/状态的自动化验证"
              "（无需 GPU 就能断言「这棵树算出的矩形与状态是什么」）。\n"
              "接口按能力分层，读起来就是翻译器干活的顺序："
              "**视图簿记**（Begin/EndView、显示区、原点）→ **资源解析**（字体/纹理）→ **测量** → "
              "**原生绘制基元**（矩形/文本/图标/图片）→ **交互控件**（语义等价 ImGui 同名控件，返回值即本帧结果）→ "
              "**区域与容器**（滚动区、折叠头）→ **弹出层**（提示、弹层）→ **拖放** → **焦点/快捷键**。\n"
              "控件返回值是 `FUIHitResult`（本帧发生了什么），而不是「现在的状态」—— "
              "状态由控件写在树上（`bValue` / `bOpen` 之类的引用形参）。\n"
              "公开头**不含 imgui.h**：ImGui 只出现在实现文件（`Private/UIImGuiTranslator.*`）里，"
              "所以本头的每个签名都只能用 `FUIRect` / `FUIName` / 原生句柄这些中立类型表达。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("UIApi.h", "导出标记（`IUITranslator` 是可被外部实现的接口）")
D.Row("UIEvent.h", "`FUIEventRecord` / `FUIKeyChord`（入队事件与快捷键查询）")
D.Row("UIResource.h", "`FUIResolvedResource`（解析结果按值传递）")
D.Row("UIStyle.h", "`FUIResolvedStyle`（每个绘制原语都收一份已解析样式）")
D.Row("UITypes.h", "`FUIRect` / `FUIName` / `FUIHitResult` / `FUIColor`")
D.Row("<cstdint> / <string> / <string_view>", "位掩码底层类型、文本与格式串")

D.Enum("EUITextAlign", Base="std::uint8_t", Desc="文本水平对齐（在给定矩形内）。")
D.Field("Left", "左对齐")
D.Field("Center", "居中")
D.Field("Right", "右对齐")

D.Enum("EUIScaleMode", Base="std::uint8_t", Desc="图片在矩形内的缩放模式（`FUIImage` 用）。")
D.Field("Stretch", "拉伸铺满（**可能变形**）")
D.Field("Fit", "保持比例完整装进（默认）")
D.Field("Fill", "保持比例铺满（超出部分裁剪）")

D.Enum("EUIInputFlags", Base="std::uint32_t",
       Desc="命中 / 裁剪 / 拖放的标记位（按位组合）。它决定翻译器对一个节点**做哪些事**，"
            "而不是「节点长什么样」—— 于是行为开关集中成一个位掩码，各组件只声明自己要哪几位。")
D.Field("None = 0", "什么都不做（纯绘制节点）")
D.Field("HitTest = 1u << 0", "参与指针命中（按钮 / 输入等）")
D.Field("Clip = 1u << 1", "自身矩形作为子节点的裁剪矩形")
D.Field("Scroll = 1u << 2", "允许滚动（内容超出时可滚动）")
D.Field("DragSource = 1u << 3", "可拖出")
D.Field("DropTarget = 1u << 4", "可落入")
D.Field("ContextMenu = 1u << 5", "自身矩形是右键菜单区域（右键命中回写 `FUIWidgetState`，"
                                 "见 `HitTestSecondary`）")

D.Card("自由函数（位掩码运算）")
D.Table("签名", "说明")
D.Row("EUIInputFlags operator|(EUIInputFlags A, EUIInputFlags B)", "位或：组合标记")
D.Row("EUIInputFlags operator&(EUIInputFlags A, EUIInputFlags B)", "位与：取交集")
D.Row("bool HasFlag(EUIInputFlags V, EUIInputFlags F)", "某位是否置位（翻译器的分支判据）")

D.Class("IUITranslator", Desc="翻译后端接口：把树的语义原语映射为后端的绘制/控件调用。"
                              "**它是纯虚接口**（没有数据成员），因为翻译器的实例生命周期是"
                              "「一次 `TranslateView`（或叠加层一帧）」，不跨帧复用 —— "
                              "因此它不需要状态护栏，只需要把能力列全。\n"
                              "唯一的状态类是**跨帧记忆**：翻译器一帧一实例，记不住上一帧，"
                              "所以需要跨帧事实的地方（弹层上一帧是否真的画出来过、滚动的实测值）"
                              "都通过参数进、通过引用/结构体出 —— 记忆寄存在树上。")
D.SetAccess("public")
D.Interface("virtual ~IUITranslator() = default", "虚析构：后端由宿主创建/销毁（可能跨 DLL）")
D.Interface("virtual void BeginView(FUIView& View, const FUIRect& DisplayRect)",
            "一帧翻译开始（后端可在此做帧簿记）。`DisplayRect` 是本帧视图显示区")
D.Interface("virtual void EndView(FUIView& View)", "一帧翻译结束（与 `BeginView` 对称）")
D.Interface("[[nodiscard]] virtual FUIRect GetDisplayRect() const",
            "本帧视图显示区（局部坐标）—— 锚点比例（`FUIPanel::SetAnchor` / `FUIViewShell`）的参照")
D.Interface("[[nodiscard]] virtual FUIVector2 GetScreenOrigin() const",
            "当前原点（局部坐标 → 屏幕坐标的偏移，随滚动区进栈变化）。"
            "后端据此把节点矩形换算成屏幕矩形回写 `FUIWidgetState::ScreenRect`")
D.Interface("virtual void EnqueueEvent(FUIEventRecord Record)",
            "翻译线程入队一条交互事件 —— **只入队，不执行回调**（回调归所有者线程 `DrainEvents()`）。"
            "组件用 `FUIBuilder::MakeEvent(Type)` 造记录（自带 Id 路径）")
D.Interface("virtual FUIResolvedResource ResolveFont(FUIName Font, float Size)",
            "字体资源：按 (Font, Size) 解析到字体图集条目；`Font` 为 None 时返回缺省字体")
D.Interface("virtual FUIResolvedResource ResolveTexture(FUIName Texture)",
            "纹理资源（图标 / 图片 / 渲染目标镜像）：先查渲染镜像池，未命中再问资源系统；"
            "仍未就绪时 `bValid=false`，本帧按占位外观绘 —— 不阻塞帧、不做重试风暴")
D.Interface("virtual FUIVector2 MeasureText(std::string_view Text, const FUIResolvedResource& Font, float Size)",
            "测量文本尺寸（内容尺寸计算的唯一来源）")
D.Interface("virtual FUIVector2 MeasureIcon(const FUIResolvedResource& Icon, float Size)", "测量图标尺寸")
D.Interface("virtual void PushDisabled() / PopDisabled()",
            "祖先禁用传播：后端映射 `BeginDisabled/EndDisabled`。**必须是栈式** —— "
            "禁用是祖先属性，子节点翻译本身并不知道自己「在禁用子树里」")
D.Interface("virtual void PushClip(const FUIRect& Rect) / PopClip()", "裁剪矩形的压/弹（同样必须成对）")
D.Interface("virtual void DrawRect(const FUIRect& Rect, const FUIResolvedStyle& S)", "填充 + 描边 + 圆角")
D.Interface("virtual void DrawText(const FUIRect& Rect, std::string_view Text, "
            "const FUIResolvedResource& Font, float Size, const FUIResolvedStyle& S, EUITextAlign Align)",
            "在矩形内按对齐画文本（样式给颜色，字体/字号单独给 —— 因为它们是解析出来的资源）")
D.Interface("virtual void DrawIcon(const FUIRect& Rect, const FUIResolvedResource& Icon, "
            "const FUIResolvedStyle& S)", "画图标（尺寸取矩形）")
D.Interface("virtual void DrawImage(const FUIRect& Rect, const FUIResolvedResource& Texture, "
            "const FUIColor& Tint, const FUIVector2& UV0, const FUIVector2& UV1)",
            "画图片（带色调与 UV 子区域 —— 图集 / 精灵图靠 UV 取片）")
D.Interface("virtual FUIHitResult WidgetButton(FUIName Id, const FUIRect& Rect, const FUIResolvedStyle& S)",
            "按钮（语义等价 ImGui 同名控件）")
D.Interface("virtual FUIHitResult WidgetCheckbox(FUIName Id, const FUIRect& Rect, bool& bValue, "
            "const FUIResolvedStyle& S)", "复选框（`bValue` 引用形参：交互结果直接写回树）")
D.Interface("virtual FUIHitResult WidgetSliderFloat(FUIName Id, const FUIRect& Rect, float& Value, "
            "float Min, float Max, std::string_view Format, const FUIResolvedStyle& S)", "浮点滑块")
D.Interface("virtual FUIHitResult WidgetInputText(FUIName Id, const FUIRect& Rect, std::string& Text, "
            "std::string_view Hint, std::size_t MaxLength, bool bMultiline, const FUIResolvedStyle& S)",
            "文本输入（单行 / 多行）")
D.Interface("virtual FUIHitResult WidgetSelectable(FUIName Id, const FUIRect& Rect, bool bSelected, "
            "const FUIResolvedStyle& S)",
            "可选项：`bSelected` **只作为输入**（语义等价 ImGui::Selectable 的 selected 形参）—— "
            "选中态由树（`FUIBuilder::SetSelected`）决定并已在样式里解析，后端不再回写")
D.Interface("virtual FUIHitResult WidgetDragFloat(FUIName Id, const FUIRect& Rect, float* Values, "
            "int Components, float Speed, std::string_view Format, const FUIResolvedStyle& S)",
            "拖拽数值（1..4 分量，语义等价 ImGui::DragFloatN）：直接改写 `Values`")
D.Interface("virtual FUIHitResult WidgetColorEdit(FUIName Id, const FUIRect& Rect, float* RGBA, "
            "const FUIResolvedStyle& S)", "颜色编辑（RGBA 四分量）：直接改写 `RGBA`")
D.Interface("virtual bool BeginScrollRegion(FUIName Id, const FUIRect& Rect, "
            "const FUIScrollRequest& Request)", "进滚动区域，返回内容是否可见。`Request` 是待应用的滚动请求"
                                                "（贴底 / 显式置量）")
D.Interface("virtual FUIScrollInfo EndScrollRegion()", "出滚动区域：返回**实测**滚动量，"
                                                      "写回节点的运行期状态（下一帧贴底判定用）")
D.Interface("virtual FUIHitResult WidgetCollapsingHeader(FUIName Id, const FUIRect& Rect, bool& bOpen, "
            "const FUIResolvedStyle& S)", "可折叠分组头（`bOpen` 引用形参）")
D.Interface("[[nodiscard]] virtual bool HitTestSecondary(const FUIRect& Rect, FUIVector2& OutPos)",
            "右键（次要键）**区域**命中：指针落在 `Rect` 内且本帧按下了右键。"
            "纯几何判定、不走 item 命中 —— 滚动容器自己的 item 会被内容子窗口挡掉（子窗口在上），"
            "而右键菜单要的恰是「整个矩形」这层含义。`OutPos` 是命中那一刻的指针位置（视图局部坐标），"
            "可直接当弹层锚点")
D.Interface("virtual bool BeginTooltip(FUIName Id, const FUIRect& Anchor, bool bFollowMouse, "
            "const FUIResolvedStyle& S)",
            "悬停提示：返回 true 时后端已开提示窗口，调用方在窗口内摆子树后 `EndTooltip`。"
            "`S` 是提示窗**自身**样式：提示窗是第二个窗口，底/边框/圆角由窗口样式决定"
            "（调用方自己的绘制不覆盖那个窗口），故声明侧的值必须由后端压进去才落到屏上")
D.Interface("virtual void EndTooltip()", "关提示窗")
D.Interface("virtual bool BeginPopup(FUIName Id, bool bOpen, bool bWasShown, "
            "const FUIPopupAnchor& Anchor, bool bModal, const FUIRect& ContentBox, "
            "const FUIResolvedStyle& S)",
            "弹层：返回 true 时后端已开始弹层，调用方摆子树后 `EndPopup()`。`bOpen` 是树的期望状态；"
            "`bWasShown` 是**上一帧后端是否真的画出了它**（后端的跨帧记忆，调用方持有 —— "
            "翻译器一帧一实例记不住）：两者一起构成开合边沿，上升沿开、下降沿关。"
            "用户自己关掉（点外部 / Esc）时「树要开但后端已关」，后端据 `bWasShown` 不开新窗、"
            "如实报 false，调用方落回 `bOpen=false`。少了这份记忆就是每帧重新 `OpenPopup`："
            "弹层反复被当成刚出现，且「点外部关掉 → 下一帧又弹」永不可关。"
            "`ContentBox` 是调用方摆子树用的内容矩形（弹层自身局部坐标，含自身内边距偏移）："
            "落位要用它决定「贴锚点下沿还是翻到上沿」—— 同样因为翻译器记不住上一帧尺寸，"
            "故由调用方在开窗**之前**量好")
D.Interface("virtual void EndPopup()", "关弹层")
D.Interface("virtual bool BeginDragSource(FUIName Id, std::string_view PayloadType, "
            "std::string_view Payload, std::string_view PreviewText)",
            "开始拖放源（内容浏览器 / 资产拖拽复用同一套载荷）")
D.Interface("virtual void EndDragSource()", "结束拖放源")
D.Interface("virtual bool IsDropTarget(FUIName Id, std::string_view PayloadType, "
            "std::string* OutPayload)", "是否落在本节点上；命中时把载荷写进 `OutPayload`")
D.Interface("virtual void SetKeyboardFocus(FUIName Id)", "把键盘焦点交给某节点")
D.Interface("virtual bool HasFocus(FUIName Id) const", "某节点是否持有键盘焦点")
D.Interface("virtual bool IsShortcutPressed(const FUIKeyChord& Chord)",
            "声明式键盘快捷键查询（翻译期由声明了快捷键的节点调用）：本帧命中该组合返回 true。"
            "仅在键盘焦点落在本视图窗口（含子窗口）且当前无文本输入时命中 —— 否则打字会误触发。"
            "命名键（`EUIKey`，如 ↑/↓）不受「无文本输入」那道守卫限制：它不产生字符、不参与文本输入，"
            "而声明它的往往正是那个输入框自己（命令行 ↑ 翻历史），挡掉就永远不命中")
D.Interface("virtual void DebugDrawRect(const FUIRect& Rect, const FUIColor& C)",
            "调试：把矩形描出来（`bDrawDebug` 时由宿主入口打开）")
D.Nested("FUIScrollRequest", Kind="struct",
         Desc="滚动区域**进区域前**给出的请求：`bToBottom` 一次性贴底（Console 自动滚动），"
              "`bSetScrollY` 显式置量。做成请求而不是直接设置，是因为滚动量只有内容摆完之后才能确定")
D.Nested("FUIScrollInfo", Kind="struct", Desc="出区域时**实测**的滚动量：写回节点的运行期状态"
                                              "（下一帧贴底判定用：`ScrollY >= ScrollMaxY - 1`）")
D.Nested("FUIPopupAnchor", Kind="struct",
         Desc="弹层锚点。**零尺寸矩形是合法的点锚点**（右键菜单：左下角落在指针处），"
              "所以「有没有锚点」单独用 `bHas` 表达 —— 只看矩形是否为空就分不清「点锚点」和「没给锚点」")

# ══════════════════════════════════════════════════════════════════════════════
# Public/FUIBuilder.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/FUIBuilder.h", Title="FUIBuilder.h —— 组件树节点基类",
         Desc="**树是唯一数据源**：每帧全量翻译到后端，重新声明同 Id 同类型的节点 = 复用（运行期状态保留）。"
              "节点类把三类东西分开摆：**结构**（子节点 / 布局 / 样式覆盖 / 事件）、"
              "**运行期状态**（`FUIWidgetState`，翻译期回写、业务只读）、"
              "**一次性请求**（滚动 / 焦点，翻译时取走即清）。这个三分法是本类的骨架 —— "
              "混在一起就会出现「声明把用户正在输入的值压回去」这类事故。\n"
              "`AddItem<T>` 的复用规则是声明式 UI 的核心：同 Id 同类型 ⇒ 复用（保留运行期状态）、"
              "同 Id 异类型 ⇒ 原地替换、Id 为 None ⇒ 每次新建（不可复用）。"
              "`operator[](FUIBlock)` 把这条规则升级成「块内集合 = 子节点全量」：未声明者移除、顺序即声明顺序 —— "
              "于是「声明」与「实际树」结构性一致，不需要任何 diff。\n"
              "子类扩展点是虚函数：`TypeName` / `MeasureContent` / `ResolveFrame` / `PaintSelf` / "
              "`PaintContent` / `ArrangeChildren` / `PaintOverlay` / `TypeDefaultStyle` / "
              "`IsOverlayLayer` / `SyncConfig` / `GetInputFlags` / `WantsKeyboardFocus`。"
              "其中 `TypeDefaultStyle` 必须定义在**该类型自己的 cpp**（键函数落在唯一模块 —— "
              "否则 vtable 会在每个包含头的地方各生成一份）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("UIApi.h", "导出标记（节点类型要跨 DLL 构造）")
D.Row("UIEvent.h", "事件集 / 订阅票据 / 记录（绑定与广播都在节点上）")
D.Row("UILayout.h", "`FUILayout`（节点的布局参数）")
D.Row("UIRender.h", "`IUITranslator` / `EUIInputFlags` / `EUITextAlign`（翻译与命中）")
D.Row("UIStyle.h", "`FUIStyle` 覆盖与 `FUIResolvedStyle` 解析结果")
D.Row("UITypes.h", "`FUIName` / `FUIRect` / `EUIState` / `FUIHitResult`")
D.Row("<cstdint> / <memory> / <string_view> / <type_traits> / <utility> / <vector>",
      "序号与一次性请求标志、`unique_ptr` 子节点与懒分配事件集、类型名、`static_assert` 校验、转发、子节点表")

D.Struct("FUIWidgetState", Desc="节点的**运行期状态**：翻译阶段回写，业务只读。"
                                "与树结构字段分离是刻意的 —— 翻译器只写这里，"
                                "于是「谁改了什么」在字段归属上就是自明的；"
                                "帧外读到的矩形是**上一帧**的（翻译本帧还没跑）")
D.SetAccess("public")
D.Field("FUIRect Rect{}", "本节点视图局部矩形（帧外读到的是上一帧）")
D.Field("FUIRect ScreenRect{}", "同一矩形的屏幕坐标（= 局部 + 视图原点）")
D.Field("bool bHovered = false", "指针在本控件上")
D.Field("bool bPressed = false", "按下")
D.Field("bool bActive = false", "被按下且指针仍在其上")
D.Field("bool bFocused = false", "持有键盘焦点")
D.Field("bool bSelected = false", "选中（样式解析读的是节点字段，这里是回写的观察值）")
D.Field("bool bSecondaryClicked = false",
        "本帧在「右键菜单区域」（`EUIInputFlags::ContextMenu`）内按下了右键。"
        "一次性：表达「刚刚发生的一次右键」，业务在下一帧读它开菜单")
D.Field("FUIVector2 PointerPos{}", "`bSecondaryClicked` 那一刻的指针位置（视图局部坐标，可直接当弹层锚点）")
D.Field("EUIModifiers LastModifiers = EUIModifiers::None", "最近一次事件命中时的修饰键（回调里读）")
D.Field("FUIVector2 ContentSize{}", "本帧测量结果")
D.Field("float ScrollY = 0.f", "滚动容器当前纵向滚动量")
D.Field("float ScrollMaxY = 0.f", "可滚动上限（贴底判定：`ScrollY >= ScrollMaxY - 1`）")

D.Class("FUIBuilder", Desc="组件树节点基类。每个节点同时是：一棵子树的根、一块布局参数、"
                           "一份样式覆盖（+ 每帧解析结果）、一组事件订阅、以及一次翻译单元。"
                           "这些职责刻意不拆成多个对象 —— 翻译要按树遍历，"
                           "拆开就要在遍历时同步多棵平行的树，而「顺序必须一致」这种约束无法用类型表达。")
D.SetAccess("public")
D.Interface("explicit FUIBuilder(FUIName InId)", "以 Id 构造节点（Id 是节点在其层级里的身份）")
D.Interface("virtual ~FUIBuilder()", "虚析构：组件子类在外部模块外被持有，删除析构必须在本模块（导出标记保证）")
D.Interface("FUIBuilder(const FUIBuilder&) = delete / operator=(const FUIBuilder&) = delete",
            "**拷贝显式删除**：子节点是 `unique_ptr`，拷贝会与「树里每个节点只有一份」不变量冲突")
D.Interface("FUIBuilder(FUIBuilder&&) noexcept = default",
            "移动**必须显式声明**（拷贝已删 + 虚析构会抑制隐式移动）。"
            "只可用于尚未挂载的新节点（无父无子）：已挂载节点一旦移动，其子节点的 `Parent` 会悬垂")
D.Interface("FUIBuilder& operator=(FUIBuilder&&) noexcept = default", "同上，移动赋值")
D.Interface("[[nodiscard]] FUIName GetId() const", "节点 Id")
D.Interface("[[nodiscard]] std::string_view GetTypeName() const", "类型名（虚函数 `TypeName` 的公开读口）")
D.Interface("[[nodiscard]] FUIBuilder* GetParent() const", "父节点（根为 nullptr）")
D.Interface("[[nodiscard]] const std::vector<std::unique_ptr<FUIBuilder>>& GetChildren() const", "子节点表")
D.Interface("template <typename T, typename... TArgs> T& AddItem(FUIName InId, TArgs&&... Args)",
            "声明一个子节点并返回其引用。规则：**同 Id + 同类型 = 复用已有节点**（保留运行期状态）、"
            "**同 Id + 不同类型 = 原地替换该子节点**（其余子节点不动）、"
            "`Id` 为 None 时不可复用（每次新建，诊断由调用点断言负责）。"
            "两条 `static_assert` 在编译期挡住「非 `FUIBuilder` 派生」与「构造不出」两种错法")
D.Interface("void ResetChildren()", "清空子节点（整棵树重新声明的入口）")
D.Interface("bool RemoveItem(FUIName InId)", "按 Id 移除一个子节点；不存在返回 false")
D.Interface("FUIBuilder* FindChild(FUIName InId) const", "按 Id 找一个**直接**子节点")
D.Interface("FUIBuilder& operator[](FUIBlock InBlock)",
            "**声明式块**：块内集合 = 本节点子节点的全量（同 Id 同类型复用、同 Id 异类型替换、"
            "未声明者移除、顺序按声明顺序）。返回 `*this`，故类型专属 setter 必须写在 `[]` **之前**")
D.Interface("FUILayout& Layout()", "布局参数（可写，链式 setter 的转发目标）")
D.Interface("const FUILayout& GetLayout() const", "布局参数（读）")
D.Interface("FUIStyle& Style()", "本节点的样式覆盖（只有显式写进去的项会覆盖回退链）")
D.Interface("const FUIStyle& GetStyle() const", "样式覆盖（读）")
D.Interface("const FUIResolvedStyle& GetResolvedStyle() const", "本帧解析结果（`PaintXxx` 里读的就是它）")
D.Interface("[[nodiscard]] const FUIStyle& GetTypeDefaultStyle() const",
            "该**类型**（编译期）的静态默认样式 —— 定义在本类型自己的 cpp")
D.Interface("FUIBuilder& SetDisabled(bool bIn)", "设禁用（链式）。禁用的可见效果会沿子树传播")
D.Interface("FUIBuilder& SetVisible(bool bIn)", "设可见（链式）。隐藏节点不参与翻译")
D.Interface("FUIBuilder& SetSelected(bool bIn)", "设选中（链式）")
D.Interface("[[nodiscard]] bool IsSelected() const",
            "选中态：样式解析（`EUIState::Selected`）用的**唯一**来源。"
            "组件不得再自存一份（先例：`FUISelectable` 曾自带镜像成员，导致只传给后端、样式不换色）")
D.Interface("[[nodiscard]] bool IsDisabled() const", "自身或任一祖先禁用")
D.Interface("[[nodiscard]] bool IsVisible() const", "自身或任一祖先隐藏")
D.Interface("[[nodiscard]] EUIState GetVisualState() const", "本帧生效的状态（五态里的哪一个）")
D.Interface("[[nodiscard]] const FUIWidgetState& GetState() const", "运行期状态（业务只读入口）")
D.Interface("[[nodiscard]] const FUIRect& GetRect() const", "本帧局部矩形")
D.Interface("[[nodiscard]] const FUIRect& GetScreenRect() const", "屏幕坐标矩形（上一帧）。"
                                                                  "换算由后端给，业务不需要知道窗口偏移")
D.Interface("[[nodiscard]] EUIModifiers GetLastModifiers() const",
            "最近一次事件命中时的修饰键（Shift/Ctrl/Alt）：仅在事件回调里读才有意义")
D.Interface("FUIWidgetState& MutableState()", "运行期状态写入口 —— **翻译器专用**；业务只读 `GetState()`")
D.Interface("FUIEvents* GetEvents() / GetEvents() const", "事件集（懒分配：无订阅时为 nullptr）")
D.Interface("FUIEventSubscription BindClick(FUIEventHandler H)", "订阅点击，返回票据供 Unbind")
D.Interface("FUIEventSubscription BindValueChanged(FUIFloatEventHandler H)", "订阅数值变化")
D.Interface("FUIEventSubscription BindToggled(FUIBoolEventHandler H)", "订阅勾选 / 开合")
D.Interface("FUIEventSubscription BindTextChanged(FUITextEventHandler H)", "订阅文本变化")
D.Interface("FUIEventSubscription BindSubmitted(FUITextEventHandler H)", "订阅输入框回车提交")
D.Interface("FUIEventSubscription BindSelectionChanged(FUIBoolEventHandler H)", "订阅选中项变化")
D.Interface("FUIEventSubscription BindDragDropped(FUINameEventHandler H)", "订阅落点载荷")
D.Interface("FUIEventSubscription BindPopupClosed(FUIEventHandler H)", "订阅弹层关闭")
D.Interface("FUIEventSubscription BindShortcut(FUIKeyChord Chord, FUIEventHandler H)",
            "订阅**这一条 chord** 的命中。事件按 chord 分组派发，回调不带载荷（组本身就是身份）；"
            "作用域在声明侧（`OnShortcut`）给出")
D.Interface("void UnbindClick / UnbindValueChanged / UnbindToggled / UnbindTextChanged / "
            "UnbindSubmitted / UnbindSelectionChanged / UnbindDragDropped / UnbindPopupClosed"
            "(FUIEventSubscription Id)", "按票据注销对应的订阅")
D.Interface("void UnbindShortcut(FUIKeyChord Chord, FUIEventSubscription Id)",
            "注销 `BindShortcut` 的订阅（需要给出当时声明的 chord：订阅组按 chord 分）")
D.Interface("FUIBuilder& OnClick / OnValueChanged / OnToggled / OnTextChanged / OnSubmitted / "
            "OnSelectionChanged / OnDragDropped / OnPopupClosed(...)",
            "糖：订阅 + 返回 `*this`（链式）。注销请用上面的 `BindXxx` 取票据")
D.Interface("FUIBuilder& OnShortcut(FUIKeyChord Chord, FUIEventHandler H, "
            "EUIShortcutScope Scope = EUIShortcutScope::NodeActive)",
            "声明「本节点关心的组合 + 作用域 + 命中回调」。翻译期（本节点参与翻译且未禁用时）"
            "按声明序逐条查询后端，命中即入队 `EUIEventType::Shortcut`。作用域默认 `NodeActive` = "
            "本节点自己正拿着输入才匹配；容器级 / 面板级快捷键显式传 `Anywhere`。"
            "快捷键的**可见性由本节点决定** —— 声明了就有，没声明就没有（不需要全局注册表）。"
            "只应声明一次，逐帧重来会累积订阅（与其它 `OnXxx` 同约定，放在 `if (bNew)` 里）")
D.Interface("void BroadcastEvent(const FUIEventRecord& Record)",
            "翻译器改用：把一条事件在本节点上 Broadcast（**所有者线程**调用）")
D.Interface("[[nodiscard]] FUIEventRecord MakeEvent(EUIEventType Type) const",
            "造一条指向本节点的事件记录（自带根→本节点的 Id 路径）。翻译线程用，"
            "随后交给 `IUITranslator::EnqueueEvent` 入队 —— 不做回调")
D.Interface("FUIBuilder& RequestScrollToBottom()",
            "请求下一次翻译把滚动条贴底（Console 自动滚动）；一次性，翻译后自动清除")
D.Interface("FUIBuilder& SetScrollY(float InScrollY)",
            "置滚动量（像素）。跨线程下等价于一条请求，由翻译线程在下一次翻译时应用")
D.Interface("[[nodiscard]] float GetScrollY() const", "当前滚动量")
D.Interface("[[nodiscard]] float GetScrollMaxY() const", "可滚动上限")
D.Interface("bool ConsumeScrollRequest(bool& bOutToBottom, float& OutScrollY)",
            "翻译器专用：取走一次性滚动请求；无请求返回 false")
D.Interface("FUIBuilder& RequestKeyboardFocus()",
            "请求下一次翻译把键盘焦点交给本节点（输入框自动补全的连续性）。一次性，翻译后自动清除")
D.Interface("bool ConsumeFocusRequest()", "翻译器专用：取走一次性焦点请求；无请求返回 false")
D.Interface("FUIBuilder& SetDragSource(FUIName Payload)",
            "声明本节点可被拖动，载荷是一个 `FUIName`（资产引用）。None = 不是拖放源")
D.Interface("[[nodiscard]] FUIName GetDragSource() const", "本节点的拖放载荷")
D.Interface("FUIBuilder& OnDropTarget(FUINameEventHandler H)",
            "声明本节点可落入：落点事件走 `FUIEvents::DragDropped`（多播）")
D.Interface("[[nodiscard]] bool IsDropTarget() const", "是否声明过可落入")
D.Interface("void Translate(IUITranslator& T, const FUIRect& InRect)",
            "翻译一个节点（翻译器驱动；业务不直接调用）：解析样式 → 解析矩形 → 绘制自身 → 摆子节点 → 覆盖层")
D.SetAccess("protected")
D.Interface("virtual std::string_view TypeName() const = 0",
            "类型名（纯虚）：用于诊断与「同 Id 同类型才复用」的判定")
D.Interface("virtual FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const",
            "内容尺寸（叶子：文本/图标经翻译器测量）。容器默认返回子节点累加值")
D.Interface("[[nodiscard]] virtual FUIRect ResolveFrame(const FUIRect& Allocated, IUITranslator& T) const",
            "本帧矩形的解析入口：默认按 Layout（`FrameRect`）；`FUIPanel` 的锚点比例覆盖它")
D.Interface("virtual void PaintSelf(IUITranslator& T, const FUIResolvedStyle& S) {}",
            "画自身外观（默认空：纯容器不自绘）")
D.Interface("virtual void PaintContent(IUITranslator& T, const FUIResolvedStyle& S) {}",
            "画内容（文本 / 图标 / 图片；默认空）")
D.Interface("virtual void ArrangeChildren(IUITranslator& T)",
            "子节点摆放；默认按 `LayoutParams` 交给布局引擎，`FUIGrid` / `FUICollapsingHeader` 覆盖它")
D.Interface("virtual void PaintOverlay(IUITranslator& T, const FUIResolvedStyle& S) {}",
            "覆盖在子节点之上（滚动条、遮罩、浮动菜单）")
D.Interface("virtual const FUIStyle& TypeDefaultStyle() const = 0",
            "每类型静态默认样式：**定义在该类型自己的 cpp**（键函数落在唯一模块）。"
            "返回引用 ⇒ 必须落在某个静态对象上，见 `FTypeStyleCache`")
D.Interface("virtual bool IsOverlayLayer() const",
            "浮层节点（提示 / 弹层）：正常流里不占位（矩形可为空），内容画在第二个窗口。"
            "此类节点矩形为空时仍会调用 `PaintContent`（其余节点直接跳过本帧）")
D.Interface("virtual void SyncConfig(const FUIBuilder& Declared)",
            "块声明复用时的类型专属配置同步：把**声明节点**上的结构化字段（Label/Value/…）"
            "搬到被复用的既有节点上（结构/样式/事件由 `operator[]` 统一处理，运行期状态保留旧值）")
D.Interface("virtual EUIInputFlags GetInputFlags() const", "参与命中测试 / 裁剪 / 滚动的标记位")
D.Interface("virtual bool WantsKeyboardFocus() const",
            "本类型是否要吃键盘焦点（输入框 true；其余默认 false）")
D.SetAccess("private")
D.Interface("void TranslateDragDrop(IUITranslator& T)",
            "拖放通道（翻译期调用）：声明载荷 / 接受落点，落点事件只入队")
D.Interface("std::uint32_t AllocateSerial()", "视图级只增序号（自本节点上溯到根取计数器）")
D.Interface("FUIEvents& EnsureEvents()", "事件集懒分配（首次绑定时才建）")
D.Field("FUIName Id", "节点 Id（同级唯一）")
D.Field("FUIBuilder* Parent = nullptr", "父节点（**裸观察指针** —— 所有权在父节点的 `Children` 里）")
D.Field("std::uint32_t Serial = 0", "本节点在视图里的稳定序号（诊断 / 调试用）")
D.Field("std::uint32_t NextSerial = 1", "仅根节点使用的序号计数器")
D.Field("std::vector<std::unique_ptr<FUIBuilder>> Children", "子节点（唯一所有权；顺序 = 排布顺序）")
D.Field("FUILayout LayoutParams", "布局参数")
D.Field("FUIStyle StyleOverride", "样式覆盖（实例层）")
D.Field("FUIResolvedStyle ResolvedStyle", "每帧解析结果缓存（回退链的产物）")
D.Field("FUIWidgetState State", "翻译期回写的运行期状态")
D.Field("mutable std::unique_ptr<FUIEvents> Events", "懒分配的多播事件集。`mutable` 是要点："
                                                     "绑定发生在 `const FUIBuilder&` 语境里的地方也存在（见 `SyncConfig`）")
D.Field("FUIName DragPayload{}", "拖放载荷（None = 不是拖放源）")
D.Field("std::vector<FUIShortcutDecl> Shortcuts", "声明式快捷键（声明序 = 查询序，即优先级）")
D.Field("bool bDisabled = false", "禁用标志")
D.Field("bool bVisible = true", "可见标志")
D.Field("bool bSelected = false", "选中标志（样式的唯一来源）")
D.Field("bool bIsDropTarget = false", "`OnDropTarget` 声明过")
D.Field("bool bPendingToBottom = false", "一次性：下次翻译贴底")
D.Field("bool bPendingScrollY = false", "一次性：下次翻译置滚动量")
D.Field("float PendingScrollY = 0.f", "一次性滚动量的值")
D.Field("bool bPendingFocus = false", "一次性：下次翻译取键盘焦点")

D.Struct("FUIBlock", Desc="声明式块：`Root[\"panel\"][ FUIText{\"title\"}, FUIButton{\"ok\"} ]`。"
                          "**刻意不提供 `initializer_list` 构造** —— 避免 `{...}` 在重载之间产生歧义，"
                          "强制走变参构造。块本身只持有节点（`unique_ptr`），"
                          "真正的「全量替换 + 复用」语义在 `FUIBuilder::operator[]` 里")
D.SetAccess("public")
D.Interface("template <typename... TKids> FUIBlock(TKids&&... Kids)",
            "变参构造：逐个 move 进 `Nodes`（拷贝已删，只能移动）")
D.Field("std::vector<std::unique_ptr<FUIBuilder>> Nodes", "块内节点（顺序 = 声明顺序）")

D.Card("自由函数（块内节点工厂）")
D.Table("签名", "说明")
D.Row("template <typename T, typename... TArgs> T AddItem(FUIName InId, TArgs&&... Args)",
      "按值返回（组件拷贝已删，靠移动），Id 必填 —— 与成员的 `AddItem`（返回栈上节点的引用）区分开。"
      "`static_assert` 在编译期挡住非 `FUIBuilder` 派生的类型")

# ══════════════════════════════════════════════════════════════════════════════
# Public/UICanvas.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/UICanvas.h", Title="UICanvas.h —— 视图根画布",
         Desc="根画布：视图的树根，也是 HUD / 自由布局的载体。"
              "它**无外观、无裁剪** —— 只按 `Layout()` 排布子节点，根矩形由宿主给（内容区或视口）。"
              "之所以不复用 `FUIBox`：外壳（真窗口）与叠加层两条路径都要有一个「不自绘、不裁」的根，"
              "而 `FUIBox` 是给业务当容器用的（语义不同，混用会让「根不自绘」变成靠约定维持）")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("FUIBuilder.h", "节点基类（根画布的全部行为都来自它）")

D.Class("FUICanvas", Base="FUIBuilder",
        Desc="视图根画布（`final`：它刻意不可扩展 —— 根的职责只有「按布局排子节点」）。"
             "`FUIView` 用它作为 `RootNode` 的具体类型，因此业务拿到的 `GetRoot()` 一定是一个可当容器用的节点。")
D.SetAccess("public")
D.Interface("explicit FUICanvas(FUIName InId)", "以 Id 构造根画布")
D.Interface("~FUICanvas() override", "析构")
D.SetAccess("protected")
D.Interface("[[nodiscard]] std::string_view TypeName() const override", "覆盖：类型名（诊断 / 复用判定用）")
D.Interface("[[nodiscard]] const FUIStyle& TypeDefaultStyle() const override",
            "覆盖：类型默认样式（**无外观** —— 根画布不该自绘任何东西）")

# ══════════════════════════════════════════════════════════════════════════════
# Public/UIView.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/UIView.h", Title="UIView.h —— 持久视图：根 + 显示区 + 外壳 + 事件抽干",
         Desc="**视图**是本体系的所有权单元：它持有一棵持久带状态的树（`RootNode`），"
              "声明自己的显示区与（可选）真窗口外壳，并在所有者线程抽干翻译线程入队的事件。\n"
              "线程模型由 `EUIOwnership` 显式声明，而不是靠约定：`SameThread`（编辑器面板："
              "构建与翻译同线程）与 `CrossThread`（游戏：所有者线程构建、渲染线程翻译）。"
              "两者都要走 `Edit()` 取锁，区别只是前者拿到的是无竞争锁 —— "
              "统一成一条路径，就不会出现「编辑器少写一把锁」这类只在游戏侧暴露的 bug。\n"
              "事件是**两段式**：翻译线程只 `PushEvent` 入队（不回调，因此绝不在翻译中改树），"
              "所有者在 `DrainEvents()` 里按 Id 路径派发（回调内可 `Edit()`）。"
              "调用 `DrainEvents()` 时**不得**已持有 `Edit()` 作用域 —— 回调会再次 `Edit()`，"
              "而 `std::shared_mutex` 不可重入。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("UIApi.h / UITypes.h", "导出标记与公共值类型")
D.Row("FUIBuilder.h / UICanvas.h", "节点基类与根画布（视图的树就是它们组成的）")
D.Row("UIEvent.h", "`FUIEventRecord` 与 `WindowClosed` 多播事件")
D.Row("<Core/Delegate.h>", "`TMulticastEvent`（外壳关闭广播）")
D.Row("<memory> / <mutex> / <shared_mutex>", "根画布所有权；事件队列锁；**树锁**（读写分用 —— "
                                            "翻译线程共享读、所有者独占写）")
D.Row("<string> / <vector>", "外壳标题；Id 路径与事件队列")

D.Enum("EUIOwnership", Base="std::uint8_t",
       Desc="树的所有权 / 线程模式：决定 `Edit()` 是真的取独占锁，还是只走一次无竞争锁。")
D.Field("SameThread", "编辑器面板：构建与翻译同线程")
D.Field("CrossThread", "游戏：所有者线程构建，渲染线程翻译")

D.Enum("EUIShellFlags", Base="std::uint32_t",
       Desc="外壳的窗口标志：**UI 自有位掩码**（公开头不出现 ImGui 类型），由翻译器映射为 "
            "`ImGuiWindowFlags`。这是「公开头不含 imgui.h」这条边界在数据上的体现。")
D.Field("None = 0", "无标志")
D.Field("NoCollapse = 1u << 0", "禁止折叠")
D.Field("NoMove = 1u << 1", "禁止移动")
D.Field("NoResize = 1u << 2", "禁止缩放")
D.Field("NoScrollbar = 1u << 3", "无滚动条")
D.Field("NoTitleBar = 1u << 4", "无标题栏")
D.Field("NoSavedSettings = 1u << 5", "不写 ini 记忆（位置 / 大小不落盘）")

D.Card("自由函数（位掩码运算）")
D.Table("签名", "说明")
D.Row("EUIShellFlags operator|(EUIShellFlags A, EUIShellFlags B)", "位或：组合窗口标志")
D.Row("EUIShellFlags operator&(EUIShellFlags A, EUIShellFlags B)", "位与：取交集")
D.Row("bool HasFlag(EUIShellFlags Value, EUIShellFlags Flag)", "某位是否置位")

D.Struct("FUIViewShell", Desc="可选的真窗口外壳：宿主「按视图通用循环」照着开窗，窗口内的树照常翻译。"
                              "**停靠（dock id）刻意不在其中** —— 宿主开窗时统一 "
                              "`SetNextWindowDockID(自己的 dockspace id)`，"
                              "于是「谁停靠谁」这件事只有一个决定点，而不是散在每个视图的声明里。")
D.SetAccess("public")
D.Field("bool bEnabled = false", "false = 无外壳（自绘窗口外观 / HUD / 浮层）")
D.Field("std::string Title", "窗口标题（**须稳定** —— ImGui 的 ini 记忆按它做键）")
D.Field("FUIVector2 DefaultPos{}", "首次打开的位置（像素）")
D.Field("FUIVector2 DefaultSize{}", "首次打开的大小；0 = 由内容决定")
D.Field("EUIShellFlags Flags = EUIShellFlags::None", "窗口标志")
D.Field("FUIVector2 PosFraction{}", "锚点比例（0..1，0 = 不用）：游戏侧 HUD 的「按显示区比例」语义。"
                                    "位置**只在首次生效**（其后可拖动）")
D.Field("FUIVector2 SizeFraction{}", "尺寸比例：**每帧按比例设** —— 与旧 `FUIWidget` 的 "
                                     "`AnchorX/Y`(Once) + `SizeX/Y`(Always) 逐字对齐。编辑器面板不用（交给 ini 记忆）")

D.Class("FUIEditScope", Base="RAII",
        Desc="结构 / 样式变更作用域（RAII）。跨线程模式下取**独占锁**，与翻译线程的共享读互斥。"
             "做成作用域对象而不是一对 `Lock/Unlock`：异常路径与提前 return 都不会漏解锁。"
             "**不可拷贝**（拷贝会让同一把 `shared_mutex` 的独占锁被两个对象各自持有一份，"
             "解锁时机立刻错乱）")
D.SetAccess("public")
D.Interface("explicit FUIEditScope(FUIView& InView)", "取视图的独占锁（`same_thread` 下无竞争）")
D.Interface("~FUIEditScope()", "释放锁")
D.Interface("FUIEditScope(const FUIEditScope&) = delete / operator=(const FUIEditScope&) = delete",
            "删除拷贝：作用域语义下复制锁没有定义")
D.Interface("FUIBuilder& GetRoot() const", "取根节点，声明子树（`Root[ FUIBlock{...} ]` 的起点）")
D.SetAccess("private")
D.Field("FUIView* View", "被编辑的视图（裸观察指针：`FUIEditScope` 由视图的 `Edit()` 造出并返回，"
                         "生命周期不超出调用点）")
D.Field("std::unique_lock<std::shared_mutex> Lock", "独占锁（跨线程模式下才真的竞争）")

D.Class("FUIView", Desc="持久带状态 UI 树的所有者：根 + 显示区 + 外壳声明 + 事件抽干。"
                        "**一个视图一个所有者**：视图的注册表（`FUIViewRegistry`）只持裸指针、从不删除它，"
                        "所以「谁建谁拆」是硬约定 —— 所有者必须先 `UnregisterView` 再析构本对象。")
D.SetAccess("public")
D.Interface("explicit FUIView(FUIName InId, EUIOwnership InOwnership = EUIOwnership::SameThread)",
            "建视图（默认同线程 —— 编辑器面板是多数场景）。跨线程（游戏叠加层）要显式给出")
D.Interface("~FUIView()", "析构：销毁树。**调用方必须保证已从注册表注销**（注册表持裸指针）")
D.Interface("FUIView(const FUIView&) = delete / operator=(const FUIView&) = delete",
            "删除拷贝：视图内含 `shared_mutex` 与树的所有权，复制没有意义")
D.Interface("[[nodiscard]] FUIName GetId() const", "视图 Id（注册表的键，也是调试标识）")
D.Interface("[[nodiscard]] EUIOwnership GetOwnership() const", "所有权 / 线程模式")
D.Interface("[[nodiscard]] FUIBuilder& GetRoot() / const FUIBuilder& GetRoot() const",
            "根节点（读写两个重载）。**注意：改结构要走 `Edit()`**，这个重载只是给已有作用域内的代码用")
D.Interface("[[nodiscard]] FUIEditScope Edit()",
            "变更作用域：结构 / 样式改动一律在其内；翻译线程只取共享锁，故两者互斥")
D.Interface("void SetDisplaySize(float W, float H)", "显示区域尺寸（面板 = 内容区，游戏 = 视口）。"
                                                     "翻译前由宿主设置")
D.Interface("[[nodiscard]] FUIVector2 GetDisplaySize() const", "当前显示区尺寸")
D.Interface("void SetRenderContext(void* InContext)",
            "翻译目标 ImGui 上下文（编辑器 / 游戏各一个，opaque）；翻译入口按它筛选视图")
D.Interface("[[nodiscard]] void* GetRenderContext() const", "本视图的目标上下文")
D.Interface("void SetWindowShell(bool bEnabled, std::string Title, FUIVector2 DefaultPos = {}, "
            "FUIVector2 DefaultSize = {}, EUIShellFlags Flags = EUIShellFlags::None)",
            "声明真窗口外壳（标题须稳定，因为它是 ini 记忆的键）")
D.Interface("[[nodiscard]] const FUIViewShell& GetWindowShell() const", "读外壳声明")
D.Interface("void SetShellFractions(FUIVector2 PosFraction, FUIVector2 SizeFraction)",
            "外壳锚点比例（0..1）：游戏侧 HUD 的位置 / 尺寸按显示区比例给，宿主翻译时逐帧换算。"
            "与 `SetWindowShell` 同一类（结构变更），跨线程下应在 `Edit()` 区间内调用")
D.Interface("void RequestCloseWindow()",
            "翻译线程：用户点了外壳关闭按钮（或 Esc）时调用 —— **只入队，不改结构**")
D.Interface("TMulticastEvent<void(FUIView&)> WindowClosed",
            "外壳关闭请求（多播）：所有者线程 `DrainEvents()` 期间触发，回调内可 `Edit()`")
D.Interface("void DrainEvents()",
            "所有者线程：抽干翻译线程入队的交互事件并按路径派发回调。"
            "**调用时不得持有 `Edit()` 作用域**（回调内会再次 `Edit()`）")
D.Interface("void PushEvent(FUIEventRecord Record)", "翻译线程：入队一条交互事件（锁保护）")
D.Interface("[[nodiscard]] FUIBuilder* Find(FUIName InId) const", "按 Id 查节点（递归，深度优先）")
D.Interface("[[nodiscard]] std::shared_mutex& GetTreeMutex()", "翻译器内部使用：取树锁做共享读")
D.SetAccess("private")
D.Interface("FUIBuilder* FindPath(const std::vector<FUIName>& Path) const",
            "按 Id 路径查节点（事件派发用 —— 记录带的是路径，不是指针）")
D.Field("FUIName Id", "视图 Id")
D.Field("EUIOwnership Ownership", "所有权 / 线程模式")
D.Field("std::unique_ptr<FUICanvas> RootNode", "根画布（树的唯一所有权入口）")
D.Field("mutable std::shared_mutex TreeMutex",
        "**树锁**：所有者独占写、翻译线程共享读。`mutable` 是因为 `GetTreeMutex()` 在 const 语境里也要能取")
D.Field("FUIVector2 DisplaySize{ 1280.f, 720.f }", "显示区尺寸（宿主每次翻译前设置）")
D.Field("void* RenderContext = nullptr", "目标 ImGui 上下文（opaque）")
D.Field("FUIViewShell WindowShell", "外壳声明")
D.Field("mutable std::mutex EventMutex", "事件队列锁（**独立于树锁**：入队不该阻塞树读）")
D.Field("std::vector<FUIEventRecord> PendingEvents", "待派发事件（翻译线程入队，所有者抽干）")
D.Field("bool bCloseRequested = false", "外壳关闭请求标志（在 `DrainEvents()` 时转成广播）")

# ══════════════════════════════════════════════════════════════════════════════
# Public/UITranslate.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/UITranslate.h", Title="UITranslate.h —— 翻译入口 + 每帧参数",
         Desc="宿主 / 渲染特性调用的**两个入口**，以及它们的参数结构。两个入口对应两条截然不同的路径：\n"
              "1) **编辑器面板**：宿主已 `Begin(窗口)`，调 `TranslateView(View)` —— 内容区就是当前窗口内容区；\n"
              "2) **游戏叠加层**：`UIFeature` 在 NewFrame 与 Render 之间调 `TranslateRegisteredViews(Desc)` —— "
              "遍历注册表，把所有 render context 匹配的视图翻译一遍。\n"
              "参数被收进一个结构体（而不是长参数表），是因为它要跨 DLL 传值且字段还会长 —— "
              "加字段不动签名，调用点也不必逐个改。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("UIApi.h / UITypes.h", "导出标记与 `FUIName`")
D.Row("UIView.h", "`FUIView`（翻译目标）")
D.Row("<cstdint>", "dockspace id 等定宽值")

D.Struct("FUIViewFrameDesc", Desc="翻译入口参数。`ImGuiContext` 用 opaque 指针 —— "
                                  "本头（以及所有公开头）都不认识 ImGui 类型。")
D.SetAccess("public")
D.Field("void* ImGuiContext = nullptr", "目标上下文（编辑器与游戏各一个）")
D.Field("float DisplayWidth = 0.f / float DisplayHeight = 0.f", "**无外壳路径**的显示区尺寸"
                                                                "（有外壳时窗口尺寸由 ImGui 记）")
D.Field("bool bDrawDebug = false", "绘制节点矩形与状态（调试用）")
D.Field("std::uint32_t DockSpaceId = 0", "非 0 时对外壳视图首帧 `SetNextWindowDockID`（宿主 dockspace）")
D.Field("FUIName OnlyView{}", "非 None 时只翻译该视图（面板单独翻译自己的那张树）")

D.Card("自由函数（翻译入口）")
D.Table("签名", "说明")
D.Row("void TranslateView(FUIView& View)",
      "在宿主已 `Begin` 的窗口内翻译一个视图（编辑器面板路径）。调用者须持有 `FUIView` 的所有权线程上下文；"
      "翻译内部取**共享锁**（所以：不能在 `Edit()` 区间内调它）")
D.Row("std::uint32_t TranslateRegisteredViews(const FUIViewFrameDesc& Desc)",
      "遍历注册表，翻译所有 render context 匹配的视图（游戏叠加层路径）。返回翻译的视图数")

# ══════════════════════════════════════════════════════════════════════════════
# Public/UIViewRegistry.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/UIViewRegistry.h", Title="UIViewRegistry.h —— 本 DLL 唯一的层 + 视图注册表",
         Desc="**本插件唯一的层**是 `FUIViewRegistry`（`MAHO_DECLARE_FRAME`）—— 代码生成由它推导模块名，"
              "产物即 `FUIViewRegistry.dll`；`Public/UI.h` 只是总览头，**不得**再放第二个 `MAHO_DECLARE_FRAME`。"
              "这与「一个模块一个层」的引擎约定一致：一个 DLL 只能由一个帧（收集器）代表。\n"
              "访问器与 `GetLog()` 同形：**跨 DLL 走导出函数，不导出裸变量，没有 `static Get()`、"
              "没有 `TSingleton`**；层未安装或已关闭时返回 `nullptr`，取用前判空。\n"
              "**所有权不在注册表**：各视图归各自所有者（编辑器面板 / 游戏侧系统），"
              "注册表只持裸指针、从不删除别人注册的视图。因为持的是裸指针，"
              "视图必须在析构**之前** `UnregisterView` —— 而这条次序的依赖边要由真正的 TopLevel 驱动层"
              "（游戏侧 `FGameWorld` / 编辑器侧 `FRender`）用名字形声明：子插件自己声明的 `BlockOn` "
              "会被静默跳过（collector 子图只含自己的待处理集）。\n"
              "游戏上下文的登记（`SetUIGameRenderContext`）是为了打断一个潜在环："
              "视图所有者（`FUISystem`）需要游戏的 ImGui 上下文，而上下文所有者（`FUIFeature`）"
              "已经依赖 `UISystem` —— 于是上下文通过 opaque 指针单向发布，"
              "两个模块之间不必再加一条构建依赖。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("UIApi.h / UITypes.h", "导出标记与 `FUIName`")
D.Row("<Maho.h>", "`FFrameExtension` + `MAHO_DECLARE_FRAME`（本层的帧身份）")
D.Row("Engine/Engine.h", "`IInit` / `IShutdown` 与 `FEngineBase`")
D.Row("<mutex> / <vector>", "注册表内部互斥（注册与快照可能来自不同线程）与视图表")

D.Card("自由函数（访问器与游戏上下文）")
D.Table("签名", "说明")
D.Row("FUIViewRegistry* GetUIViewRegistry()",
      "注册表访问器：跨 DLL 走导出函数。**层 Init 后非空、Shutdown 后置空**，取用前判空")
D.Row("void SetUIGameRenderContext(void* Context)",
      "游戏侧上下文登记（opaque 指针，本 DLL 不认识 ImGui）。上下文所有者（`FUIFeature`）"
      "创建后发布、销毁前置空")
D.Row("void* GetUIGameRenderContext()",
      "取游戏上下文。为空 = 上下文还没建好（视图所有者下一帧再试注册）—— "
      "**这就是「不需要 UISystem → UIFeature 构建依赖」的代价与收益**")

D.Class("FUIViewRegistry", Base="FFrameExtension + IPipeline<IInit, IShutdown>",
        Desc="跨 DLL 视图注册表（与 `FLog` 同形）。它本身是一个层，**只有 IInit / IShutdown 两个阶段**："
             "`Initialize` 里发布 `this`（此后 `GetUIViewRegistry()` 才非空），"
             "`Shutdown` 里清空并撤发布 —— 于是「注册表可用」与「层已装」是同一件事，"
             "不存在「注册表指针还在但层已经被拆」的中间态。\n"
             "线程契约：注册 / 注销在宿主线程（安装 / 卸载期），读快照可来自翻译线程 —— 内部互斥保护。")
D.SetAccess("public")
D.Interface("MAHO_DECLARE_FRAME(FUIViewRegistry)",
            "帧身份与工厂符号。**本 DLL 里唯一的一个** —— 模块名由它推导（产物 `FUIViewRegistry.dll`）")
D.Interface("FUIViewRegistry()", "构造注册表（空表）")
D.Interface("~FUIViewRegistry() override", "析构（表应为空：视图所有者已在卸载期注销）")
D.Interface("FUIViewRegistry(const FUIViewRegistry&) = delete / operator=(const FUIViewRegistry&) = delete",
            "删除拷贝：内含 mutex 与「唯一一张表」的语义")
D.Interface("void RegisterView(FUIView& View)", "注册一个视图（**不接管所有权**）")
D.Interface("void UnregisterView(FUIView& View)", "注销一个视图 —— 必须在视图析构**之前**调用")
D.Interface("[[nodiscard]] std::vector<FUIView*> SnapshotViews() const",
            "锁内快照 —— 翻译线程遍历用，**遍历期间不持锁**（视图可在遍历中被注销，"
            "所以遍历里用到的指针不能跨帧保存）")
D.Interface("[[nodiscard]] FUIView* FindView(FUIName Id) const", "按 Id 查视图；未注册返回 nullptr")
D.SetAccess("private")
D.Interface("void Initialize(FEngineBase& Engine) override", "阶段入口：发布 `this`（访问器开始返回非空）")
D.Interface("void Shutdown(FEngineBase& Engine) override", "阶段入口：清空视图表 + 撤发布")
D.Field("mutable std::mutex Mutex", "保护视图表（注册 / 注销 / 快照来自不同线程）")
D.Field("std::vector<FUIView*> Views", "视图表（**裸观察指针** —— 所有权归各视图所有者）")

# ══════════════════════════════════════════════════════════════════════════════
# Public/UI.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/UI.h", Title="UI.h —— 插件公开总览头",
         Desc="依赖方 include 这一个即可拿到全部公开类型。"
              "**硬约束：本插件的公开头不得出现 `<imgui.h>`** —— ImGui 只活在 `Private/` 里"
              "（翻译器），这条边界让 UI 的编译依赖不泄漏 ImGui，也让将来换后端只改翻译器。\n"
              "还有一条与「一个模块一个层」相关的约束：本头只是总览，**不得**再放第二个 "
              "`MAHO_DECLARE_FRAME` —— 本 DLL 唯一的层是 `FUIViewRegistry`。")

D.Card("包含的公开头（本头的全部内容）")
D.Table("头文件", "功能")
D.Row("UIApi.h", "导出标记 `MAHO_UI_API`")
D.Row("UITypes.h", "`FUIName` / `FUIRect` / `FMargin` / `EUIState` / `FUIHitResult`")
D.Row("UIStyle.h", "`FUIColor` / 分态覆盖 / 解析结果")
D.Row("UITheme.h", "全局主题 token + 换代广播")
D.Row("UILayout.h", "布局参数（方向 / 尺寸模式 / 对齐）")
D.Row("UIEvent.h", "事件类型 / 事件集 / 快捷键 chord")
D.Row("UIResource.h", "资源解析注入 + 字体图集登记")
D.Row("UIClipboard.h", "剪贴板能力注入")
D.Row("UIRender.h", "翻译后端接口 `IUITranslator`")
D.Row("FUIBuilder.h", "组件树节点基类 + 声明式块")
D.Row("UICanvas.h", "视图根画布")
D.Row("UIView.h", "持久视图（根 + 显示区 + 外壳 + 事件抽干）")
D.Row("UITranslate.h", "翻译入口与每帧参数")
D.Row("UIViewRegistry.h", "本 DLL 唯一的层 + 视图注册表 + 游戏上下文登记")

# ══════════════════════════════════════════════════════════════════════════════
# Public/Widgets —— 组件类型
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Widgets/FUIBox.h", Title="FUIBox.h —— 纯容器（分组 / 行 / 列）",
         Desc="纯容器：分组 / 行 / 列，**外观透明**（不自绘任何东西），排布走 `Layout()`。"
              "它存在的意义是「布局是树的结构，不是旁挂的约束」："
              "行列关系写在树的形状里，而不是让每个叶子自己算绝对坐标 —— "
              "于是插入/删除一个子项不会影响兄弟的写法。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("FUIBuilder.h", "节点基类（布局参数与翻译流程都由它提供）")

D.Class("FUIBox", Base="FUIBuilder",
        Desc="纯容器（`final`）。六个链式 setter 都是 `Layout().SetXxx` 的转发糖 —— "
             "转发而不是缓存字段，保证「只有一个布局真值来源」。")
D.SetAccess("public")
D.Interface("explicit FUIBox(FUIName InId)", "以 Id 构造")
D.Interface("~FUIBox() override", "析构")
D.Interface("FUIBox& SetDirection(EUIDirection D)", "设主轴方向（链式）")
D.Interface("FUIBox& SetSpacing(float S)", "设子项间距（链式）")
D.Interface("FUIBox& SetPadding(FMargin P)", "设内边距（链式）")
D.Interface("FUIBox& SetSize(FUILength W, FUILength H)", "设宽高（链式）")
D.Interface("FUIBox& SetAlign(EUIAlign H, EUIAlign V)", "设横 / 纵对齐（链式）")
D.Interface("FUIBox& SetMargin(FMargin M)", "设外边距（链式）")
D.SetAccess("protected")
D.Interface("std::string_view TypeName() const override", "覆盖：类型名")
D.Interface("const FUIStyle& TypeDefaultStyle() const override", "覆盖：类型默认样式（**空样式** —— "
                                                                "纯容器不自绘，也不该给子节点留任何继承残留）")

D.Header("Public/Widgets/FUIButton.h", Title="FUIButton.h —— 按钮",
         Desc="按钮：底/边/圆角 + 图标/标签，点击事件走多播 `Clicked`。"
              "标签与图标都是**结构化字段**（`Label` / `Icon`），因此块声明复用时可经 `SyncConfig` 同步到既有节点 —— "
              "这是「每帧重新声明」不会把按钮文字丢掉的原因。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("FUIBuilder.h", "节点基类（命中 / 事件 / 绘制流程）")
D.Row("<string>", "标签文本（节点上的持久值 —— 见 `SyncConfig`）")

D.Class("FUIButton", Base="FUIBuilder",
        Desc="按钮（`final`）。点击的判定走基类的命中流程，本类型只负责「怎么画」与「要不要重复触发」。")
D.SetAccess("public")
D.Interface("explicit FUIButton(FUIName InId)", "以 Id 构造")
D.Interface("~FUIButton() override", "析构")
D.Interface("FUIButton& SetLabel(std::string_view InLabel)", "设标签（链式）")
D.Interface("FUIButton& SetIcon(FUIName InIcon)", "设图标资源引用（链式）")
D.Interface("FUIButton& SetIconOnly(bool bIn)", "只画图标不画标签（链式）")
D.Interface("FUIButton& SetRepeat(bool bIn)", "按住时重复触发（链式）")
D.Interface("FUIButton& OnClick(FUIEventHandler H)", "订阅点击（链式糖，转发 `BindClick`）")
D.Interface("[[nodiscard]] std::string_view GetLabel() const", "读标签")
D.Interface("[[nodiscard]] bool IsClickedThisFrame() const", "本帧是否被点（读运行期状态的 `bPressed`）")
D.SetAccess("protected")
D.Interface("std::string_view TypeName() const override", "覆盖：类型名")
D.Interface("FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override",
            "覆盖：内容尺寸 = 标签 / 图标测量（内容模式下的尺寸来源）")
D.Interface("void PaintSelf(IUITranslator& T, const FUIResolvedStyle& S) override", "覆盖：自绘底 / 边 / 圆角")
D.Interface("void PaintContent(IUITranslator& T, const FUIResolvedStyle& S) override", "覆盖：画图标与标签")
D.Interface("EUIInputFlags GetInputFlags() const override", "覆盖：参与命中（`HitTest`）")
D.Interface("const FUIStyle& TypeDefaultStyle() const override", "覆盖：类型默认样式（定义在本类型自己的 cpp）")
D.Interface("void SyncConfig(const FUIBuilder& Declared) override",
            "覆盖：把声明节点的标签 / 图标搬到被复用的既有节点上（用户点按态因此不会被声明压回）")
D.SetAccess("private")
D.Field("std::string Label", "标签文本")
D.Field("FUIName Icon{}", "图标资源引用")
D.Field("bool bIconOnly = false", "只画图标")
D.Field("bool bRepeat = false", "按住重复触发")

D.Header("Public/Widgets/FUICheckbox.h", Title="FUICheckbox.h —— 复选框",
         Desc="复选框：勾选框 + 标签。**勾选态是结构化声明字段**（`bChecked`），"
              "但交互结果由后端写回（`WidgetCheckbox` 的引用形参），"
              "所以「声明」与「用户刚改的值」必须靠 `bValueDirty` 之类的显式声明才覆盖 —— "
              "否则每次重声明都会把用户刚勾掉的状态塞回去。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("FUIBuilder.h", "节点基类")
D.Row("<string>", "标签文本")

D.Class("FUICheckbox", Base="FUIBuilder", Desc="复选框（`final`）：勾选框 + 标签，勾选态是节点上的结构化字段。")
D.SetAccess("public")
D.Interface("explicit FUICheckbox(FUIName InId)", "以 Id 构造")
D.Interface("~FUICheckbox() override", "析构")
D.Interface("FUICheckbox& SetLabel(std::string_view InLabel)", "设标签（链式）")
D.Interface("FUICheckbox& SetChecked(bool bIn)", "设勾选态（链式）")
D.Interface("FUICheckbox& SetIcon(FUIName InIcon)", "设图标资源引用（链式）")
D.Interface("FUICheckbox& OnToggled(FUIBoolEventHandler H)", "订阅勾选变化（链式糖，转发 `BindToggled`）")
D.Interface("[[nodiscard]] bool IsChecked() const", "读勾选态")
D.Interface("[[nodiscard]] std::string_view GetLabel() const", "读标签")
D.SetAccess("protected")
D.Interface("std::string_view TypeName() const override", "覆盖：类型名")
D.Interface("FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override",
            "覆盖：内容尺寸 = 勾选框边长 + 标签宽")
D.Interface("void PaintSelf(IUITranslator& T, const FUIResolvedStyle& S) override", "覆盖：自绘")
D.Interface("void PaintContent(IUITranslator& T, const FUIResolvedStyle& S) override", "覆盖：画勾选框与标签")
D.Interface("EUIInputFlags GetInputFlags() const override", "覆盖：参与命中")
D.Interface("const FUIStyle& TypeDefaultStyle() const override", "覆盖：类型默认样式")
D.Interface("void SyncConfig(const FUIBuilder& Declared) override", "覆盖：同步标签 / 图标到复用节点")
D.SetAccess("private")
D.Field("std::string Label", "标签文本")
D.Field("FUIName Icon{}", "勾选框图标引用")
D.Field("bool bChecked = false", "勾选态（结构化字段）")
D.Field("float BoxSize = 16.f", "勾选框边长")

D.Header("Public/Widgets/FUICollapsingHeader.h", Title="FUICollapsingHeader.h —— 可折叠分组头",
         Desc="可折叠分组头：一行标题 + 展开时的子树。它覆盖 `ArrangeChildren` —— "
              "因为「折叠时不摆子节点」是一条布局规则（子节点仍然存在，只是不参与本帧排布），"
              "而不是「删掉子节点」：删了会连同运行期状态一起丢（用户展开后内部的滚动位置、输入内容）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("FUIBuilder.h", "节点基类（含 `ArrangeChildren` 扩展点）")
D.Row("<string>", "标签文本")

D.Class("FUICollapsingHeader", Base="FUIBuilder", Desc="可折叠分组头（`final`）。")
D.SetAccess("public")
D.Interface("explicit FUICollapsingHeader(FUIName InId)", "以 Id 构造")
D.Interface("~FUICollapsingHeader() override", "析构")
D.Interface("FUICollapsingHeader& SetLabel(std::string_view InLabel)", "设标签（链式）")
D.Interface("FUICollapsingHeader& SetDefaultOpen(bool bIn)", "设初始展开态（链式）")
D.Interface("FUICollapsingHeader& SetCollapsible(bool bIn)", "设是否可折叠（链式）")
D.Interface("FUICollapsingHeader& SetIcon(FUIName InIcon)", "设图标资源引用（链式）")
D.Interface("FUICollapsingHeader& OnToggled(FUIBoolEventHandler H)", "订阅开合（链式糖）")
D.Interface("[[nodiscard]] bool IsOpen() const", "读展开态")
D.Interface("[[nodiscard]] std::string_view GetLabel() const", "读标签")
D.SetAccess("protected")
D.Interface("std::string_view TypeName() const override", "覆盖：类型名")
D.Interface("FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override",
            "覆盖：内容尺寸（展开时含子树）")
D.Interface("void PaintSelf(IUITranslator& T, const FUIResolvedStyle& S) override", "覆盖：画标题行")
D.Interface("void ArrangeChildren(IUITranslator& T) override",
            "覆盖：**折叠时不摆子节点**（子节点仍在树里，只不参与本帧排布）")
D.Interface("const FUIStyle& TypeDefaultStyle() const override", "覆盖：类型默认样式")
D.Interface("void SyncConfig(const FUIBuilder& Declared) override", "覆盖：同步标签 / 图标 / 初始展开态")
D.SetAccess("private")
D.Interface("[[nodiscard]] float RowHeight() const", "标题行高（测量与排布共用同一处计算）")
D.Field("std::string Label", "标签文本")
D.Field("FUIName Icon{}", "图标资源引用")
D.Field("bool bOpen = false", "展开态（结构化字段，跨帧持久）")
D.Field("bool bCollapsible = true", "是否可折叠")

D.Header("Public/Widgets/FUIColorEdit.h", Title="FUIColorEdit.h —— 颜色编辑",
         Desc="颜色编辑（RGBA 四分量，语义等价 `ImGui::ColorEdit4` + AlphaBar）：标签在左、色块占右。\n"
              "**值与后端的关系是本类型最要紧的一条**：翻译期后端**原地改写**节点的 `RGBA`，"
              "所以所有者读回请用 `GetValue()`（编辑中的颜色就是运行期真值）。"
              "下一帧的声明**不会**把它压回 —— 只有 `SetValue` 显式给过（`bValueDirty`）才覆盖。"
              "颜色编辑没有单值语义，故**不抛** `ValueChanged`（多播一条「颜色变了 0.3」没有意义）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("FUIBuilder.h", "节点基类")
D.Row("<string>", "标签文本")

D.Class("FUIColorEdit", Base="FUIBuilder", Desc="颜色编辑控件（`final`）。")
D.SetAccess("public")
D.Interface("explicit FUIColorEdit(FUIName InId)", "以 Id 构造")
D.Interface("~FUIColorEdit() override", "析构")
D.Interface("FUIColorEdit& SetLabel(std::string_view InLabel)", "设标签（链式）")
D.Interface("FUIColorEdit& SetValue(const FUIColor& InColor)",
            "显式设值（链式）：**这是唯一会把后端正在编辑的值覆盖掉的入口**，故它置 `bValueDirty`")
D.Interface("FUIColorEdit& SetLabelWidth(float W)", "设标签列宽（链式）")
D.Interface("[[nodiscard]] FUIColor GetValue() const", "读当前颜色（编辑中的真值）")
D.SetAccess("protected")
D.Interface("std::string_view TypeName() const override", "覆盖：类型名")
D.Interface("FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override",
            "覆盖：内容尺寸 = 标签宽 + 字号 + 色块宽")
D.Interface("void PaintSelf(IUITranslator& T, const FUIResolvedStyle& S) override", "覆盖：自绘")
D.Interface("void PaintContent(IUITranslator& T, const FUIResolvedStyle& S) override",
            "覆盖：标签 + 色块；交互结果由后端原地写回 `RGBA`")
D.Interface("EUIInputFlags GetInputFlags() const override", "覆盖：参与命中")
D.Interface("const FUIStyle& TypeDefaultStyle() const override", "覆盖：类型默认样式")
D.Interface("void SyncConfig(const FUIBuilder& Declared) override",
            "覆盖：**只认显式声明**（`bValueDirty`），不拿陈旧声明压回后端写入的运行期值")
D.SetAccess("private")
D.Field("std::string Label", "标签文本")
D.Field("float RGBA[4] = { 1.f, 1.f, 1.f, 1.f }", "颜色值（后端原地改写）")
D.Field("float LabelWidth = 0.f", "标签列宽（0 = 用默认）")
D.Field("bool bValueDirty = false", "声明侧是否显式给过值（块复用同步的判据）")

D.Header("Public/Widgets/FUIDragFloat.h", Title="FUIDragFloat.h —— 拖拽数值",
         Desc="拖拽数值（1..4 分量，语义等价 `ImGui::DragFloatN`）：标签在左、拖拽条占右。"
              "分量数决定画几个格子与读几个 float，因此 `SetComponents` 必须在 `SetValue(s)` 之前 —— "
              "否则会出现「按 1 分量写的赋值，却在 3 分量布局上生效」。\n"
              "值与后端的关系同 `FUIColorEdit`：后端原地改写 `Values`，所有者用 `GetValues()` 读真值；"
              "多播 `ValueChanged` 只是糖（1 分量时携带该值，多分量时携带第 0 分量）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("FUIBuilder.h", "节点基类")
D.Row("<string>", "标签与格式串（`\"%.3f\"` 这类 —— 是节点字段，不是全局设置）")

D.Class("FUIDragFloat", Base="FUIBuilder", Desc="拖拽数值控件（`final`）。")
D.SetAccess("public")
D.Interface("explicit FUIDragFloat(FUIName InId)", "以 Id 构造")
D.Interface("~FUIDragFloat() override", "析构")
D.Interface("FUIDragFloat& SetLabel(std::string_view InLabel)", "设标签（链式）")
D.Interface("FUIDragFloat& SetComponents(int InComponents)", "设分量数（1..4）；越界按 1 / 4 收口")
D.Interface("FUIDragFloat& SetValue(float InValue)", "单分量赋值（`Components == 1` 时用）")
D.Interface("FUIDragFloat& SetValues(const float* InValues)", "N 分量赋值（读 `Components` 个 float；"
                                                             "nullptr 忽略）")
D.Interface("FUIDragFloat& SetSpeed(float InSpeed)", "设拖拽灵敏度（链式）")
D.Interface("FUIDragFloat& SetFormat(std::string_view InFormat)", "设数值格式串（链式）")
D.Interface("FUIDragFloat& SetLabelWidth(float W)", "设标签列宽（链式）")
D.Interface("FUIDragFloat& OnValueChanged(FUIFloatEventHandler H)", "订阅数值变化（链式糖）")
D.Interface("[[nodiscard]] float GetValue() const", "读第 0 分量")
D.Interface("[[nodiscard]] const float* GetValues() const", "读全部分量（长度 = `Components`）")
D.Interface("[[nodiscard]] int GetComponents() const", "读分量数")
D.SetAccess("protected")
D.Interface("std::string_view TypeName() const override", "覆盖：类型名")
D.Interface("FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override",
            "覆盖：内容尺寸 = 标签宽 + 各分量格子宽 + 间距")
D.Interface("void PaintSelf(IUITranslator& T, const FUIResolvedStyle& S) override", "覆盖：自绘")
D.Interface("void PaintContent(IUITranslator& T, const FUIResolvedStyle& S) override", "覆盖：画标签与格子")
D.Interface("EUIInputFlags GetInputFlags() const override", "覆盖：参与命中")
D.Interface("const FUIStyle& TypeDefaultStyle() const override", "覆盖：类型默认样式")
D.Interface("void SyncConfig(const FUIBuilder& Declared) override", "覆盖：只认显式声明的值（`bValueDirty`）")
D.SetAccess("private")
D.Field("std::string Label", "标签文本")
D.Field("std::string Format = \"%.3f\"", "数值格式串")
D.Field("float Values[4] = { 0, 0, 0, 0 }", "四个分量（后端原地改写）")
D.Field("int Components = 1", "分量数（1..4）")
D.Field("float Speed = 0.25f", "拖拽灵敏度")
D.Field("float LabelWidth = 0.f", "标签列宽")
D.Field("bool bValueDirty = false", "声明侧是否显式给过值")

D.Header("Public/Widgets/FUIGrid.h", Title="FUIGrid.h —— 网格容器",
         Desc="网格容器：定列数 + 定单元尺寸，按行铺子（内容浏览器资产网格）。"
              "它覆盖 `ArrangeChildren`（走 `ArrangeIn`，把内容区切成格子）与 `PaintOverlay`（选中框）—— "
              "「选中态用 `Selected` 状态组绘制」这条规则因此只写在网格里，"
              "单元自己不必知道谁选中了它（`SelectedId` 是网格的字段，不是单元字段）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("FUIBuilder.h", "节点基类（含 `ArrangeChildren` / `PaintOverlay` 扩展点）")

D.Class("FUIGrid", Base="FUIBuilder", Desc="网格容器（`final`）：定列数、定单元尺寸，按行铺子。")
D.SetAccess("public")
D.Interface("explicit FUIGrid(FUIName InId)", "以 Id 构造")
D.Interface("~FUIGrid() override", "析构")
D.Interface("FUIGrid& SetColumns(int InColumns)", "设列数（≤0 收口为 1）（链式）")
D.Interface("FUIGrid& SetCellSize(float W, float H)", "设单元尺寸（链式）")
D.Interface("FUIGrid& SetToggleable(bool bIn)", "设单元可切换选中（链式）")
D.Interface("FUIGrid& SetSelected(FUIName Id)", "设当前选中项 Id（链式）")
D.Interface("FUIGrid& SetCellSpacing(float S)", "设单元间距（链式）")
D.Interface("[[nodiscard]] int GetColumns() const", "读列数")
D.Interface("[[nodiscard]] FUIVector2 GetCellSize() const", "读单元尺寸")
D.Interface("[[nodiscard]] float GetCellSpacing() const", "读单元间距")
D.Interface("[[nodiscard]] FUIName GetSelected() const", "读当前选中项 Id")
D.SetAccess("protected")
D.Interface("std::string_view TypeName() const override", "覆盖：类型名")
D.Interface("FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override",
            "覆盖：内容尺寸 = 列数 × 单元宽、行数 × 单元高")
D.Interface("void ArrangeChildren(IUITranslator& T) override", "覆盖：按网格切分内容区并摆放子节点")
D.Interface("void PaintOverlay(IUITranslator& T, const FUIResolvedStyle& S) override",
            "覆盖：在子节点之上画选中框（选中态取 `Selected` 状态组）")
D.Interface("EUIInputFlags GetInputFlags() const override", "覆盖：裁剪 / 滚动等标记位")
D.Interface("const FUIStyle& TypeDefaultStyle() const override", "覆盖：类型默认样式")
D.SetAccess("private")
D.Field("int Columns = 1", "列数")
D.Field("float CellW = 0.f, CellH = 0.f", "单元尺寸")
D.Field("float CellSpacing = 4.f", "单元间距")
D.Field("bool bToggleable = false", "单元可切换选中")
D.Field("FUIName SelectedId{}", "当前选中项 Id（None = 无选中）")

D.Header("Public/Widgets/FUIImage.h", Title="FUIImage.h —— 图片",
         Desc="图片：资源引用 `FUIName`，由翻译层解析成原生句柄后绘制（UI 插件不认识任何纹理类型）。"
              "`Tint`（资源引用）与 `TintColor`（颜色）并存，是因为色调既可能是「一张色调贴图」，"
              "也可能是「一个纯色乘数」—— 两种需求都真实存在，用一个字段包两边反而要靠约定区分。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("FUIBuilder.h", "节点基类（含 `EUIScaleMode` 与绘制原语）")

D.Class("FUIImage", Base="FUIBuilder", Desc="图片控件（`final`）。")
D.SetAccess("public")
D.Interface("explicit FUIImage(FUIName InId)", "以 Id 构造")
D.Interface("~FUIImage() override", "析构")
D.Interface("FUIImage& SetTexture(FUIName InTexture)", "设纹理资源引用（链式）")
D.Interface("FUIImage& SetUV(float U0, float V0, float U1, float V1)",
            "设 UV 子区域（链式）—— 图集 / 精灵图靠它取片，而不是给每片单独一张纹理")
D.Interface("FUIImage& SetTint(FUIName InTint)", "设色调资源引用（链式）")
D.Interface("FUIImage& SetTintColor(FUIColor InColor)", "设色调颜色（链式）")
D.Interface("FUIImage& SetScaleMode(EUIScaleMode InMode)", "设缩放模式（链式）")
D.Interface("[[nodiscard]] FUIName GetTexture() const", "读纹理引用")
D.SetAccess("protected")
D.Interface("std::string_view TypeName() const override", "覆盖：类型名")
D.Interface("FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override",
            "覆盖：内容尺寸（按 `ScaleMode` 由资源尺寸与可用区算出）")
D.Interface("void PaintContent(IUITranslator& T, const FUIResolvedStyle& S) override",
            "覆盖：解析纹理后画（资源未就绪时按占位外观绘，**不阻塞帧**）")
D.Interface("const FUIStyle& TypeDefaultStyle() const override", "覆盖：类型默认样式")
D.Interface("void SyncConfig(const FUIBuilder& Declared) override", "覆盖：同步纹理 / UV / 缩放模式")
D.SetAccess("private")
D.Field("FUIName Texture{}", "纹理资源引用")
D.Field("FUIName Tint{}", "色调资源引用")
D.Field("FUIColor TintColor{ 1, 1, 1, 1 }", "色调颜色乘数")
D.Field("FUIVector2 UV0{ 0, 0 } / UV1{ 1, 1 }", "UV 子区域（左上 / 右下）")
D.Field("EUIScaleMode ScaleMode = EUIScaleMode::Fit", "缩放模式")

D.Header("Public/Widgets/FUIInputText.h", Title="FUIInputText.h —— 文本输入",
         Desc="文本输入：单行 / 多行，带提示串与长度上限。**文本是节点上的持久值** —— "
              "这一点是全体系的关键：后端每帧直接把编辑内容写回节点的 `Value`，"
              "所以「正在输入的文字」不会因为下一帧重新声明而丢；"
              "而 `SetValue` 置 `bValueDirty`，只有显式声明过才覆盖用户输入。\n"
              "`OnSubmitted` 只对**单行**有意义（多行不提交，那该交给按钮）—— "
              "这是语义约定，写在这里而不是让调用方自己判。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("FUIBuilder.h", "节点基类（含 `WantsKeyboardFocus` 扩展点）")
D.Row("<string>", "文本 / 提示串（节点上的持久值）")

D.Class("FUIInputText", Base="FUIBuilder", Desc="文本输入控件（`final`）。")
D.SetAccess("public")
D.Interface("explicit FUIInputText(FUIName InId)", "以 Id 构造")
D.Interface("~FUIInputText() override", "析构")
D.Interface("FUIInputText& SetValue(std::string_view InValue)",
            "显式设文本（链式）：置 `bValueDirty` —— 这是唯一会覆盖用户输入正文的入口")
D.Interface("FUIInputText& SetHint(std::string_view InHint)", "设提示串（链式）")
D.Interface("FUIInputText& SetMaxLength(std::size_t InMax)", "设长度上限（链式；0 = 不限）")
D.Interface("FUIInputText& SetMultiline(bool bIn)", "设多行（链式）")
D.Interface("FUIInputText& OnTextChanged(FUITextEventHandler H)", "订阅文本变化（链式糖）")
D.Interface("FUIInputText& OnSubmitted(FUITextEventHandler H)",
            "订阅回车提交（链式糖）。载荷 = 提交时的文本；**单行输入框才有提交**（多行交给按钮）")
D.Interface("[[nodiscard]] const std::string& GetValue() const", "读当前文本（用户输入的真值）")
D.Interface("[[nodiscard]] bool HasFocus() const", "本帧是否持有键盘焦点（读运行期状态的 `bFocused`）")
D.SetAccess("protected")
D.Interface("std::string_view TypeName() const override", "覆盖：类型名")
D.Interface("FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override",
            "覆盖：内容尺寸（多行时按可用宽换算行数）")
D.Interface("void PaintSelf(IUITranslator& T, const FUIResolvedStyle& S) override", "覆盖：自绘输入框底 / 边")
D.Interface("EUIInputFlags GetInputFlags() const override", "覆盖：参与命中")
D.Interface("bool WantsKeyboardFocus() const override", "覆盖：**返回 true** —— 输入框是唯一主动索取键盘焦点的类型")
D.Interface("const FUIStyle& TypeDefaultStyle() const override", "覆盖：类型默认样式")
D.Interface("void SyncConfig(const FUIBuilder& Declared) override", "覆盖：只认显式声明的文本（`bValueDirty`）")
D.SetAccess("private")
D.Field("std::string Value", "当前文本（后端每帧原地改写）")
D.Field("std::string Hint", "提示串（空值时显示）")
D.Field("std::size_t MaxLength = 0", "长度上限（0 = 不限）")
D.Field("bool bMultiline = false", "多行模式")
D.Field("bool bValueDirty = false", "声明侧是否显式给过文本")

D.Header("Public/Widgets/FUIPanel.h", Title="FUIPanel.h —— 面板",
         Desc="面板：可选锚点比例、可选自绘外观、可选滚动区。它覆盖 `ResolveFrame` —— "
              "因为锚点比例是**直接按视图显示区取矩形**（旧 `FUIWidget` 语义），"
              "绕开父布局分配；这条覆盖是本类型存在的核心理由（否则它只是个 `FUIBox`）。\n"
              "`Chrome` 区分「只当容器」与「自绘外观」：真窗口外壳已经画了标题与边框时再自绘一次，"
              "就会得到两层边框 —— 所以这不是外观偏好，而是「谁画窗口」的归属问题。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("FUIBuilder.h", "节点基类（含 `ResolveFrame` / `ArrangeChildren` 扩展点）")
D.Row("<string>", "标题文本")

D.Enum("EUIPanelChrome", Base="std::uint8_t", Desc="面板的自绘范围。")
D.Field("None", "只当容器（真窗口外壳已画标题 / 边框）")
D.Field("Full", "自绘面板外观（无窗口叠加层 / 子区域）")

D.Class("FUIPanel", Base="FUIBuilder", Desc="面板（`final`）：可选锚点比例、可选自绘外观、可选滚动区。")
D.SetAccess("public")
D.Interface("explicit FUIPanel(FUIName InId)", "以 Id 构造")
D.Interface("~FUIPanel() override", "析构")
D.Interface("FUIPanel& SetAnchor(float InX, float InY, float InW, float InH)",
            "设显示区比例（0..1）：覆盖父布局分配，直接按视图显示区取矩形（链式）")
D.Interface("FUIPanel& SetTitle(std::string_view InTitle)", "设标题（链式）")
D.Interface("FUIPanel& SetClosable(bool bIn)", "设可关闭（链式）")
D.Interface("FUIPanel& SetChrome(EUIPanelChrome C)", "设自绘范围（链式）")
D.Interface("FUIPanel& SetScrollable(bool bIn)", "设可滚动（链式）")
D.Interface("FUIPanel& SetContextMenu(bool bIn)",
            "本面板矩形即右键菜单区域（链式）：区域内右键回写 `GetState().bSecondaryClicked` 与 "
            "`.PointerPos`（下一帧读；位置可直接当 `FUIPopup::SetAnchor` 的点锚点）")
D.Interface("FUIPanel& SetHeaderHeight(float H)", "设标题行高（链式）")
D.Interface("[[nodiscard]] bool HasAnchor() const", "是否给了锚点比例")
D.Interface("[[nodiscard]] const std::string& GetTitle() const", "读标题")
D.SetAccess("protected")
D.Interface("std::string_view TypeName() const override", "覆盖：类型名")
D.Interface("FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override",
            "覆盖：内容尺寸（含标题行）")
D.Interface("[[nodiscard]] FUIRect ResolveFrame(const FUIRect& Allocated, IUITranslator& T) const override",
            "覆盖：**锚点比例在这里生效** —— 不走父布局分配，直接按显示区比例取矩形")
D.Interface("void PaintSelf(IUITranslator& T, const FUIResolvedStyle& S) override",
            "覆盖：`Chrome == Full` 时自绘底 / 边框 / 标题行")
D.Interface("void ArrangeChildren(IUITranslator& T) override", "覆盖：从内容区扣掉标题行长再摆子节点")
D.Interface("EUIInputFlags GetInputFlags() const override",
            "覆盖：`Clip`（有滚动时）与 `ContextMenu`（声明过时）等标记位")
D.Interface("const FUIStyle& TypeDefaultStyle() const override", "覆盖：类型默认样式")
D.Interface("void SyncConfig(const FUIBuilder& Declared) override", "覆盖：同步标题 / 外观 / 旗标")
D.SetAccess("private")
D.Interface("[[nodiscard]] float UsedHeaderHeight() const", "本帧实际占用的标题行高（含关掉按钮的存在性判断）")
D.Interface("[[nodiscard]] FUIRect BodyRect() const", "内容区矩形（标题行之下）")
D.Field("std::string Title", "标题文本")
D.Field("FUIName CloseId{}", "关闭框的内部命中 Id（由面板 Id 派生 —— 子控件不能和面板同级撞 Id）")
D.Field("EUIPanelChrome Chrome = EUIPanelChrome::None", "自绘范围")
D.Field("bool bClosable = false", "可关闭")
D.Field("bool bScrollable = false", "可滚动")
D.Field("bool bContextMenu = false", "本矩形是右键菜单区域")
D.Field("bool bHasAnchor = false", "是否给了锚点比例")
D.Field("float AnchorX = 0.f, AnchorY = 0.f, AnchorW = 1.f, AnchorH = 1.f", "锚点比例（相对显示区）")
D.Field("float HeaderHeight = 24.f", "标题行高")

D.Header("Public/Widgets/FUIPopup.h", Title="FUIPopup.h —— 弹层",
         Desc="弹层：自身在正常流里**零尺寸**，`bOpen` 由业务置位，翻译器开第二个窗口并把子树摆进去。\n"
              "`bShownLastFrame` 是**跨帧记忆**，它必须寄存在节点上：翻译器一帧一实例，记不住上一帧，"
              "而弹层的开合边沿判定需要「上一帧到底画出来没有」这个事实。"
              "用户点外部 / 按 Esc 关掉时，翻译器入队 `PopupClosed`（多播 `OnClosed`），"
              "回调里把 `SetOpen(false)` 落回树 —— 少了这份记忆，用户关掉后下一帧又会被重新 `OpenPopup`，永不可关。\n"
              "它同时覆盖 `IsOverlayLayer()` 返回 true：零尺寸节点在正常流里不该被布局分配空间，"
              "但内容仍要画（画在第二个窗口里）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("FUIBuilder.h", "节点基类（含 `IsOverlayLayer` 与弹层原语）")

D.Class("FUIPopup", Base="FUIBuilder", Desc="弹层（`final`）：`bOpen` 是声明侧意图，"
                                            "`bShownLastFrame` 是后端实测事实 —— 两者一起构成开合边沿。")
D.SetAccess("public")
D.Interface("explicit FUIPopup(FUIName InId)", "以 Id 构造")
D.Interface("~FUIPopup() override", "析构")
D.Interface("FUIPopup& SetOpen(bool bIn)", "设期望开合态（链式）")
D.Interface("FUIPopup& SetAnchor(const FUIRect& InAnchor)",
            "设固定锚点矩形（链式，局部坐标）。右键菜单可用零尺寸的点锚点")
D.Interface("FUIPopup& SetModal(bool bIn)", "设模态（链式）：模态时挡住下层输入")
D.Interface("FUIPopup& SetFollowAnchor(bool bIn)", "设是否每帧跟随锚点 / 父矩形（链式）")
D.Interface("FUIPopup& SetMeasureWidth(float InWidth)",
            "设内容测量宽度（链式，< 0 = 用默认值）。内容最终是「最宽子节点 + 自身内边距」，"
            "故窗口宽 ≈ 本值 + 内边距")
D.Interface("FUIPopup& OnClosed(FUIEventHandler H)", "订阅弹层关闭（链式糖，转发 `BindPopupClosed`）")
D.Interface("[[nodiscard]] bool IsOpen() const", "读期望开合态")
D.SetAccess("protected")
D.Interface("std::string_view TypeName() const override", "覆盖：类型名")
D.Interface("FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override",
            "覆盖：内容尺寸（正常流里为零，量的是弹层窗口内要摆的子树）")
D.Interface("void PaintContent(IUITranslator& T, const FUIResolvedStyle& S) override",
            "覆盖：开第二个窗口并摆子树；返回的 `bWasShown` 写回 `bShownLastFrame`")
D.Interface("const FUIStyle& TypeDefaultStyle() const override",
            "覆盖：类型默认样式 —— **弹层窗口的底色取自它**（弹层是独立窗口，须压成声明值才落屏）")
D.Interface("void SyncConfig(const FUIBuilder& Declared) override", "覆盖：同步锚点 / 模态 / 测量宽")
D.Interface("bool IsOverlayLayer() const override", "覆盖：**返回 true** —— 正常流里不占位，矩形可为空")
D.SetAccess("private")
D.Interface("[[nodiscard]] IUITranslator::FUIPopupAnchor ResolveAnchor() const",
            "解析落位目标（锚点或父节点矩形）—— 可能是「没有锚点」，故用带 `bHas` 的结构体")
D.Field("FUIRect Anchor{}", "固定锚点矩形（局部坐标）")
D.Field("bool bOpen = false", "期望开合态")
D.Field("bool bModal = false", "模态")
D.Field("bool bHasAnchor = false", "是否给了固定锚点")
D.Field("bool bFollowAnchor = true", "每帧跟随父节点矩形（否则只认 `SetAnchor` 的固定矩形）")
D.Field("float MeasureWidth = -1.f", "内容测量宽度，< 0 = 默认")
D.Field("bool bShownLastFrame = false",
        "上一帧后端**真的**画出了这个弹层吗（后端的跨帧记忆，寄存在节点上）：开合边沿用它 —— "
        "上升沿（树要开、后端没画）才开新窗；用户自己关掉的那一帧「树还要开、后端已经没画」，"
        "据此不开新窗，交给调用方落回 `bOpen=false`。**不进 `SyncConfig`**：这是后端实测事实，"
        "不是声明侧能改写的东西")

D.Header("Public/Widgets/FUISelectable.h", Title="FUISelectable.h —— 可选项",
         Desc="可选项：**选中态由父容器（列表 / 树 / 网格）维护**，本节点只报告点击。"
              "选中态刻意不在这里自存镜像，而是直接转发基类的 `SetSelected` / `IsSelected` —— "
              "先例：曾自带一份镜像成员，结果是「传给了后端、样式却不换色」（样式解析读的是基类那份）。"
              "把这层转发写进类型，就使这条错误的写法不可能再被写出来。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("FUIBuilder.h", "节点基类（选中态与命中流程）")
D.Row("<string>", "标签文本")

D.Class("FUISelectable", Base="FUIBuilder", Desc="可选项（`final`）：只报告点击，选中态归父容器维护。")
D.SetAccess("public")
D.Interface("explicit FUISelectable(FUIName InId)", "以 Id 构造")
D.Interface("~FUISelectable() override", "析构")
D.Interface("FUISelectable& SetLabel(std::string_view InLabel)", "设标签（链式）")
D.Interface("FUISelectable& SetIcon(FUIName InIcon)", "设图标资源引用（链式）")
D.Interface("FUISelectable& SetSelected(bool bIn)",
            "设选中（链式，**转发 `FUIBuilder::SetSelected`**）：选中态就是基类那一份，本类型不再自存镜像")
D.Interface("FUISelectable& SetSpanAll(bool bIn)", "设横跨整行（链式）")
D.Interface("FUISelectable& OnSelected(FUIEventHandler H)", "订阅点击（链式糖，转发 `BindClick`）")
D.Interface("[[nodiscard]] bool IsSelected() const", "读选中态（转发基类）")
D.Interface("[[nodiscard]] std::string_view GetLabel() const", "读标签")
D.SetAccess("protected")
D.Interface("std::string_view TypeName() const override", "覆盖：类型名")
D.Interface("FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override",
            "覆盖：内容尺寸（整行时取可用宽）")
D.Interface("void PaintSelf(IUITranslator& T, const FUIResolvedStyle& S) override",
            "覆盖：自绘（选中时用 `Selected` 状态组的样式）")
D.Interface("void PaintContent(IUITranslator& T, const FUIResolvedStyle& S) override", "覆盖：画图标与标签")
D.Interface("EUIInputFlags GetInputFlags() const override", "覆盖：参与命中")
D.Interface("const FUIStyle& TypeDefaultStyle() const override", "覆盖：类型默认样式")
D.Interface("void SyncConfig(const FUIBuilder& Declared) override", "覆盖：同步标签 / 图标")
D.SetAccess("private")
D.Field("std::string Label", "标签文本")
D.Field("FUIName Icon{}", "图标资源引用")
D.Field("bool bSpanAll = false", "是否横跨整行")

D.Header("Public/Widgets/FUISeparator.h", Title="FUISeparator.h —— 分隔线",
         Desc="分隔线：横向为一条细线，纵向（`SetVertical`）为竖线，颜色缺省取主题 `Separator`。"
              "颜色用 `std::optional` 而不是「颜色 + 布尔」：**「没给颜色」与「给了某个颜色」是两种不同语义**"
              "（前者走主题回退，后者是明确覆盖），`optional` 让这个区分在类型上成立。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("FUIBuilder.h", "节点基类")
D.Row("<optional>", "颜色的存在性（是否显式覆盖主题色）")

D.Class("FUISeparator", Base="FUIBuilder", Desc="分隔线（`final`）。")
D.SetAccess("public")
D.Interface("explicit FUISeparator(FUIName InId)", "以 Id 构造")
D.Interface("~FUISeparator() override", "析构")
D.Interface("FUISeparator& SetThickness(float InThickness)", "设线宽（链式）")
D.Interface("FUISeparator& SetColor(FUIColor InColor)", "设颜色（链式，覆盖主题 `Separator`）")
D.Interface("FUISeparator& SetVertical(bool bIn)", "设纵向（链式）")
D.Interface("[[nodiscard]] bool HasColor() const", "是否显式给了颜色")
D.SetAccess("protected")
D.Interface("std::string_view TypeName() const override", "覆盖：类型名")
D.Interface("FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override",
            "覆盖：内容尺寸（横线高 = 线宽，竖线宽 = 线宽）")
D.Interface("void PaintSelf(IUITranslator& T, const FUIResolvedStyle& S) override", "覆盖：画那条线")
D.Interface("const FUIStyle& TypeDefaultStyle() const override", "覆盖：类型默认样式（缺省色 = 主题 `Separator`）")
D.Interface("void SyncConfig(const FUIBuilder& Declared) override", "覆盖：同步线宽 / 颜色 / 方向")
D.SetAccess("private")
D.Field("std::optional<FUIColor> Color", "显式颜色（空 = 用主题色）")
D.Field("float Thickness = 1.f", "线宽")
D.Field("bool bVertical = false", "纵向")

D.Header("Public/Widgets/FUISliderFloat.h", Title="FUISliderFloat.h —— 浮点滑块",
         Desc="浮点滑块：标签在左、滑条占右。值变更走多播 `ValueChanged`（带新值）。"
              "与拖拽数值同样的规矩：后端原地改写 `Value`，`SetValue` 才置 `bValueDirty` —— "
              "拖动中的值不会被下一帧的重新声明压回起点。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("FUIBuilder.h", "节点基类")
D.Row("<string>", "标签与格式串")

D.Class("FUISliderFloat", Base="FUIBuilder", Desc="浮点滑块（`final`）。")
D.SetAccess("public")
D.Interface("explicit FUISliderFloat(FUIName InId)", "以 Id 构造")
D.Interface("~FUISliderFloat() override", "析构")
D.Interface("FUISliderFloat& SetLabel(std::string_view InLabel)", "设标签（链式）")
D.Interface("FUISliderFloat& SetValue(float InValue)", "显式设值（链式）：置 `bValueDirty`")
D.Interface("FUISliderFloat& SetRange(float InMin, float InMax)", "设取值范围（链式）")
D.Interface("FUISliderFloat& SetFormat(std::string_view InFormat)", "设数值格式串（链式）")
D.Interface("FUISliderFloat& SetLabelWidth(float W)", "设标签列宽（链式）")
D.Interface("FUISliderFloat& OnValueChanged(FUIFloatEventHandler H)", "订阅数值变化（链式糖）")
D.Interface("[[nodiscard]] float GetValue() const", "读当前值（拖动中的真值）")
D.SetAccess("protected")
D.Interface("std::string_view TypeName() const override", "覆盖：类型名")
D.Interface("FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override",
            "覆盖：内容尺寸")
D.Interface("void PaintSelf(IUITranslator& T, const FUIResolvedStyle& S) override", "覆盖：自绘")
D.Interface("void PaintContent(IUITranslator& T, const FUIResolvedStyle& S) override", "覆盖：画标签与滑条")
D.Interface("EUIInputFlags GetInputFlags() const override", "覆盖：参与命中")
D.Interface("const FUIStyle& TypeDefaultStyle() const override", "覆盖：类型默认样式")
D.Interface("void SyncConfig(const FUIBuilder& Declared) override", "覆盖：只认显式声明的值（`bValueDirty`）")
D.SetAccess("private")
D.Field("std::string Label", "标签文本")
D.Field("std::string Format = \"%.3f\"", "数值格式串")
D.Field("float Value = 0.f", "当前值（后端原地改写）")
D.Field("float Min = 0.f, Max = 1.f", "取值范围")
D.Field("float LabelWidth = 0.f", "标签列宽")
D.Field("bool bValueDirty = false", "声明侧是否显式给过值")

D.Header("Public/Widgets/FUIText.h", Title="FUIText.h —— 单行文本",
         Desc="单行文本：内容尺寸由翻译后端测量，**字体是资源引用**（经样式逐实例可换）。"
              "`SetFont` / `SetFontSize` 直接写样式（`Style().Font` / `Style()[Normal].FontSize`）—— "
              "字号是「一档」而不是任意值（`SnapFontSize` 会吸附），所以它在状态组里，"
              "而字体在样式的共用槽里（五态通常同字体）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("FUIBuilder.h", "节点基类（`Style()` 的入口与绘制原语）")
D.Row("<string>", "文本内容")

D.Class("FUIText", Base="FUIBuilder", Desc="单行文本（`final`）。")
D.SetAccess("public")
D.Interface("explicit FUIText(FUIName InId)", "以 Id 构造")
D.Interface("~FUIText() override", "析构")
D.Interface("FUIText& SetText(std::string_view InText)", "设文本（链式）")
D.Interface("FUIText& SetAlign(EUITextAlign A)", "设水平对齐（链式）")
D.Interface("FUIText& SetWrap(bool bIn)", "设自动换行（链式）")
D.Interface("FUIText& SetFont(FUIName F)", "设字体资源引用（链式，写样式的共用槽）")
D.Interface("FUIText& SetFontSize(float Size)", "设字号（链式，写 `States[Normal].FontSize`）")
D.Interface("[[nodiscard]] std::string_view GetText() const", "读文本")
D.SetAccess("protected")
D.Interface("std::string_view TypeName() const override", "覆盖：类型名")
D.Interface("FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override",
            "覆盖：内容尺寸 = `MeasureText`（换行时按可用宽换算）")
D.Interface("void PaintContent(IUITranslator& T, const FUIResolvedStyle& S) override",
            "覆盖：解析字体后画文本")
D.Interface("const FUIStyle& TypeDefaultStyle() const override", "覆盖：类型默认样式（定义在本类型自己的 cpp）")
D.Interface("void SyncConfig(const FUIBuilder& Declared) override", "覆盖：同步文本 / 对齐 / 换行")
D.SetAccess("private")
D.Field("std::string Text", "文本内容")
D.Field("EUITextAlign Align = EUITextAlign::Left", "水平对齐")
D.Field("bool bWrap = false", "自动换行")

D.Header("Public/Widgets/FUITooltip.h", Title="FUITooltip.h —— 悬停提示",
         Desc="悬停提示：自身在正常流里**零尺寸**（占位不占空间）；当父节点被悬停时，"
              "由翻译器开一个提示窗口，内容 = `SetText` 的文本（**或**本节点的子树）。"
              "「文本还是子树」用 `bHasText` 切换 —— 两者共用一个节点，"
              "于是面板侧要加副标题 / 图标时不必换成另一种提示类型。"
              "同样覆盖 `IsOverlayLayer()`：零尺寸但仍要画（画在第二个窗口里）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("FUIBuilder.h", "节点基类（含 `IsOverlayLayer` 与提示窗原语）")
D.Row("<string>", "提示文本")

D.Class("FUITooltip", Base="FUIBuilder", Desc="悬停提示（`final`）。面板侧**零后端代码** —— "
                                              "开窗、换行、跟随鼠标都由翻译器处理。")
D.SetAccess("public")
D.Interface("explicit FUITooltip(FUIName InId)", "以 Id 构造")
D.Interface("~FUITooltip() override", "析构")
D.Interface("FUITooltip& SetText(std::string_view InText)", "设提示文本（链式）")
D.Interface("FUITooltip& SetChildren()", "改用子树当内容（链式）：不再画 `SetText` 的文本")
D.Interface("FUITooltip& SetDelay(float InDelay)", "设延迟（链式）。v1：后端尽力而为"
                                                  "（ImGui 即时提示，不排队等待）")
D.Interface("FUITooltip& SetFollowMouse(bool bIn)", "设是否跟随鼠标（链式）")
D.Interface("[[nodiscard]] std::string_view GetText() const", "读提示文本")
D.SetAccess("protected")
D.Interface("std::string_view TypeName() const override", "覆盖：类型名")
D.Interface("FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override",
            "覆盖：**正常流里返回零尺寸**（不占位）")
D.Interface("void PaintContent(IUITranslator& T, const FUIResolvedStyle& S) override",
            "覆盖：父节点被悬停时开提示窗并画内容")
D.Interface("const FUIStyle& TypeDefaultStyle() const override", "覆盖：类型默认样式（提示窗底色取自它）")
D.Interface("void SyncConfig(const FUIBuilder& Declared) override", "覆盖：同步文本 / 跟随鼠标 / 延迟")
D.Interface("bool IsOverlayLayer() const override", "覆盖：**返回 true** —— 零尺寸仍要画")
D.SetAccess("private")
D.Interface("[[nodiscard]] FUIVector2 MeasureTipContent(IUITranslator& T) const",
            "提示窗口的内容尺寸（文本或子树），与正常流的零尺寸测量分开")
D.Field("std::string Text", "提示文本")
D.Field("float Delay = 0.f", "延迟（v1 后端尽力而为）")
D.Field("bool bFollowMouse = true", "跟随鼠标")
D.Field("bool bHasText = true", "内容模式：true = 画文本，false = 画子树")

D.Header("Public/Widgets/FUITreeNode.h", Title="FUITreeNode.h —— 树节点",
         Desc="树节点：一行标题（箭头 + 图标 + 标签）+ 展开时的缩进子树。"
              "**子节点只在展开时参与排布**（覆盖 `ArrangeChildren`），但树里的子节点不会因折叠而消失 —— "
              "折叠只影响本帧排布，不影响所有权与运行期状态。展开态是结构化字段（跨帧持久）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("FUIBuilder.h", "节点基类（含 `ArrangeChildren` 扩展点）")
D.Row("<string>", "标签文本")

D.Class("FUITreeNode", Base="FUIBuilder", Desc="树节点（`final`）。")
D.SetAccess("public")
D.Interface("explicit FUITreeNode(FUIName InId)", "以 Id 构造")
D.Interface("~FUITreeNode() override", "析构")
D.Interface("FUITreeNode& SetLabel(std::string_view InLabel)", "设标签（链式）")
D.Interface("FUITreeNode& SetOpen(bool bIn)", "设展开态（链式）")
D.Interface("FUITreeNode& SetIcon(FUIName InIcon)", "设图标资源引用（链式）")
D.Interface("FUITreeNode& SetIndent(float InIndent)", "设子树的缩进量（链式）")
D.Interface("FUITreeNode& OnToggled(FUIBoolEventHandler H)", "订阅开合（链式糖，转发 `BindToggled`）")
D.Interface("[[nodiscard]] bool IsOpen() const", "读展开态")
D.Interface("[[nodiscard]] std::string_view GetLabel() const", "读标签")
D.SetAccess("protected")
D.Interface("std::string_view TypeName() const override", "覆盖：类型名")
D.Interface("FUIVector2 MeasureContent(IUITranslator& T, const FUIVector2& Available) const override",
            "覆盖：内容尺寸（展开时含缩进子树）")
D.Interface("void PaintSelf(IUITranslator& T, const FUIResolvedStyle& S) override", "覆盖：画标题行（箭头 + 图标 + 标签）")
D.Interface("void ArrangeChildren(IUITranslator& T) override", "覆盖：**只在展开时摆子节点**，并加缩进")
D.Interface("const FUIStyle& TypeDefaultStyle() const override", "覆盖：类型默认样式")
D.Interface("void SyncConfig(const FUIBuilder& Declared) override", "覆盖：同步标签 / 图标 / 缩进")
D.SetAccess("private")
D.Interface("[[nodiscard]] float RowHeight() const", "标题行高（测量与排布共用同一处计算）")
D.Field("std::string Label", "标签文本")
D.Field("FUIName Icon{}", "图标资源引用")
D.Field("bool bOpen = false", "展开态（跨帧持久）")
D.Field("float Indent = 14.f", "子树缩进量")

# ══════════════════════════════════════════════════════════════════════════════
# Private —— 实现细节（公开头不得出现 imgui.h，ImGui 只活在这里）
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Private/UIImGuiTranslator.h", Title="UIImGuiTranslator.h —— ImGui 翻译器（唯一的 imgui.h 边界）",
         Desc="**唯一允许 include `<imgui.h>` 的原语边界**（本头只在 `Private/` 内被包含）。"
              "把树的语义原语映射为 ImGui 的绘制 / 控件调用。\n"
              "一个翻译器实例 = 一次 `TranslateView`（或叠加层一帧），**不跨帧复用** —— "
              "所以任何「上一帧的事实」（弹层是否画出来过、滚动实测值）都不能存在翻译器里，"
              "只能经参数进出、寄存在节点上。\n"
              "文件开头先 `#define NOMINMAX`：Win32 的 `min`/`max` 宏会破坏 `std::min/max` 与 "
              "`<limits>`，而 ImGui 会间接引入 `<Windows.h>`。"
              "「声明即真值」是本文件里最要紧的一条：后端自己画的控件（输入框 / 滑条 / 拖拽 / 颜色选择 / 折叠头）"
              "不吃树画的 `DrawRect`，只有把解析结果压成 ImGui 样式，节点上声明的 `FUIStyle` 才真正落到屏上。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("<UIRender.h>", "要实现的接口 `IUITranslator` 及其原语签名")
D.Row("<UIView.h>", "`FUIView`（翻译目标；`FImGuiTranslator` 持其裸观察指针）")
D.Row("imgui.h", "**本插件唯一接触 ImGui 的地方** —— 公开头永远看不到它")
D.Row("<unordered_map>", "每帧的资源解析缓存（纹理 / 字体），帧内零重复解析")
D.Row("<vector>", "原点栈与滚动请求栈")

D.Class("FImGuiTranslator", Base="IUITranslator",
        Desc="`IUITranslator` 的 ImGui 实现。构造即绑定一个 `ImGuiContext` —— "
             "编辑器与游戏各有上下文，翻译器按上下文创建，因此**不需要在内部再筛上下文**。\n"
             "它把三类「隐藏状态」显式化成字段：坐标原点栈（滚动区进栈）、禁用深度（祖先禁用传播）、"
             "以及两个每帧缓存（纹理 / 字体）。这些状态都是「翻译过程中必须记住」的东西，"
             "而翻译器一帧一实例，所以它们不需要跨帧重置逻辑。")
D.SetAccess("public")
D.Interface("explicit FImGuiTranslator(ImGuiContext* InContext)", "绑定目标上下文（一次翻译一个上下文）")
D.Interface("void BeginView(FUIView& View, const FUIRect& DisplayRect) override",
            "开始一帧：记下视图与显示区，重置每帧缓存")
D.Interface("void EndView(FUIView& View) override", "结束一帧（清缓存 / 收尾）")
D.Interface("[[nodiscard]] FUIRect GetDisplayRect() const override", "本帧显示区（局部坐标）")
D.Interface("[[nodiscard]] FUIVector2 GetScreenOrigin() const override", "当前原点（局部 → 屏幕的偏移）")
D.Interface("void EnqueueEvent(FUIEventRecord Record) override",
            "把事件交给视图入队 —— **只入队，不回调**（回调归所有者线程）")
D.Interface("FUIResolvedResource ResolveFont(FUIName Font, float Size) override",
            "解析字体：查 `FindUIFont(上下文, 引用, 吸附后的档位)`，记进 `FontCache`")
D.Interface("FUIResolvedResource ResolveTexture(FUIName Texture) override",
            "解析纹理：先查渲染镜像池、再问资源系统；未就绪返回 `bValid=false`（按占位绘，不阻塞帧）")
D.Interface("FUIVector2 MeasureText(std::string_view Text, const FUIResolvedResource& Font, "
            "float Size) override", "用 ImGui 度量文本（内容尺寸的唯一来源）")
D.Interface("FUIVector2 MeasureIcon(const FUIResolvedResource& Icon, float Size) override", "度量图标")
D.Interface("void PushDisabled() / PopDisabled() override",
            "祖先禁用的压栈（映射 ImGui 的 `BeginDisabled/EndDisabled`），"
            "与 `DisabledDepth` 计数配对")
D.Interface("void PushClip(const FUIRect& Rect) / PopClip() override", "裁剪矩形压栈")
D.Interface("void DrawRect(const FUIRect& Rect, const FUIResolvedStyle& S) override",
            "填充 + 描边 + 圆角（`AddRectFilled` / `AddRect`）")
D.Interface("void DrawText(const FUIRect& Rect, std::string_view Text, const FUIResolvedResource& Font, "
            "float Size, const FUIResolvedStyle& S, EUITextAlign Align) override",
            "画文本（先 `PushFont` 到解析出的字体档位，再按对齐算起点）")
D.Interface("void DrawIcon(const FUIRect& Rect, const FUIResolvedResource& Icon, "
            "const FUIResolvedStyle& S) override", "画图标（按解析出的字体档位画字形）")
D.Interface("void DrawImage(const FUIRect& Rect, const FUIResolvedResource& Texture, "
            "const FUIColor& Tint, const FUIVector2& UV0, const FUIVector2& UV1) override",
            "画图片（`Image` + UV，带色调）")
D.Interface("FUIHitResult WidgetButton(FUIName Id, const FUIRect& Rect, const FUIResolvedStyle& S) override",
            "按钮：`InvisibleButton` + 自绘，返回 `FUIHitResult`")
D.Interface("FUIHitResult WidgetCheckbox(FUIName Id, const FUIRect& Rect, bool& bValue, "
            "const FUIResolvedStyle& S) override", "复选框（引用形参直接写回节点字段）")
D.Interface("FUIHitResult WidgetSliderFloat(FUIName Id, const FUIRect& Rect, float& Value, "
            "float Min, float Max, std::string_view Format, const FUIResolvedStyle& S) override",
            "滑条：**压入解析样式**后调 ImGui 控件，否则声明值不落屏")
D.Interface("FUIHitResult WidgetInputText(FUIName Id, const FUIRect& Rect, std::string& Text, "
            "std::string_view Hint, std::size_t MaxLength, bool bMultiline, "
            "const FUIResolvedStyle& S) override", "输入框（原地改写 `Text`）")
D.Interface("FUIHitResult WidgetSelectable(FUIName Id, const FUIRect& Rect, bool bSelected, "
            "const FUIResolvedStyle& S) override",
            "可选项：`bSelected` 只作为输入（选中态已在树侧解析进样式），后端不回写")
D.Interface("FUIHitResult WidgetDragFloat(FUIName Id, const FUIRect& Rect, float* Values, "
            "int Components, float Speed, std::string_view Format, const FUIResolvedStyle& S) override",
            "拖拽数值（1..4 分量，原地改写 `Values`）")
D.Interface("FUIHitResult WidgetColorEdit(FUIName Id, const FUIRect& Rect, float* RGBA, "
            "const FUIResolvedStyle& S) override", "颜色编辑（原地改写 `RGBA`）")
D.Interface("bool BeginScrollRegion(FUIName Id, const FUIRect& Rect, "
            "const FUIScrollRequest& Request) override",
            "进滚动区：把请求压进 `ScrollStack`（贴底要等内容摆完才能应用）")
D.Interface("FUIScrollInfo EndScrollRegion() override", "出滚动区：取实测滚动量返回给调用方写回节点")
D.Interface("FUIHitResult WidgetCollapsingHeader(FUIName Id, const FUIRect& Rect, bool& bOpen, "
            "const FUIResolvedStyle& S) override", "折叠头（引用形参写回展开态）")
D.Interface("bool HitTestSecondary(const FUIRect& Rect, FUIVector2& OutPos) override",
            "右键区域命中：纯几何判定（滚动容器的 item 会被内容子窗口挡掉，区域语义才对）")
D.Interface("bool BeginTooltip(FUIName Id, const FUIRect& Anchor, bool bFollowMouse, "
            "const FUIResolvedStyle& S) override", "开提示窗（并把声明样式压成窗口样式）")
D.Interface("void EndTooltip() override", "关提示窗")
D.Interface("bool BeginPopup(FUIName Id, bool bOpen, bool bWasShown, const FUIPopupAnchor& Anchor, "
            "bool bModal, const FUIRect& ContentBox, const FUIResolvedStyle& S) override",
            "开弹层：**开合边沿用 `bOpen` 与 `bWasShown` 一起算**（上升沿 `OpenPopup`）—— "
            "翻译器记不住上一帧，`bWasShown` 由调用方（节点字段）带进来")
D.Interface("void EndPopup() override", "关弹层")
D.Interface("bool BeginDragSource(FUIName Id, std::string_view PayloadType, std::string_view Payload, "
            "std::string_view PreviewText) override", "开始拖放源（`BeginDragDropSource`）")
D.Interface("void EndDragSource() override", "结束拖放源")
D.Interface("bool IsDropTarget(FUIName Id, std::string_view PayloadType, std::string* OutPayload) override",
            "接受落点并取回载荷")
D.Interface("void SetKeyboardFocus(FUIName Id) override",
            "请求键盘焦点（记进 `PendingFocusId`，本帧进入实际控件时生效）")
D.Interface("bool HasFocus(FUIName Id) const override", "某节点是否持有焦点（与 `FocusedId` 比）")
D.Interface("bool IsShortcutPressed(const FUIKeyChord& Chord) override",
            "快捷键命中查询：按后端键映射 + 窗口焦点 + 「无文本输入」守卫判定")
D.Interface("void DebugDrawRect(const FUIRect& Rect, const FUIColor& C) override", "调试描边")
D.Interface("bool bDebugDraw = false", "调试叠加开关（帧描述 `bDrawDebug` 透传过来）")
D.Interface("[[nodiscard]] FUIRect ToScreen(const FUIRect& Local) const", "局部坐标 → 屏幕坐标")
D.Interface("[[nodiscard]] FUIView* CurrentView() const", "当前翻译的视图（宿主入口用）")
D.SetAccess("private")
D.Interface("[[nodiscard]] ImVec2 ScreenMin(const FUIRect& Local) const", "局部矩形左上角 → 屏幕坐标")
D.Interface("[[nodiscard]] ImVec2 ScreenMax(const FUIRect& Local) const", "局部矩形右下角 → 屏幕坐标")
D.Interface("[[nodiscard]] static ImU32 ToColor(const FUIColor& C)", "`FUIColor` → `ImU32`（打包 8 位）")
D.Interface("[[nodiscard]] static ImVec4 ToColor4(const FUIColor& C)", "`FUIColor` → `ImVec4`（ImGui 样式用）")
D.Interface("[[nodiscard]] ImFont* FontOf(const FUIResolvedResource& Font, float Size) const",
            "由解析结果取 `ImFont*`（未解析到时返回缺省字体）")
D.Interface("void PushControlStyle(const FUIResolvedStyle& S) / PopControlStyle()",
            "把解析结果压成 ImGui 控件样式（颜色 + FramePadding/Rounding/BorderSize + 字体档位）。"
            "**压 / 弹必须成对**（计数固定）—— 后端自己画的控件不吃树画的 `DrawRect`，"
            "不压就等于声明被丢弃")
D.Interface("void PushWindowStyle(const FUIResolvedStyle& S) / PopWindowStyle()",
            "同上，但作用于弹层 / 提示**窗口**（它们是独立窗口，底 / 边框 / 圆角由窗口样式决定）")
D.Interface("[[nodiscard]] FUIHitResult HitTestItem(FUIName Id, const FUIRect& Local, bool bHitTest, "
            "bool bAllowHoverWhileActive = false)",
            "统一命中流程。`bAllowHoverWhileActive`：同窗口另有活跃项时仍报告悬停位 —— "
            "ImGui 默认把「非活跃项」的悬停直接压成 false，于是「拖着 A 扫过 B」里 B 永远不报悬停；"
            "拖拽扩选（控制台日志行）正需要 B 的悬停位")
D.Field("ImGuiContext* Context = nullptr", "目标上下文")
D.Field("FUIView* View = nullptr", "当前翻译的视图（裸观察指针，仅本帧有效）")
D.Field("FUIVector2 Origin{}", "当前坐标原点（屏幕坐标，随滚动区进栈变化）")
D.Field("FUIRect Display{}", "本帧显示区（局部坐标）")
D.Field("std::vector<FUIVector2> OriginStack", "原点栈（滚动区进 / 出区域时压弹）")
D.Field("FUIResolvedResource DefaultFont", "本帧缺省字体（缓存，避免每节点重查）")
D.Field("std::unordered_map<std::uint32_t, FUIResolvedResource> TextureCache",
        "纹理解析缓存（**每帧清空**）")
D.Field("std::unordered_map<std::uint32_t, FUIResolvedResource> FontCache", "字体解析缓存（每帧清空）")
D.Field("std::uint32_t PendingFocusId = 0", "待生效的焦点请求（节点 Id）")
D.Field("std::uint32_t FocusedId = 0", "本帧实际持有焦点的节点 Id")
D.Field("int DisabledDepth = 0", "禁用压栈深度（`PushDisabled` / `PopDisabled` 配对计数）")
D.Field("bool bPopupOpen = false", "当前是否已在弹层窗口内（决定 `EndView` 时是否要收尾）")
D.Field("std::vector<FUIScrollRequest> ScrollStack", "滚动区域的待应用请求（贴底要等内容摆完）")

D.Card("自由函数")
D.Table("签名", "说明")
D.Row("void TranslateViewBody(FUIView& View, FImGuiTranslator& T, const FUIRect& LocalRect)",
      "关掉 `View` 的树锁**之前**用的内部入口（锁在宿主入口里取）："
      "把「取锁 → 翻译整棵树 → 解锁」的边界留在一处，避免各处自己写锁范围")

D.Header("Private/UILayoutEngine.h", Title="UILayoutEngine.h —— 两趟布局算法（唯一实现处）",
         Desc="**两趟布局**：`Measure` 自底向上量 Content 尺寸，`Arrange` 自顶向下分配矩形。"
              "算法只在这里实现一次；容器若要自定义排布，覆盖 `FUIBuilder::ArrangeChildren` 或调 `ArrangeIn`。\n"
              "「测量的量是什么」这件事必须精确：`MeasureContent` 给的是**子树尺寸，不含本节点外框**，"
              "`ContentRect` 与 `OuterRect` 是互逆的两次换算 —— "
              "浮层调用方算内容起点时若不先经 `OuterRect` 补回内边距，同一段内边距会被扣两次。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("<FUIBuilder.h>", "被排布 / 测量的节点类型与它的布局参数")

D.Struct("FUILayoutEngine", Desc="布局算法的静态函数集（**无状态** —— 布局的全部输入都在节点与树上，"
                                 "引擎自己没有要记住的东西，因此不需要实例）")
D.SetAccess("public")
D.Interface("static FUIVector2 Measure(FUIBuilder& Node, IUITranslator& T, const FUIVector2& Available)",
            "Content 尺寸（**不含本节点 Margin；含本节点 Padding**）")
D.Interface("static void ArrangeChildren(FUIBuilder& Node, IUITranslator& T)",
            "按 `Node` 的 Layout 摆放其**可见**子节点并逐个 `Translate`（默认 `ArrangeChildren` 走它）")
D.Interface("static void ArrangeIn(FUIBuilder& Node, IUITranslator& T, const FUIRect& Content)",
            "同上，但内容区**显式给出** —— 自定义排布（Grid 网格 / Tree 缩进）复用同一算法的入口")
D.Interface("static FUIRect FrameRect(const FUIBuilder& Node, const FUIRect& Allocated)",
            "本节点外框（已扣 Margin）—— 矩形解析的默认实现就是它")
D.Interface("static FUIRect ContentRect(const FUIBuilder& Node, const FUIRect& Frame)",
            "外框再扣内边距 = 子节点可用区。**内边距来源**：`FUILayout::Padding` 非零则用它，"
            "否则用解析样式的 `Padding`")
D.Interface("static FUIRect OuterRect(const FUIBuilder& Node, const FUIVector2& Content)",
            "`ContentRect` 的**逆**：子树尺寸 + 本节点自身内边距 = 外框（左上角在本节点原点）。"
            "`FUIBuilder::MeasureContent` 给的是子树尺寸，本节点内边距不在其中；要先经这里补回再交给 "
            "`ContentRect`")

D.Header("Private/UIStyleResolver.h", Title="UIStyleResolver.h —— 样式回退链",
         Desc="**逐项**回退（每个 `optional` 独立走链）：实例覆盖 → 父节点已解析值 → 类型默认 → "
              "（主题 token）。\n"
              "「可继承的只有前景类项」是这里最要紧的规则：`Text` / `Font` / `FontSize` / `Icon` / "
              "`IconSize` 是「逐级传导」的语义（子文本默认跟父文本同色同字体），"
              "而背景 / 描边 / 圆角 / 内边距属「容器自绘」—— 继承它们会让子容器套上父容器底色，"
              "进而使每一层容器都多画一遍背景。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("<FUIBuilder.h>", "被解析的节点（读它的样式覆盖与类型默认样式）")

D.Struct("FUIStyleResolver", Desc="样式解析的静态入口（**无状态**：解析结果由调用方持有，"
                                  "父级值以参数传入 —— 于是「继承」是显式传参，不是隐式全局状态）")
D.SetAccess("public")
D.Interface("static FUIResolvedStyle Resolve(const FUIBuilder& Node, const FUIResolvedStyle* ParentResolved)",
            "解析本节点本帧生效样式。**`ParentResolved` 为 nullptr 表示根节点**（没有可继承的父级）")

D.Header("Private/WidgetStyle.h", Title="WidgetStyle.h —— 组件实现共用小工具（Detail）",
         Desc="组件实现共用的几个小工具（只在 `Private/` 内使用，不对外暴露）。"
              "它们全都对应一条**反复出现、放错就会出错**的规矩："
              "类型默认样式必须有地方落（返回引用 ⇒ 不能是临时值）、命中结果必须写回状态、"
              "事件必须只入队、禁用节点不许产生事件、图标与标签的布局只应有一处定义。"
              "集中在这里，组件实现就只剩「画什么」而不是「记得写哪几条纪律」。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("<FUIBuilder.h>", "节点的布局 / 样式 / 状态读口与 `FUIHitResult`")
D.Row("<UITheme.h>", "`GetUIThemeStamp()` —— 样式缓存的重建判据")
D.Row("<utility>", "`std::move`（事件入队时搬运记录）")

D.Struct("FTypeStyleCache", Desc="类型默认样式的**静态缓存：主题换代后自动重建**。"
                                 "组件的 `TypeDefaultStyle()` 返回引用，故必须落在某个静态对象上；"
                                 "若只建一次，主题 token 改写后旧颜色会永久留着 —— "
                                 "所以判据是换代计数器 `Stamp`，而不是「建过一次」。")
D.SetAccess("public")
D.Field("std::uint32_t Stamp = 0xFFFFFFFFu", "上次构建时的主题换代号（初值是**不可能等于**任何真实号的值，"
                                             "保证首帧必建）")
D.Field("FUIStyle Style", "缓存下来的默认样式")
D.Interface("template <typename TBuild> const FUIStyle& Get(TBuild&& Build)",
            "取默认样式：换代则先 `Style = FUIStyle{}` 清空、再 `Build(Style)` 重建。"
            "**先清空再建**是刻意的 —— 否则改主题时删掉的项会残留")

D.Card("自由函数（Detail 内联小工具）")
D.Table("签名", "说明")
D.Row("inline FUIRect ContentRectOf(const FUIBuilder& Node)",
      "本帧矩形去掉内边距 = 内容区（**节点自己的 `Padding` 优先于解析样式的**）。"
      "与 `FUILayoutEngine::ContentRect` 同一口径 —— 组件画内容时用它就不必自己再算一遍")
D.Row("inline FUIRect Inflate(const FUIRect& R, float X, float Y)",
      "矩形向外扩张（x / y 各自加一圈）—— 画描边 / 焦点框时用")
D.Row("inline void WriteHit(FUIBuilder& Node, const FUIHitResult& Hit)",
      "命中结果回写运行期状态（业务只读 `GetState()`；结构化字段仍在节点上）")
D.Row("inline void Enqueue(FUIBuilder& Node, IUITranslator& T, EUIEventType Type, float Value = 0.f, "
      "bool bFlag = false, std::string Text = std::string())",
      "事件入队（**翻译线程只入队**；回调归所有者 `FUIView::DrainEvents()`）。"
      "**禁用节点直接 return** —— 禁用不只影响外观，也不该产生事件")
D.Row("inline FUIHitResult HitAndReport(FUIBuilder& Node, IUITranslator& T, bool bDrawChrome)",
      "通用可点控件：跑后端命中 → 回写状态 → 完成点击时入队 `Clicked`。"
      "`bDrawChrome` 决定是否把解析样式交给后端画（自绘外观的控件传 false，用空样式让后端别画底）")
D.Row("inline FUIRect LabelRect(const FUIBuilder& Node, bool bHasIcon)",
      "标签文本可用区：有图标时把图标所占的横向span（`IconSize + 6`）让出去 —— "
      "图标与标签的排布因此只有这一处定义")
D.Row("inline FUIRect IconRect(const FUIBuilder& Node)",
      "行内图标区（左端方框，纵向居中）")


