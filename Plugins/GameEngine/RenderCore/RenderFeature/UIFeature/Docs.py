# -*- coding: utf-8 -*-
# UIFeature 插件文档内容（由 Tools/plugin_docs.py 执行，docs_builder 以变量 D 注入）。

# ══════════════════════════════════════════════════════════════════════════════
# Public/UIFeatureApi.h —— 模块导出标签
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/UIFeatureApi.h", Title="UIFeatureApi.h —— 模块导出标签",
         Desc="本特性的 DLL 导出标签。**这里导出的东西比别的特性多一层意义**："
              "`GetUI()` 与 `FUIFeature` 被编辑器特性跨模块直接调用"
              "（取游戏 UI 的 ImGui 上下文、喂重基后的输入），"
              "方法调用要跨 DLL 进来，所以类型必须有稳定的 vtable 归属。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT`")

D.Card("导出标签")
D.Table("宏", "说明")
D.Row("MAHO_UIFEATURE_API",
      "编译本模块时 codegen 定义 `MAHO_UIFEATURE_MODULE_EXPORTS` ⇒ `MAHO_EXPORT`。"
      "**为什么 `FUIFeature` 必须导出：**编辑器特性经 `GetUI()` 拿到指针后**跨模块调用它的方法**"
      "（`GetImGuiContext()` / `SetEditorInput()`）—— 这些是虚表之外也有意义的非虚方法，"
      "但实例仍由引擎侧销毁（卸载），vftable 归属必须落在本 DLL")

# ══════════════════════════════════════════════════════════════════════════════
# Public/UIFeature.h —— ImGui 渲染特性（UI 上下文 + 帧生命期所有者）
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/UIFeature.h", Title="UIFeature.h —— ImGui 渲染特性",
         Desc="ImGui 渲染特性 —— **UI 的 CPU 侧 ImGui 上下文与整个帧生命期的所有者**。"
              "它在 `OnInstalled` / `PreUnInstall` 建 / 毁 ImGui 上下文，并在 `InitViews` 里驱动整帧："
              "喂输入 → `NewFrame` → 通用注册视图循环（UI 插件把本上下文注册过的视图逐个打开并翻译，"
              "游戏侧只声明持久组件树、不写 ImGui 调用）→ `Render` → `GetDrawData` → "
              "把绘制数据翻译成 GPU 缓冲（就在 `InitViews` 里上传）+ 一张持有引用的 `FDrawList`。"
              "`RenderUI` 把这张列表画进**本特性自己的**离屏合成目标（`UIRenderTarget`，"
              "按交换链画布尺寸 + 格式建）并把它设为 `FRender` 的上屏目标；"
              "帧特性的 `IPresent` 再负责 blit 上屏 —— **UI 是最终屏幕表面**，"
              "场景是经游戏侧的 `imgui::image SceneColor` 控件**采样进**它里面的。\n"
              "本特性**只做离屏合成**：它不再拥有上屏点（那归帧特性）。`FRender` 因此完全与 UI 无关 —— "
              "它不持有任何 ImGui 状态、也不链接 / 引用 ImGui。\n"
              "它还把**游戏** ImGui 上下文经 UI 插件发布出去（`UI::SetUIGameRenderContext`），"
              "于是游戏侧的视图所有者（`FUISystem`）能给自己的视图打上这个上下文的标记，"
              "**而不需要一条 UISystem → UIFeature 的构建依赖**。\n"
              "**无状态绘制特性**：UI 着色器走 `FRender::TryGetShader<FUIShader>`（异步编译 + 按类型缓存）；"
              "字体后端**只持有** RDG 字体纹理句柄。这里**没有裸 RHI 指针**："
              "描述符 set 布局、描述符 set、采样器都是按需从资源池重新解析出来的"
              "（内容可寻址 get-or-create，键就是产生它们的那组 PassParameter 绑定值）。"
              "池拥有所有原生的生命期，所以本特性既不拥有也不拆解任何资源；"
              "它只持有翻译出的 `FDrawList`（成员，跨帧复用容量）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("UIFeatureApi.h", "`MAHO_UIFEATURE_API`")
D.Row("Engine/Frame.h", "`FFrameExtension` / `MAHO_DECLARE_FRAME` / 依赖声明")
D.Row("Render.h", "`FRender` / `IPipeline` / stage 接口 `IOnInstalled` / `IInitViews` / `IRenderUI` / "
                 "`IPreUnInstall` / `TShaderHandle`")
