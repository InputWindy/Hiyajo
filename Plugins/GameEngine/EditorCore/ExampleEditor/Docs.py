# ExampleEditor 插件的内容脚本（plugin_docs.py 会 exec 本文件，docs_builder 已注入为 D）。
# 内容与磁盘上的 Public/*.h（+ Private/ImGuiTheme.h）逐字对应。

# ══════════════════════════════════════════════════════════════════════════════
# Public/ExampleEditorApi.h —— 导出宏
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/ExampleEditorApi.h", Title="ExampleEditorApi.h —— 导出宏",
         Desc="本插件的导出开关：`MAHO_EXAMPLEEDITOR_API` 按 `MAHO_EXAMPLEEDITOR_MODULE_EXPORTS` "
              "在 `MAHO_EXPORT` / `MAHO_IMPORT` 之间切换。\n"
              "**为什么这里尤其需要**：宿主 `FRender` 构造/析构 `FExampleEditor`，"
              "编辑器组件插件继承 `IEditorPanel` / `IEditorInit` / `IEditorShutdown`，"
              "主题函数又从组件 DLL 被调用。一个类型若不带导出标记，vftable 与删除析构会"
              "每个模块各生成一份，析构就落在已卸载的映像里 —— 静默 `0xC0000005`。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT` 两个底层标记")

D.Macro("MAHO_EXAMPLEEDITOR_API", "MAHO_EXPORT / MAHO_IMPORT（按 MAHO_EXAMPLEEDITOR_MODULE_EXPORTS 切换）",
        Desc="挂在 `FExampleEditor`、3 个编辑器 stage 接口与 `ExampleEditorTheme.h` 的每个自由函数上。"
             "这是组件插件能跨 DLL 继承并调用它们的前提。")

# ══════════════════════════════════════════════════════════════════════════════
# Public/ExampleEditorTheme.h —— 主题数据模型（imgui-free）
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/ExampleEditorTheme.h", Title="ExampleEditorTheme.h —— 主题数据模型（imgui-free）",
         Desc="编辑器主题 = **两张扁平的具名表**：`Colors`（每个可调色槽 4 个 float，r,g,b,a ∈ 0..1）"
              "与 `Styles`（每个可调 ImGui style 字段 1~2 个 float，标量只用 v0）。"
              "扁平缓冲**按元素交错**：`colors[i*4+0..3]`、`styles[i*2+0..1]`。\n"
              "**为什么是「索引 + 平行查询函数」而不是结构体数组**：索引顺序对某个引擎构建是稳定的"
              "（见 `Private/ImGuiTheme.cpp`），于是主题面板/持久化只需要搬运两块 float 缓冲，"
              "不必导出任何 ImGui 类型 —— 这个头**一个 ImGui 符号都没有**（连 include 都没有），"
              "所以任何插件都能在不知道 ImGui 存在的前提下存/读主题。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("ExampleEditorApi.h", "`MAHO_EXAMPLEEDITOR_API`：每个函数都要跨 DLL 导出")

D.Card("自由函数 —— 颜色")
D.Table("签名", "说明")
D.Row("MAHO_EXAMPLEEDITOR_API int GetEditorThemeColorCount()", "可编辑颜色槽的数量（缓冲长度 = 数量 × 4）")
D.Row("MAHO_EXAMPLEEDITOR_API const char* GetEditorThemeColorName(int index)",
      "该槽的显示名（如 `FrameBg`）；**永不为 null**，越界返回空串而不是崩溃")
D.Row("MAHO_EXAMPLEEDITOR_API const char* GetEditorThemeColorGroup(int index)",
      "该槽的显示分组（如 `Input frames`）—— 面板按它归类；**永不为 null**")
D.Row("MAHO_EXAMPLEEDITOR_API bool GetEditorThemeColorDefault(int index, float out[4])",
      "内建默认 (r,g,b,a) 写进 `out[4]`。**「初值住在这里、不住在调用方」**："
      "默认值属于引擎版本，调用方只负责缓存与持久化")
D.Row("MAHO_EXAMPLEEDITOR_API void ApplyEditorTheme(const float* flatColors)",
      "把整块扁平颜色缓冲写给**活的** ImGui style（长度由 `GetEditorThemeColorCount()` 决定）")
