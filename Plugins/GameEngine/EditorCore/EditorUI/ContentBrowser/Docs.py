# ContentBrowser 插件的内容脚本（plugin_docs.py 会 exec 本文件，docs_builder 已注入为 D）。
# 内容与磁盘上的 Public/*.h 逐字对应。

# ══════════════════════════════════════════════════════════════════════════════
# Public/ContentBrowserApi.h —— 导出宏
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/ContentBrowserApi.h", Title="ContentBrowserApi.h —— 导出宏",
         Desc="本插件的导出开关：`MAHO_CONTENTBROWSER_API` 按 `MAHO_CONTENTBROWSER_MODULE_EXPORTS` "
              "在 `MAHO_EXPORT` / `MAHO_IMPORT` 之间切换。\n"
              "**当前没有任何类型挂它**：面板类 `FContentBrowser` 只被本插件自己用，"
              "别的模块既不 include 也不构造它 —— 它只经 DLL 工厂 `CreateFrame()` 按符号名被宿主装上，"
              "所以不需要导出标记。宏留着是为了「一旦出现跨 DLL 的类型，直接挂上」。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT` 两个底层标记")

D.Macro("MAHO_CONTENTBROWSER_API", "MAHO_EXPORT / MAHO_IMPORT（按 MAHO_CONTENTBROWSER_MODULE_EXPORTS 切换）",
        Desc="当前面板类不带它（面板只作 DLL 工厂的产物存在）；保留这个宏供将来真正跨 DLL 的类型使用。")

# ══════════════════════════════════════════════════════════════════════════════
# Public/ContentBrowser.h —— 内容浏览器面板
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/ContentBrowser.h", Title="ContentBrowser.h —— 内容浏览器面板",
         Desc="`FContentBrowser` 是编辑器**组件插件**：挂面板 stage（`IEditorPanel`）与卸载 stage"
              "（`IEditorShutdown`），由编辑器宿主 `FExampleEditor` 每帧驱动。\n"
              "**它看到的是引擎的虚拟文件系统**：项目 Content 根（`Game`）与引擎 Content 根（`Engine`）"
              "由 `FPaths` 注册，按需扫描后显示成 UE 风格的双栏浏览器（目录树 + 条目网格 + 面包屑 + 搜索）。"
              "**只列容器资产（`.casset`）**：这是资产视图，不是文件夹视图 —— 躺在旁边的 `.png` 看不见。\n"
              "**面板拥有一棵持久声明式 UI 树**（`UI::FUIView`）：`Update`（宿主开帧 UI **之前**的声明期）"
              "重建这棵树，宿主随后在帧内翻译它。**插件从不碰后端**（这里没有任何 ImGui 类型或调用）。\n"
              "**拖入**：宿主把本帧 OS 拖入的文件交给面板（`FExampleEditor::GetDroppedFiles`），"
              "面板**只在自己某个节点被悬停时**认领，并把每个文件路由进资源系统"
              "（`.casset` → 它记录的资产类型；光栅图 → `FTexture2D`；其它 → 记一条「不支持」日志）。"
              "**面板从不写盘**：导入只是登记一个资源，文件留在 OS 放它的地方。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("ContentBrowserApi.h", "`MAHO_CONTENTBROWSER_API`：导出开关（面板类当前未挂，见上一页说明）")
D.Row("Engine/Frame.h", "`MAHO_DECLARE_FRAME` / `FFrameExtension` / `IPipeline` —— 身份与 stage 列表")
D.Row("ExampleEditor.h", "`FExampleEditor`（宿主上下文：`GetUIRenderContext` / `GetDroppedFiles` / "
      "`ConsumeDroppedFiles`）与 `IEditorPanel` / `IEditorShutdown` 两个 stage 接口")