D.Row("RDG.h", "`FRDGTextureRef`（字体纹理 / 合成目标）")
D.Row("RHI/RHIResources.h", "`FRHISampler` / `FRHIDescriptorSetLayout` 等（都是池拥有的借用句柄）")
D.Row("RenderDrawList.h", "`FDrawList`（翻译出的绘制数据，成员跨帧复用）")
D.Row("cstdint / mutex", "定宽整数 / `ImGuiFrameMutex`（整帧串行化）")
D.Row("Platform.h", "`Platform::MInputContext` / `MInputEvent`（编辑器输入接管的参数类型）")

D.Card("前置声明")
D.Table("声明", "说明")
D.Row("struct ImGuiContext",
      "**在全局命名空间**（ImGui 自己声明的）。本头只用它当指针成员，所以前向声明就够 —— "
      "这样 Render.cpp（以及任何包含本头的消费方）**不需要 include imgui.h**")

D.Struct("FUIShader", Desc="被 `FRender::TryGetShader<T>` 消费的着色器类型（与 `FTriangleShader` 完全同一个契约；"
        "UI 着色器路径就是那条通用路径）。**纯静态**访问器 —— 编译 / 字节码缓存 / 内容哈希都在共享的 "
        "`TShaderHandle` 状态里，所以特性不拥有字节码也不拥有原生模块。静态体定义在 .cpp"
        "（GLSL 串保持特性私有）。")
D.Interface("static const char* GetVertexSource()", "顶点 GLSL 源码")
D.Interface("static const char* GetFragmentSource()", "片段 GLSL 源码")
D.Interface("static const char* GetVertexEntryPoint()", "顶点入口名")
D.Interface("static const char* GetFragmentEntryPoint()", "片段入口名")

D.Class("FUIFeature", Base="FFrameExtension, IPipeline<IOnInstalled, IInitViews, IRenderUI, IPreUnInstall>",
        Desc="ImGui 渲染特性。挂四个 stage：`IOnInstalled`（建上下文）、`IInitViews`（驱动整帧 + "
             "上传几何）、`IRenderUI`（离屏合成 + 设上屏目标）、`IPreUnInstall`（毁上下文）。"
             "**注意它不挂 `IPresent`** —— 上屏点归帧特性，这是刻意的职责切分。")
D.SetAccess("private")
D.Interface("FUIFeature()",
            "构造：声明依赖。① `MyStage<IRenderUI>().IsWaiting<Scene::FScene>().ForStage<IEndRender>()` "
            "+ 同一个 stage 等 `FDrawTriangleFeature` 的 `IEndRender` —— UI 的绘制与提交是**最后**的，"
            "于是它合成在场景之上（场景那些提交先到队列）。"
            "② `MyStage<IInitViews>().IsWaiting<Scene::FScene>().ForStage<IBeginRender>()` —— "
            "**数据流边，不是提交顺序边**（见 Scene.h 的说明：尺寸变化时镜像会被重建）。"
            "③ `MyStage<IRenderUI>().IsBlocking<FFrameRenderFeature>().OnStage<IPresent>()` —— "
            "**反向边**：把帧特性的 `IPresent` 钉在本特性的 `IRenderUI` 之后，"
            "于是它消费的是本帧刚设好的上屏目标")
D.SetAccess("public")
D.Interface("void OnInstalled(FRender& R) override",
            "装载时**创建 ImGui 上下文**（在任何东西碰 `ImGui::GetIO()` 之前 —— "
            "`EnsureUIBackend` 会读 `GetIO().Fonts`），并登记全局访问器。"
            "窗口闸门与原 `FRender::Initialize` 的检查一致：**没有窗口 ⇒ UI 停用、上下文不创建**，"
            "`InitViews` 每帧直接退出")
D.Interface("void InitViews(FRender& R) override",
            "**整帧都在这里**（`ImGuiFrameMutex` 串行化）：喂输入 → `SetCurrentContext` 选本上下文 → "
            "`NewFrame` → `UI::TranslateRegisteredViews`（UI 插件把注册在本上下文上的视图逐个打开 + 翻译，"
            "游戏侧只声明组件树）→ `Render` → `GetDrawData` → 翻译成 GPU 缓冲（**在这里上传**）+ "
            "填入成员 `DrawList`。编辑器构建里若本帧已有输入接管（`bEditorInputThisFrame`），"
            "**跳过自己的 OS 轮询**（见 `SetEditorInput`），消费后把标志复位")