D.Row("MAHO_EXAMPLEEDITOR_API void EditorThemeReset()", "只把颜色恢复成内建默认（不动 styles）")

D.Card("自由函数 —— 样式")
D.Table("签名", "说明")
D.Row("MAHO_EXAMPLEEDITOR_API int GetEditorThemeStyleCount()", "可编辑 style 字段数量（缓冲长度 = 数量 × 2）")
D.Row("MAHO_EXAMPLEEDITOR_API const char* GetEditorThemeStyleName(int index)",
      "该字段的显示名（如 `WindowRounding`）；**永不为 null**")
D.Row("MAHO_EXAMPLEEDITOR_API const char* GetEditorThemeStyleGroup(int index)", "该字段的显示分组（如 `Tabs`）")
D.Row("MAHO_EXAMPLEEDITOR_API int GetEditorThemeStyleArity(int index)",
      "该字段吃几个 float（1 = 标量，2 = vec2）—— 面板据此决定渲染一个还是两个拖拽框")
D.Row("MAHO_EXAMPLEEDITOR_API bool GetEditorThemeStyleDefault(int index, float out[2])",
      "内建默认 (v0[,v1]) 写进 `out[2]`")
D.Row("MAHO_EXAMPLEEDITOR_API void ApplyEditorThemeStyles(const float* flatStyles)",
      "把整块扁平 style 缓冲写给活的 ImGui style")
D.Row("MAHO_EXAMPLEEDITOR_API void EditorThemeStylesReset()", "只把 styles 恢复成内建默认（不动颜色）")

D.Card("自由函数 —— 持久化（整个主题一个文件）")
D.Table("签名", "说明")
D.Row("MAHO_EXAMPLEEDITOR_API bool SaveEditorTheme(const char* path, const float* flatColors, const float* flatStyles)",
      "颜色 + styles 一起写入 `path`；true = 成功（父目录会按需创建）")
D.Row("MAHO_EXAMPLEEDITOR_API bool LoadEditorTheme(const char* path, float* outColors, float* outStyles)",
      "从 `path` 读回两块缓冲。**缺失的段保持原样**（调用方先填默认值再调）—— "
      "所以旧版保存的文件缺了新增的槽，仍然可用")
D.Row("MAHO_EXAMPLEEDITOR_API const char* GetEditorThemeDefaultPath()",
      "启动时自动恢复的持久路径（相对 CWD）。**自动恢复路径由引擎决定**，"
      "面板不该自己拼一个，否则「上次的编辑器外观」会因工作目录不同而丢失")

D.Card("跨 DLL 导出面")
D.Table("符号", "说明")
D.Row("全部主题函数", "组件插件（EditorTheme 面板）在自己的 DLL 里调用它们 ⇒ 必须导出；"
      "函数体内也没暴露任何 ImGui 类型，导入方不需要 imgui 头")

# ══════════════════════════════════════════════════════════════════════════════
# Private/ImGuiTheme.h —— 编辑器默认夜色的翻译边界
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Private/ImGuiTheme.h", Title="ImGuiTheme.h —— 编辑器默认夜景（私有）",
         Desc="本插件的私有头：**唯一**碰 ImGui 类型的主题入口（前置声明 `ImVec4` 而不 include "
              "`imgui.h`，所以它只是模块内部的一页，不外泄给任何插件）。\n"
              "**为什么默认外观放在这里而公开头只放数据模型**：公开主题接口是「索引 + 数值」，"
              "ImGui 的 `ImVec4` 不能出现在跨 DLL 的公开签名里（那会强制每个调用方编译 imgui）。"
              "默认夜色因此在编辑器自己创建 ImGui 上下文之后、由本模块内部直接施加。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("（无 include，只前置声明 `struct ImVec4;`）", "公开头不引 imgui；`ImVec4` 只作为引用的返回类型出现，"
      "声明足够，定义在 .cpp 里")