D.Row("UIView.h", "`UI::FUIView`（持久树 + `Edit()` 独占写作用域）与 `UI::FUIBuilder`（声明入口）")
D.Row("filesystem", "`std::filesystem::path`：扫描与相对路径计算（虚拟路径 ↔ 物理路径）")
D.Row("memory", "`std::unique_ptr<UI::FUIView> View`：面板拥有自己的树")
D.Row("string", "`FNode` 的名字 / 虚拟路径、当前目录、状态行文本")
D.Row("unordered_map", "`Expanded`：面板侧的节点展开态权威表（见字段说明）")
D.Row("vector", "`Roots`（扫描出的根子树）与 `FNode::Children`")

D.Card("本面板的原理约定（为什么这么写）")
D.Table("约定", "说明")
D.Row("节点 id 稳定 = 复用",
      "同级唯一即可（事件路由按「根 → 目标」的 Id 路径走）。同 id 同类型重新声明 ⇒ 节点被复用，"
      "所以输入框缓冲、滚动量这类运行期状态跨帧存活")
D.Row("值回读先于声明",
      "翻译期后端**原地改写节点的值**（用户输入的真值在节点上），所以 `Update` 先把 `SearchBuffer` 收回来"
      "再按缓冲声明 —— 用户编辑因此跨帧存活；只有**新建**节点时才播种初值"
      "（`Ensure()` 报告是否新建，逐帧重播种会累积事件订阅）")
D.Row("展开态由面板侧保存",
      "树每帧被整体重建，节点自己的展开位会丢；`Expanded` 是权威值，回调在 `Update` **之前**被抽干，"
      "所以它与节点同帧一致。缺省 = 目录展开（旧的 `ImGuiTreeNodeFlags_DefaultOpen`）")
D.Row("悬停归属靠上一帧状态",
      "新树的事件不携带指针位置，无法直接做旧的 `IsWindowHovered`；改为按「上一帧翻译写回的节点悬停位」近似。"
      "GLFW 把落点写进光标流之后本帧才收到投放事件，故「上一帧悬停在浏览器上」与旧判定同义")
D.Row("列数由上一帧实测宽度算",
      "旧的 `GetContentRegionAvail().x` → 上一帧的正文区宽度（外框扣面板内边距）"
      "除以单元宽 + 间距；声明期只把**算好的列数**交给网格")

D.Class("FContentBrowser", Base="FFrameExtension + IPipeline<IEditorPanel, IEditorShutdown>",
        Desc="内容浏览器面板本体：两个基类（`FFrameExtension` 声明层 + 挂面板/卸载两个 stage 的"
             "有序列表）。**只挂它真正实现的 stage**：多挂一个（例如 `IEditorInit`）会让宿主"
             "多派发一个它没有语义的节点。\n"
             "它**不持有任何后端 / RHI 资源** —— UI 上下文归宿主所有；它只持有一棵视图树与一份扫描结果。"
             "树上的一切都在 `Update` 里、在 `Edit()` 独占写作用域内改写，"
             "翻译线程遍历时持共享读，两边都不跨边界持树。")
D.SetAccess("public")
D.Interface("void Update(FExampleEditor& Editor) override",
            "声明期（宿主 `NewFrame` **之前**）：只重建本视图的树，不碰后端。顺序是："
            "① `EnsureView`（UI 插件未就位则返回 nullptr，下一帧再试）；"
            "② 首帧或刷新后 `Rescan`（遍历目录是文件系统访问，**不能**做成逐帧成本）；"
            "③ 从输入框节点**回读**搜索缓冲；④ 定位当前目录节点（重扫把它删了就回落到第一个根）；"
            "⑤ 判悬停归属 + 算网格列数（都基于上一帧翻译写回的状态）；"
            "⑥ 在 `Edit()` 里重建整棵树；⑦ 若本帧拖入的文件归属本面板，认领并导入")
D.Interface("void Shutdown(FExampleEditor& Editor) override",
            "注销视图。**注册表只持裸指针、从不删除**实例 —— 所以这里必须显式注销，"
            "否则下一次翻译会遍历到一个已销毁的树")