D.Interface("void RenderUI(FRender& R) override",
            "把 `DrawList` 画进本特性**自己的**离屏合成目标 `UIRenderTarget`，然后 "
            "`SetPresentTarget(UIRenderTarget)` —— 帧特性的 `IPresent` 会 blit 它。"
            "编辑器构建里，若下游要采样这个目标，在末尾调 "
            "`TransitionUIRenderTargetForSampling`")
D.Interface("void PreUnInstall(FRender& R) override",
            "卸载前**销毁 ImGui 上下文**并清全局访问器（`GetUI()` 之后返回 nullptr）。"
            "放在卸载流程最前，是因为卸载之后本 DLL 的代码就不能再被调用了")
D.Interface("[[nodiscard]] FRDGTextureRef GetUIRenderTarget() const",
            "Pass2 合成目标（游戏视图）。编辑器合成（pass3）读它，把游戏画面当视口背景嵌进去"
            "（经 `imgui::image`）。在第一个有合法画布的帧的 `InitViews` 里创建之前是空的")
D.Interface("void TransitionUIRenderTargetForSampling(FRender& R)",
            "编辑器构建的交接点：把 `UIRenderTarget` 从 **COLOR_ATTACHMENT**（UI 刚写完它）"
            "切到 **SHADER_READ_ONLY**，好让 pass3 能用 `imgui::image` 把它当视口的上屏目标采样"
            "（RHI 从不自动转换布局，而描述符写入写死 `SHADER_READ_ONLY`）。"
            "在 `RenderUI` **末尾**、上屏目标会被下游采样时调用；当前不是 COLOR_ATTACHMENT 时是空操作")
D.Interface("[[nodiscard]] ImGuiContext* GetImGuiContext() const",
            "本特性**自己的** ImGui 上下文。编辑器构建里编辑器特性（pass0 `IEditorInput`）"
            "需要**游戏上下文**的 IO 来喂重基后的输入，于是 `GetUI()` + 本函数取它。"
            "`OnInstalled` 之前 / `PreUnInstall` 之后可能为空")
D.Interface("void SetEditorInput(float X, float Y, bool B0, bool B1, bool B2, "
            "const Platform::MInputContext& Snap, const std::vector<Platform::MInputEvent>& Events, "
            "float WheelX, float WheelY)",
            "**编辑器构建的输入接管**。由编辑器的 pass0 `IEditorInput` stage 在本特性 `InitViews` "
            "**之前**调用：喂进光标（编辑器已把它重基到**本上下文**的整窗 `DisplaySize` 坐标 —— "
            "面板局部坐标经「面板 → 窗口」缩放映射回来）与鼠标按键，"
            "外加编辑器已经从平台收割的**完整键盘 + 字符 + 滚轮**输入"
            "（编辑器是本帧**唯一**的 drain / consume 消费者）。"
            "`DisplaySize` 保持整窗（游戏布局不会缩放到面板）。`InitViews` 随后凭 "
            "`bEditorInputThisFrame` 跳过自己的 OS 轮询 —— 于是游戏 UI 只在视口面板内响应，"
            "但仍然能收到按键 / 字符 / 滚轮。**线程安全**（与 `InitViews` 同在 `ImGuiFrameMutex` 之后串行化）；"
            "上下文没创建时是空操作")
D.SetAccess("private")
D.Interface("bool EnsureUIBackend(FRender& R)",
            "惰性创建字体后端（字体纹理 + staging），返回是否就绪；**幂等**")
D.Interface("void UploadFont(FRender& R)",
            "一次性字体图集上传（一次传输提交 —— **在渲染通道里做是非法操作**）。"
            "第一次之后就空操作")
D.Field("std::mutex ImGuiFrameMutex",
        "**整帧串行化**。ImGui 的 `GImGui` 是**进程级、非线程安全**的状态机"
        "（CurrentWindow 栈、FrameCount/FrameCountEnded、DrawData…），而 `InitViews` 跑在任意的渲染池 "
        "worker 上、不同帧可能落到不同 worker —— 所以从「喂输入」到「翻译完绘制数据」"
        "整段都在这个锁后面。**同一时刻只有一个线程拥有「当前帧」**，"
        "这满足了 ImGui 的单所有者契约，又不需要为它单独开一条线程（其他渲染特性照旧并行）")