D.Card("自由函数（模块内）")
D.Table("签名", "说明")
D.Row("void ApplyMahoNightTheme()",
      "施加编辑器默认夜颜色（menubar → tab strip → 面板三级，dock 底盘留黑槽）。"
      "在编辑器**自己的** ImGui 上下文创建之后调用一次 —— style 是上下文范围的，"
      "所以每个编辑器窗口/组件都自动继承，不需要逐个再设")
D.Row("const ImVec4& GetEditorBgColor()",
      "主题的「黑」旋钮（与 `WindowBg` 用的是同一个值）。返回**引用**：它就是那份 style 数据本身，"
      "调用方不该拿到副本后又与主题脱钩")

# ══════════════════════════════════════════════════════════════════════════════
# Public/ExampleEditor.h —— 编辑器宿主（渲染特性 + 子收集器）
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/ExampleEditor.h", Title="ExampleEditor.h —— 编辑器宿主：渲染特性 + 子收集器",
         Desc="`FExampleEditor` 同时是**两个东西**：\n"
              "① **FRender 的一个渲染特性** —— 挂 `FRender` 的 stage（`IOnInstalled` / `IEditorInput` / "
              "`IEditorCompose` / `IPreUnInstall`），驱动整个编辑器帧：自己**独占**一套 ImGui 上下文、"
              "拥有 EditorRT 合成目标、字体上传与最终 present。**这是全仓库唯一让编辑器碰 RHI 的地方**。\n"
              "② **它自己的子收集器**（`FFrameBuilder<FExampleEditor>`）—— 把编辑器**组件插件**"
              "（EditorViewport / EditorConsole / ContentBrowser / EditorTheme）当 DLL 装进来，"
              "每个组件是一个挂 `IEditor*` stage 的匿名 frame。\n"
              "**为什么组件不自己画**：宿主在 `OnInstalled` 装上组件（下个安全点跑它们的 Init 图），"
              "每帧在 `NewFrame()` **之前**用 `Select<IEditorPanel>()` 逐个 `Update` 组件（声明期），"
              "帧内再由通用翻译循环翻译所有注册视图，`PreUnInstall` 时先跑它们的 Shutdown 图再卸出 —— "
              "组件只声明自己的树，不持有任何 ImGui/RHI 资源。\n"
              "**为什么有 `IEditorInput` / `IEditorCompose` 这两个新 stage**（而不是直接用 "
              "`IInitViews` / `IRenderUI`）：编辑器的输入接管必须发生在游戏 UI 特性的 `IInitViews` "
              "喂 IO 之前，而「编辑器自己的 InitViews + Render」必须发生在游戏 UI 合成 `IRenderUI` 之后、"
              "帧的 `IPresent` 拷贝之前。这两点都是**帧内位置**的要求，只有自己的 stage 才表达得了。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("ExampleEditorApi.h", "`MAHO_EXAMPLEEDITOR_API`：本头所有类型都跨 DLL")
D.Row("Engine/Frame.h", "`MAHO_DECLARE_FRAME` / `MAHO_DECLARE_STAGE_DISPATCH` —— 身份与 stage 分派")
D.Row("Engine/FrameBuilder.h", "`FFrameBuilder<FExampleEditor>`：作为子收集器装载编辑器组件")
D.Row("Render.h", "`FRender` —— 本特性挂载的宿主；同时是 `GetRender()` 的返回类型")
D.Row("RDG.h", "`FRDGTextureRef`：EditorRT 与字体纹理的句柄类型")
D.Row("RenderDrawList.h", "`FDrawList`：翻译后的 ImDrawData 落点（编辑器 UI 的渲染输入）")
D.Row("UITypes.h", "`UI::FUIName`（`PresentTargetName()` 的返回类型）")
D.Row("cstdint", "`std::uint32_t`：dock space id 与纹理 id")
D.Row("mutex", "`std::mutex ImGuiFrameMutex`：同一时刻只有一个线程持有编辑器的 ImGui 帧")
D.Row("span", "`std::span<const std::string>`：`GetDroppedFiles()` 的只读视图（不生拷贝）")
D.Row("string / vector", "拖入文件批次与已排空事件批次的容器")
D.Row("Platform.h", "`Platform::MInputEvent`：单帧输入缓存里存的原始平台事件")
D.Row("struct ImGuiContext（**前置声明**，非 include）",
      "本头只持 `ImGuiContext*` 指针成员 ⇒ 前置声明足够，`imgui.h` 只在 .cpp 里被包含 —— "
      "这样编辑器头的包含者不会被迫编译 ImGui")

