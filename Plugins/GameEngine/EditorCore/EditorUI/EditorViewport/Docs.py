# EditorViewport 插件的内容脚本（plugin_docs.py 会 exec 本文件，docs_builder 已注入为 D）。
# 内容与磁盘上的 Public/*.h 逐字对应。

# ══════════════════════════════════════════════════════════════════════════════
# Public/EditorViewportApi.h —— 导出宏
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/EditorViewportApi.h", Title="EditorViewportApi.h —— 导出宏",
         Desc="本插件的导出开关：`MAHO_EDITORVIEWPORT_API` 按 `MAHO_EDITORVIEWPORT_MODULE_EXPORTS` "
              "在 `MAHO_EXPORT` / `MAHO_IMPORT` 之间切换。\n"
              "**当前没有类型挂它**：取景区面板只经 DLL 工厂 `CreateFrame()` 按符号名被构造，"
              "没有别的模块 include 它的类型，所以它不需要导出标记。宏按惯例保留。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT` 两个底层标记")

D.Macro("MAHO_EDITORVIEWPORT_API", "MAHO_EXPORT / MAHO_IMPORT（按 MAHO_EDITORVIEWPORT_MODULE_EXPORTS 切换）",
        Desc="面板类当前不带它（它只作 DLL 工厂的产物存在）。")

# ══════════════════════════════════════════════════════════════════════════════
# Public/EditorViewport.h —— 取景区面板
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/EditorViewport.h", Title="EditorViewport.h —— 取景区面板",
         Desc="`FEditorViewport` 是最小的编辑器组件：挂 `IEditorPanel`（加 `IEditorShutdown` 收摊），"
              "由宿主 `FExampleEditor` 每帧驱动。它拥有一棵持久声明式 UI 树：`Update`"
              "（宿主 `NewFrame` **之前**的声明期）重建它，宿主在帧内翻译。\n"
              "整棵树就是**一张铺满面板的图片**，按**名字**引用 live present 目标"
              "（`FExampleEditor::PresentTargetName()`）；宿主注入的解析器把那个名字映射到 RHI 纹理 —— "
              "所以这个组件**永远看不到 ImTextureID**，也不持有任何 ImGui / RHI 资源。\n"
              "**它同时是「游戏光标重定位」的信息源**：声明完树后把图片的屏幕矩形发布给宿主"
              "（`ReportViewportRect`），宿主的 `IEditorInput`（pass0）据此把游戏光标重定位进面板。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("EditorViewportApi.h", "`MAHO_EDITORVIEWPORT_API`：导出开关")
D.Row("Engine/Frame.h", "`MAHO_DECLARE_FRAME` / `FFrameExtension` / `IPipeline` —— 身份与 stage 列表")
D.Row("ExampleEditor.h", "`FExampleEditor`（宿主上下文：`GetUIRenderContext` / `PresentTargetName` / "
      "`ReportViewportRect`）与 `IEditorPanel` / `IEditorShutdown`")
D.Row("UIView.h", "`UI::FUIView`（持久树 + `Edit()`）与 `UI::FUIBuilder`（声明入口）")
D.Row("memory", "`std::unique_ptr<UI::FUIView> View`：面板拥有自己的树")

D.Class("FEditorViewport", Base="FFrameExtension + IPipeline<IEditorPanel, IEditorShutdown>",
        Desc="取景区面板本体：最干净的一个编辑器组件 —— 没有缓冲、没有订阅、没有状态位，"
             "只有一棵「一张图」的树。**它只挂两个 stage**：它没有 `Init` 要做的事，"
             "也没有需要额外顺序保证的初始化；多挂一个只会让宿主多派发一个空节点。")
D.SetAccess("public")
D.Interface("void Update(FExampleEditor& Editor) override",
            "声明期（宿主 `NewFrame` **之前**）：① `EnsureView`（UI 插件未就位则返回 nullptr、下一帧重试）；"
            "② 在 `Edit()` 里重建树 —— 根容器 Fill + **去掉主题内边距**（取景区要贴边铺满），"
            "唯一子节点是 Fill 的图片，引用 present target 的名字 + `Stretch` 缩放"
            "（只在新建那一帧播种：资源引用与缩放模式是「用户/别人可能改过」的配置，"
            "逐帧重写会把它们覆盖掉）；③ 读图片节点的屏幕矩形（翻译期后端写回，故是**上一帧**的值）"
            "并在尺寸为正时 `ReportViewportRect` 发布给宿主")
D.Interface("void Shutdown(FExampleEditor& Editor) override",
            "注销视图并释放。**注册表只持裸指针、从不删除** —— 漏掉注销，下一次翻译就会遍历到已销毁的树。"
            "先取注册表再 `UnregisterView`，注册表不存在（UI 插件已走）时跳过判空即可")
D.SetAccess("private")
D.Interface("UI::FUIView* EnsureView(FExampleEditor& Editor)",
            "惰性建视图 + 注册：**UI 插件在 install 时可能还没起来**，注册表为空就返回 nullptr、"
            "由调用方下一帧重试。外壳开窗（含宿主的 dockspace id）由宿主通用循环做；"
            "标题即旧窗口名，`NoCollapse | NoMove` 保留「取景区不可折叠不可拖动」的旧语义；"
            "渲染上下文用 `Editor.GetUIRenderContext()` 登记 ⇒ 通用翻译循环按上下文筛视图")
D.Field("std::unique_ptr<UI::FUIView> View", "持久 UI 树（一图铺满），归本面板所有并登记在 UI 视图注册表里")

D.Card("跨 DLL 边界")
D.Table("符号", "说明")
D.Row("CreateFrame()（`Private/EditorViewport.cpp`）", "宿主子收集器按符号名装载的 C 导出")
D.Row("present target 的名字引用", "组件只给名字（`UI::FUIName`），原生句柄由宿主解析 —— "
      "这条「不导出 ImGui 类型」的边界正是它不需要导出宏的原因")