D.Field("bool bUIInit = false", "UI 是否已初始化（字体后端等一次性准备）")
D.Field("bool bFontUploaded = false", "字体图集是否已上传（一次性传输提交）")
D.Field("bool bContextCreated = false",
        "`ImGui::CreateContext()` 是否已跑过 —— **本特性拥有 CPU 侧的 ImGui 上下文**："
        "`OnInstalled` 建、`PreUnInstall` 毁。它守着每一次帧喂入 / `InitViews` 入口")
D.Field("ImGuiContext* m_Context = nullptr",
        "**本特性自己的** ImGui 上下文。两个 UI 特性（游戏 + 编辑器）共享进程内的 imgui DLL，"
        "但各自拥有**不同的上下文**；`InitViews` 用 `SetCurrentContext` 选中它，"
        "这样一帧永远不会建在另一个上下文上")
D.Field("bool bEditorInputThisFrame = false",
        "编辑器输入接管标志：由 `SetEditorInput`（编辑器的 pass0 stage）置位；"
        "`InitViews` 读它决定是否跳过自己的 OS 轮询，并在每帧消费后复位")
D.Field("FRDGTextureRef FontTexture",
        "pass 级字体纹理（池拥有的 persistent）。每次 `RenderUI` 经 `FUIParameters` 绑定；"
        "采样器是一个池化的 clamp 采样器（内容可寻址）")
D.Field("FRDGTextureRef UIRenderTarget",
        "本特性自己的最终屏幕合成目标：按交换链画布几何 + 格式建（尺寸变化时重建），"
        "于是 `IPresent` 到后缓冲的 blit 在格式与几何上都一致。`RenderUI` 把 ImGui 列表"
        "（含游戏侧的 `imgui::image SceneColor` 采样）画进它（`LoadOp::Clear` —— 这个表面每帧全重画）。"
        "池拥有的 persistent，`PreUnInstall` 时释放")
D.Field("bool bUIRenderTargetLayoutSR = false",
        "`UIRenderTarget` 的布局追踪器。它既是动态渲染的颜色附件（`RenderUI` 写它），"
        "在编辑器构建里又是被采样镜像（pass3 视口对它的 `imgui::image`）—— 两种用途需要相反布局，"
        "而 RHI 从不自动转换，所以每帧翻：`RenderUI` 留下 COLOR_ATTACHMENT，"
        "`TransitionUIRenderTargetForSampling()` 翻到 SHADER_READ_ONLY 供 pass3 采样，"
        "`RenderUI` 开头再翻回去（任何写入之前）。新目标（从未转换）是 Common，需要一次初始提升")
D.Field("FDrawList DrawList",
        "本帧翻译出的 `ImDrawData → FDrawList`：在 `InitViews` 填（整个 ImGui 帧生命期都在那里，"
        "GPU 缓冲也在那里上传），在 `RenderUI` 画（同一张图内自推进）。做成成员是为了让"
        "合并后的图元 + 批次向量跨帧复用容量")

D.Card("ImGui 纹理 id 语义（读 Private/UIFeature.cpp）",
       "这张表解释了「每批次描述符集」在 UI 这条路径上是怎么用的，也是「池拥有原生」的直接体现。")
D.Table("ImTextureID", "说明")
D.Row("0", "选中 **pass 级字体 set**（`FontTexture` + 池化 clamp 采样器，经 `FUIParameters` 绑定）")
D.Row("非 0", "一个镜像 `FName` 的 id（`FName::GetId()`）：per-batch set 经 "
              "`FName::FromId(id)` → `FRender::GetMirror` → `FRDGTextureRef` 解析，"
              "采样器按描述重新解析出一个池化 clamp 采样器 —— **这里永远不持有描述符集或采样器**，"
              "原生生命期全归池")

D.Card("自由函数")
D.Table("签名", "说明")
D.Row("MAHO_UIFEATURE_API FUIFeature* GetUI()",
      "UI 渲染特性的全局访问器（跨 DLL，与 `Resource::GetResourceSystem()` / `Log::GetLog()` 同风格）。"
      "特性被 FRender 装载之前是 nullptr；`OnInstalled` 设置、`PreUnInstall` 清空。"
      "编辑器特性（pass0）就是经它触达游戏 UI 上下文并喂重基输入的")