D.Card("宏与声明的 stage 分派")
D.Table("签名", "说明")
D.Row("MAHO_DECLARE_FRAME(FExampleEditor)",
      "生成 `StaticName()` / `GetName()` / `CreateFrame()`（DLL 工厂）/ `GetModulePath()`。"
      "宿主按**模块基名** `Install(ApplyModuleExtension(\"FExampleEditor\"))` 装载它")
D.Row("MAHO_DECLARE_STAGE_DISPATCH(FExampleEditor, IEditorInit, IEditorInit, Init)",
      "让宿主的**安装批**能驱动组件的 `Init`：`Invoke<IEditorInit, FExampleEditor>` 全特化")
D.Row("MAHO_DECLARE_STAGE_DISPATCH(FExampleEditor, IEditorShutdown, IEditorShutdown, Shutdown)",
      "让**卸载批**能驱动组件的 `Shutdown`（先跑 stage，再销毁实例）")
D.Row("（`IEditorPanel::Update` 不需要分派特化）",
      "面板的 `Update` 不是由图驱动的：宿主每帧用 `Select<IEditorPanel>()` 取帧集后**直接 for 循环**调用它，"
      "所以它的顺序由宿主代码固定（Viewport → Outliner → Inspector → Controls）")

D.Struct("FEditorContext", Desc="轻量的共享编辑器状态：宿主拥有，组件插件经 "
        "`FExampleEditor::GetEditorContext()` 读写。**为什么是共享结构体而不是依赖边**："
        "宿主把面板绘制顺序固定死（Viewport → Outliner → Inspector → Controls），"
        "于是「先发布（选中实体）后消费」由**代码顺序**保证 —— 不需要任务图，"
        "也不需要组件之间声明依赖边（组件图里根本绑不上这种边）。")
D.SetAccess("public")
D.Field("std::uint32_t SelectedEntityId = 0", "当前选中实体 id：Outliner **写**、Inspector **读**")
D.Field("bool bSceneReady = false", "场景特性是否已装载（视口据此决定要不要采样镜像）")

D.Class("IEditorInit", Desc="编辑器组件的**初始化** stage：组件的 install 图上跑一次，"
        "用于建自己的持久资源（例如一棵视图树、一个订阅）。独立的 stage 而不是塞进 "
        "`IEditorPanel::Update` —— 一次性工作不该每帧被检查一次。")
D.SetAccess("public")
D.Interface("virtual ~IEditorInit() = default", "虚析构：跨 DLL 持有，删除必须走 vtable")
D.Interface("virtual void Init(FExampleEditor&) = 0", "收到宿主引用（上下文是宿主，组件靠它够到 `FRender`）")

D.Class("IEditorPanel", Desc="**面板** stage：一个面板拥有一棵持久 UI 树（`UI::FUIView`），"
        "在 `Update` 里重建它。宿主在 `ImGui::NewFrame()` **之前**调用（声明期），"
        "随后在帧内翻译所有已注册视图 —— **面板从不调用 ImGui**。\n"
        "这就是面板的唯一入口：老式的「直接绘制」入口已经不存在了。")
D.SetAccess("public")
D.Interface("virtual ~IEditorPanel() = default", "虚析构（同 `IEditorInit`）")
D.Interface("virtual void Update(FExampleEditor&) = 0", "声明期（宿主 `NewFrame` 之前）：只改自己的 UI 树，不碰后端")

D.Class("IEditorShutdown", Desc="编辑器组件的**卸载** stage：由宿主的卸载图驱动，"
        "**在实例被销毁之前**跑。存在的理由与 `IPreUnInstall` 同：裸的成员析构会跳过这一步，"
        "留下跨模块的订阅 / 注册表里的裸指针。")
D.SetAccess("public")
D.Interface("virtual ~IEditorShutdown() = default", "虚析构（同 `IEditorInit`）")
D.Interface("virtual void Shutdown(FExampleEditor&) = 0", "撤订阅 / 注销视图，然后实例才会被释放")