D.SetAccess("private")
D.Nested("FNode", Kind="struct",
         Desc="一个被扫描出来的目录/资产的节点。字段：`std::string Name`（显示名 = 虚拟路径最后一段）、"
              "`std::string VirtualPath`（显示用路径，如 `Game/Textures`）、"
              "`bool bDirectory`（目录还是资产）、`std::vector<FNode> Children`（子节点）。"
              "**顺序**：目录在前、资产在后，各自按名字字典序（UE 风格列表）。"
              "**按值持子树**：扫描结果一旦建好就不再变（直到下次 `Rescan`），"
              "`FindNode` 返回的指针在下次 `Rescan` 之前一直有效")
D.Interface("void Rescan()",
            "把两个根重新扫进 `Roots`。**只在首次显示 / 点 Refresh 时调**（目录遍历是文件系统命中，"
            "不是逐帧成本）。`FPaths` 未初始化时记一条错误状态并直接返回；"
            "某个根**物理目录不存在是合法的空根**（不是错误），面板在那下面什么都不显示。"
            "最后统计资产数写进状态行并 `MAHO_LOG` 一条 Info")
D.Interface("static void ScanDirectory(const std::filesystem::path& PhysicalDir, std::string VirtualPrefix, FNode& Out)",
            "递归收集 `PhysicalDir` 下的 `.casset`，命名为 `VirtualPrefix/...`。"
            "点号开头的隐藏项不进内容视图（隐藏目录整棵子树跳过）；"
            "中间目录按需建节点使树镜像磁盘；末尾对每层做「目录先、资产后 + 字典序」排序。"
            "权限错误按 `error_code` 吞掉并停止该层遍历（扫描不该抛异常穿过帧）")
D.Interface("[[nodiscard]] const FNode* FindNode(std::string_view VirtualPath) const",
            "按虚拟路径找扫描节点：先比每个根，再对该子树深度优先。返回**指向树内的指针**，"
            "故在下次 `Rescan` 之前有效；找不到返回 nullptr")
D.Interface("[[nodiscard]] std::string ToVirtualPath(const std::filesystem::path& PhysicalPath) const",
            "物理投放路径 → 虚拟路径：在某个注册根之下就是 `Game/...` / `Engine/...`，"
            "否则**原样返回物理路径** —— `FPaths::Resolve` 会把未映射的路径逐字透传，"
            "所以资源键始终有定义（从桌面直接拖进来的文件也能导入，且不会被拷进 Content/）")
D.Interface("UI::FUIView* EnsureView(FExampleEditor& Editor)",
            "惰性建视图 + 注册：**UI 插件在 Init 时可能还没起来**，注册表为空就返回 nullptr、"
            "由调用方下一帧重试。外壳开窗（含宿主的 dockspace id）由宿主的通用循环做；"
            "渲染上下文用 `Editor.GetUIRenderContext()` 登记 —— 通用翻译循环按上下文筛视图，"
            "编辑器与游戏两套上下文因此互不串扰")
D.Interface("void DeclareTree(UI::FUIBuilder& Parent, const FNode& Node)",
            "声明期：把一棵扫描子树的节点铺到 `Parent` 下（递归，折叠的子树直接返回）。"
            "展开态取面板侧 `Expanded`（缺省 = 目录展开）。**整行可点**：新控件把「箭头折叠」与"
            "「标签浏览」合成一次点击，所以只有**向展开方向**的点击才跟随切换右栏 —— "
            "否则「折叠某个目录」会顺带切走当前目录")
D.Interface("void DeclareBreadcrumb(UI::FUIBuilder& Row)",
            "声明期：面包屑行，每段一个按钮（`/Game/Textures/Env`），点哪段就跳到哪段。"
            "旧版是 `TextUnformatted(\"/\")` + `SmallButton(段名)`，这里合成一个按钮 —— 视觉等价，"
            "且省掉分隔符节点的独立 Id")