D.Struct("FEditorShader", Desc="编辑器的绘制着色器类型（对照 `FUIShader`）。GLSL 源码是 .cpp 的私有实现，"
        "**只经这四个访问器**暴露给 `FRender::TryGetShader<FEditorShader>()` —— "
        "这正是 `FRender::TryGetShader` 对任何特性着色器要求的契约。**无状态**：没有逐帧数据，"
        "正交投影的推送数据来自 `FDrawList`。用静态函数而不是虚接口，"
        "因为着色器类型是**编译期**参数（`TryGetShader<T>`），运行期多态没有意义。")
D.SetAccess("public")
D.Interface("static const char* GetVertexSource()", "顶点着色器源码（一次性编译用）")
D.Interface("static const char* GetFragmentSource()", "片元着色器源码")
D.Interface("static const char* GetVertexEntryPoint()", "顶点入口点名字")
D.Interface("static const char* GetFragmentEntryPoint()", "片元入口点名字")

D.Class("FExampleEditor", Base="FFrameExtension + IPipeline<IOnInstalled, IEditorInput, IEditorCompose, IPreUnInstall> + FFrameBuilder<FExampleEditor>",
        Desc="编辑器宿主：两个基类（声明层 + 挂 FRender stage 的有序列表）加**第三个** —— "
             "`FFrameBuilder<FExampleEditor>`，使它同时是编辑器组件（EditorViewport / EditorConsole / "
             "ContentBrowser / EditorTheme）**各自的收集器**。\n"
             "`Type=Editor` 的插件：Runtime 构建在 codegen 阶段就把它（连同它的组件）丢掉，"
             "所以编辑器的 DLL / 头 / ImGui 编译只发生在编辑器构建里（`MAHO_EDITOR_BUILD`）。\n"
             "**帧壳（DockSpace）与 dock 身份留在宿主**：宿主在打开任何带壳视图之前"
             "`SetNextWindowDockID(自己的 dockspace id)`，于是组件完全不需要知道 dock id。")
D.SetAccess("private")
D.Interface("FExampleEditor()", "**私有构造**：实例只能由 DLL 工厂 `CreateFrame()` 造出 —— "
            "宿主的安装流程是它唯一的构造入口，别人不该直接 new 一个编辑器")
D.SetAccess("public")
D.Interface("void OnInstalled(FRender&) override",
            "装入安全点：建自己的 ImGui 上下文 + 字体后端，然后 `InstallEditorComponents()` "
            "把组件 DLL 装进自己的子收集器（下个安全点跑它们的 Init 图）")
D.Interface("void EditorInput(FRender&) override",
            "**Pass0：编辑器输入接管**，跑在最前面（早于游戏 UI 特性的 `IInitViews` 喂 IO）。"
            "只看 Win32 光标、把它重定位到视口面板矩形内（并 clamp 到面板局部），"
            "再经 `FUIFeature::GetUI()->SetEditorInput` 喂给游戏上下文 —— "
            "于是游戏 UI 只在面板内响应，且它的布局与显示出来的面板一致。"
            "**构造里就 BlockOn 在 `FUIFeature::IRenderUI` 之前**，把这个次序钉死")
D.Interface("void EditorCompose(FRender&) override",
            "**Pass3：合成**，在单个 `IEditorCompose` stage 里依次跑「编辑器自己的 InitViews + Render」"
            "（位置在游戏 UI 的合成 `IRenderUI` 之后、帧的 `IPresent` 拷贝之前），"
            "并用 EditorRT 接管本帧的 present 目标")
D.Interface("void PreUnInstall(FRender&) override",
            "卸出安全点：先 `ShutdownEditorComponents()`（**跑组件的 Shutdown 图**，再释放实例与模块），"
            "然后销毁自己的 ImGui 上下文 —— 顺序反了就是在已卸载的组件模块里跑代码")
D.Interface("void ReportViewportRect(float X, float Y, float W, float H)",
            "视口组件在 `Update` 里回调：发布游戏 UI 被显示到的屏幕矩形（客户区像素）。"
            "`EditorInput` 用它把游戏光标重定位回面板内。宽或高 ≤ 0 即 `bVpValid = false`")
D.Interface("[[nodiscard]] std::span<const std::string> GetDroppedFiles() const",
            "本帧拖到窗口上的文件路径（来自 OS 文件管理器的物理绝对路径）。"
            "批次**只活一帧**：`EditorInput` 填、面板绘制时读、`EditorCompose` 清掉残余。"
            "返回 span 不生拷贝，**消费方自己判断这个 drop 是不是自己的**（例如「我的窗口被 hover 了吗」）")
D.Interface("bool ConsumeDroppedFiles(std::vector<std::string>& Out)",
            "消费本帧的拖入文件：**先到先得**（后到的面板看到空批次，所以两个 drop 目标不可能导入同一个文件）。"
            "没有待处理项时返回 false（此时 `Out` 不被修改）")
D.Interface("FEditorContext& GetEditorContext()", "共享的组件状态（选中实体 / 场景就绪标志）")
D.Interface("FRender& GetRender()", "本帧驱动的那个 `FRender`（每次 `InitViews` 重新登记）—— "
            "组件靠它够到渲染后端，而**不需要**自己声明对渲染的依赖")
D.Interface("std::uint32_t GetEditorDockSpaceId() const",
            "宿主主 dockspace 的节点 id（帧壳的拥有者）。组件对它调 DockBuilder / "
            "`SetNextWindowDockID` 就能落进共享空间，不必自己知道任何 dock id")
D.Interface("void* GetUIRenderContext() const",
            "编辑器 ImGui 上下文的 **opaque 句柄**：面板建视图时用它登记 `FUIView::SetRenderContext`。"
            "通用翻译循环**按上下文筛选**视图，所以游戏上下文与编辑器上下文的视图永不串扰")
D.Interface("static std::uint32_t PresentTargetTextureId()",
            "本帧 live present 目标的 ImTextureID（编辑器构建里游戏合成的 UIRenderTarget，"
            "只在帧末解析到 EditorRT）。宿主的翻译步把这个 id 解析到 `R.GetPresentTarget()`，"
            "**而不是**按名字查镜像（该目标没有镜像/名字项）—— 于是组件用一个稳定 id 就能 "
            "`imgui::image` 当前屏幕表面")
D.Interface("static UI::FUIName PresentTargetName()",
            "present 目标作为 UI 树**资源引用**的名字。声明式面板只需声明一个带此名字的 `FUIImage`，"
            "宿主注入的解析器把名字映射回 `PresentTargetTextureId()` ⇒ 面板永远看不到 ImTextureID")
D.SetAccess("private")
D.Interface("void InstallEditorComponents()",
            "装入编辑器组件 DLL 并跑它们的 Init 图（安全点）。装的是「匿名 frame 扩展」—— "
            "组件不暴露自己的身份，只挂 `IEditor*` stage")
D.Interface("void ShutdownEditorComponents()", "卸出 4 个编辑器组件（**先跑它们的 Shutdown 图**）再释放")
D.Interface("void UpdateEditorPanels()",
            "声明期：先抽干各视图上一帧入队的交互事件，再让每个组件 `Update` 自己的 UI 树。"
            "**在 `NewFrame()` 之前**调用 —— 后端此刻还没有帧，组件也就**不可能**碰 ImGui")
D.Interface("void DrawEditorPanels()",
            "宿主自己的帧壳：自有上下文 → 全屏 DockSpace → 再走通用已注册视图循环"
            "（宿主只提供上下文 / 显示矩形 / dock id，UI 插件负责开窗并翻译树 —— 宿主不认识任何具体视图）")
D.Interface("void InitEditorViews(FRender& R)",
            "原来是 `IInitViews` stage，现在是 `EditorCompose` 的**建帧那一半**："
            "喂数据 → `NewFrame` → 面板 → Render → 翻译成 `FDrawList`。先跑")
D.Interface("void RenderEditorUI(FRender& R)",
            "原来是 `IRenderUI` stage，现在是 `EditorCompose` 的**合成那一半**："
            "把编辑器 ImGui 列表画进 EditorRT 并接管 present 目标。后跑")