D.Interface("void DeclareItems(UI::FUIBuilder& Parent, const FNode& Node, int Columns)",
            "声明期：当前目录（**搜索时 = 当前根下所有名字匹配的资产**）的条目网格。"
            "空结果显示「No matching asset.」/「No asset here.」；每个资产单元是拖放源"
            "（载荷 = 资产的虚拟路径），并挂一个悬停提示 = 虚拟路径 —— 提示节点必须挂在被悬停的控件**之下**，"
            "翻译器是按父节点的悬停位开提示窗的")
D.Interface("[[nodiscard]] bool IsAnyNodeHovered(const UI::FUIBuilder& Node) const",
            "本视图内是否有节点被悬停 —— 旧 `IsWindowHovered` 的近似：新树的事件不携带指针位置，"
            "只能按**上一帧翻译写回**的节点悬停位判定（递归到子树）")
D.Interface("void ImportDroppedFiles(FExampleEditor& Editor)",
            "认领 + 导入本帧的拖入文件（悬停门槛由把守）。用 "
            "`ConsumeDroppedFiles` ⇒ **先到先得**：别的面板已经拿走时这里拿到空列表，同一文件不会被导入两次")
D.Interface("bool ImportFile(const std::string& PhysicalPath)",
            "导入单个文件：读原始字节 → 问资源系统是否存在 → 由**容器头**决定资产类型"
            "（`PeekCassetAssetType`：Texture / Material / StaticMesh / Skeleton / Animation / "
            "AnimationGraph / Prefab），跳过所有 ImGui 步骤。**导入是异步的** —— "
            "本函数只报告「传输是否被入队」，解码完成时由资源系统自己记录/广播结果。"
            "什么也没入队时返回 false")
D.Interface("void SetStatus(bool bError, std::string Text)",
            "写状态行（`bError = true` 时状态文本用红色样式）。面板的所有反馈都只落在这一行 + `MAHO_LOG`，"
            "不弹窗、不阻塞帧")
D.Field("std::vector<FNode> Roots", "扫描出的根子树（`Game` / `Engine`；缺失的根被跳过）")
D.Field("bool bScanned = false", "是否已经扫过至少一次（首帧与刷新用同一个入口）")
D.Field("std::string CurrentPath = \"Game\"", "当前浏览的目录（面包屑末段 = 它）")
D.Field("std::string SelectedPath", "当前选中的资产虚拟路径（空 = 无选中；用于网格的选中框）")
D.Field("char SearchBuffer[128] = {}", "搜索框缓冲（每帧从输入框节点回读，用户输入的真值在节点上）")
D.Field("std::string Status", "上次扫描 / 导入的结果，显示在面板的状态行")
D.Field("bool bStatusIsError = false", "状态行是错误还是普通信息（决定文本颜色）")
D.Field("std::unique_ptr<UI::FUIView> View", "持久 UI 树，归本面板所有并登记在 UI 视图注册表里")
D.Field("std::unordered_map<std::string, bool> Expanded",
        "树节点的展开态。展开位本来是节点自己的持久字段，但树每帧整体重建会把它丢掉，"
        "故面板侧记一份**权威值**（回调在 `Update` 之前被抽干，故它与节点同帧一致）。"
        "缺省：目录展开（对应旧的 `ImGuiTreeNodeFlags_DefaultOpen`）")

D.Card("跨 DLL 边界")
D.Table("符号", "说明")
D.Row("CreateFrame()（`Private/ContentBrowser.cpp`）", "宿主子收集器按**符号名**装载的 C 导出 —— "
      "面板类型本身不导出，面板模块只以这个入口与外界相接")
D.Row("面板持有所在图（`View`）", "注册表里是裸指针 ⇒ 卸载期必须先注销再销毁（见 `Shutdown`）")