D.Interface("bool EnsureUIBackend(FRender& R)", "惰性字体后端初始化（编辑器自有的字体纹理与暂存区），幂等")
D.Interface("void UploadFont(FRender& R)", "一次性字体图集上传：**transfer 提交**，"
            "在渲染通道里做是非法操作")
D.Field("ImGuiContext* m_Context = nullptr",
        "**自己的** ImGui 上下文（与游戏那套完全独立）。在 InitViews 里 "
        "`SetCurrentContext` 切进来，保证两套帧永不混；PreUnInstall 里 `DestroyContext`")
D.Field("bool bUIInit = false", "后端是否已初始化（配合 `EnsureUIBackend` 幂等）")
D.Field("bool bFontUploaded = false", "字体图集是否已上传（一次性）")
D.Field("std::vector<Platform::MInputEvent> EditorInputEvents",
        "单帧输入缓存：`EditorInput`（pass0，本帧最先跑）是**唯一**的排空 + 消费滚轮者，"
        "把排空到的事件批存在这里，好让 `InitEditorViews`（pass3，同一帧更晚）"
        "喂给编辑器上下文时**不必再排空一次平台** —— 这些是单消费者资源，第二次排空只会拿到空")
D.Field("float EditorWheelX = 0.f", "缓存下来的滚轮 X（已换成 0 的那份，见上面的注释）")
D.Field("float EditorWheelY = 0.f", "缓存下来的滚轮 Y")
D.Field("bool bEditorInputCached = false",
        "本帧 `EditorInput` 是否真的排空过平台（上面那份缓存是否新鲜）。"
        "为 true 时 `InitEditorViews` 才消费缓存；否则不碰输入流 —— "
        "那一帧的排空者是游戏 UI 上下文自己的整窗回退路径，再排一次也是空的")
D.Field("std::vector<std::string> DroppedFiles",
        "本帧的 OS 拖入批次（`EditorInput` 填、`EditorCompose` 末清）。与输入缓存同一份单帧契约")
D.Field("std::mutex ImGuiFrameMutex", "同一时刻只有一个线程持有编辑器的 ImGui 帧（NewFrame）")
D.Field("FRDGTextureRef EditorRT",
        "编辑器的离屏合成目标：尺寸/格式跟随 swapchain canvas。RenderUI 把编辑器 ImGui 列表"
        "（含 SceneColor 视口图）画进去，然后它成为 present 目标")
D.Field("FRDGTextureRef FontTexture", "编辑器自有的字体纹理（池持有的持久资源）")
D.Field("FDrawList DrawList", "本帧翻译后的 ImDrawData → 渲染列表")
D.Field("FRender* RenderRef = nullptr", "当前帧的渲染上下文（供组件使用），每次 InitViews 更新")
D.Field("FEditorContext EditorContext", "共享的组件状态本体")
D.Field("std::uint32_t EditorDockSpaceId = 0", "宿主 DockSpace 节点 id，每次 `DrawEditorPanels` 重新取")
D.Field("float VpX = 0.f, VpY = 0.f, VpW = 0.f, VpH = 0.f",
        "游戏 UI 被显示到的视口面板矩形（客户区像素），由视口组件 `ReportViewportRect` 发布；"
        "`EditorInput` 用它把游戏光标重定位回面板")
D.Field("bool bVpValid = false", "面板是否已经至少被画过一次且尺寸为正 —— 在此之前重定位没有意义")

D.Card("跨 DLL 导出面")
D.Table("符号", "说明")
D.Row("Maho::FExampleEditor", "宿主 `FRender` 构造 / 析构它 ⇒ 必须导出")
D.Row("Maho::IEditorInit / IEditorPanel / IEditorShutdown", "组件插件在自己模块里继承它们并 override ⇒ 必须导出，"
      "否则派生类 vtable 会落在错误模块")
D.Row("Maho::FEditorShader", "`TryGetShader<FEditorShader>()` 在 FRender 侧实例化 ⇒ 其成员必须由本模块提供")
D.Row("CreateFrame()（`Private/ExampleEditor.cpp`）", "按符号名装载的 C 导出（模块基名 = 类型名）")
