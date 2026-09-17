# -*- coding: utf-8 -*-
# Scene 插件文档内容（由 Tools/plugin_docs.py 执行，docs_builder 以变量 D 注入）。

# ══════════════════════════════════════════════════════════════════════════════
# Public/SceneApi.h —— 模块导出标签
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/SceneApi.h", Title="SceneApi.h —— 模块导出标签",
         Desc="本特性的 DLL 导出标签：`GetScene()` 访问器与 `FScene` 类型都跨模块使用"
              "（别的特性经 `Scene::GetScene()` 读共享目标），所以两者都要导出。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT`")

D.Card("导出标签")
D.Table("宏", "说明")
D.Row("MAHO_SCENE_API",
      "编译本模块时 codegen 定义 `MAHO_SCENE_MODULE_EXPORTS` ⇒ `MAHO_EXPORT`。"
      "**为什么必须导出 `FScene` 本身：**别的特性只是读它的目标（引用 / 指针），"
      "但它的实例由引擎侧经基类指针销毁（卸载时），跨模块 `delete` 需要 vftable 归属明确")

# ══════════════════════════════════════════════════════════════════════════════
# Public/Scene.h —— 全局渲染资源特性（共享场景目标）
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Scene.h", Title="Scene.h —— 全局渲染资源特性（共享场景目标）",
         Desc="`Scene::FScene` 是**全局渲染资源特性**（对齐 UE 的做法）：它挂渲染录制阶段的 stage，"
              "**跨帧拥有共享的场景目标**（`SceneColor` / `SceneDepth`）。别的特性经 "
              "`Scene::GetScene()` 读它们 —— **不在 `FRender` 里开命名槽位**："
              "要共享的东西提供者是明确的一个，用访问器比用「按名字查表」更直接"
              "（名字查表还会把共享变成隐式依赖）。目标在交换链尺寸变化时重建。\n"
              "它同时是**绘制协议的生产者**（硬编码的全屏三角形），"
              "也是「布局切换」的负责人（见下面的卡片）。")
D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("SceneApi.h", "`MAHO_SCENE_API`")
D.Row("Maho.h", "引擎聚合头（日志 / 断言）")
D.Row("Engine/Frame.h", "`FFrameExtension` / `MAHO_DECLARE_FRAME` / 依赖声明")
D.Row("Render.h", "`FRender` / stage 接口（`IBeginRender` / `IRender` / `IEndRender` / `IPreUnInstall`）")
D.Row("RDG.h", "`FRDGTextureRef`（共享目标的引用类型）")
D.Row("RenderDrawList.h", "`FDrawList` / `FDrawBatch`（本特性作为绘制生产者）")
D.Row("cstdint", "定宽整数（缓存的宽高）")

D.Card("自由函数")
D.Table("签名", "说明")
D.Row("MAHO_SCENE_API FScene* GetScene()",
      "全局访问器（Scene.dll）。返回 nullptr = 场景特性还没装或已卸载 —— "
      "**消费方必须先判空**：特性装载顺序不该成为隐藏的加载期依赖")

D.Class("FScene", Base="FFrameExtension, IPipeline<IBeginRender, IRender, IEndRender, IPreUnInstall>",
        Desc="共享场景目标 + 场景绘制阶段的所有者。它挂四个 stage：`IBeginRender`（建 / 重建目标）、"
             "`IRender`（录场景清理 / 绘制）、`IEndRender`（收尾 —— **别人等的是这个**）、"
             "`IPreUnInstall`（卸载前释放共享目标）。")
D.SetAccess("public")
D.Interface("FScene()",
            "构造：① 把自己登记为全局单例（`GScene = this`，给 `GetScene()`）；"
            "② 预置绘制协议的测试数据 —— 硬编码全屏三角形（`VertexCount = 3`，**没有顶点缓冲**，"
            "顶点着色器用 `gl_VertexIndex` 现场生成三个位置，所以 `AddPass` 录成 `Draw(3)`）。"
            "**为什么 `IBeginRender` 取列表不会与回收抢：**宿主 `FRender::BeginFrame`"
            "（引擎 stage）已经等过上一帧栅栏、回收过上一帧列表并开好了帧缓冲")
D.Interface("~FScene() override", "析构：清空全局单例（`GScene = nullptr`）—— 之后 `GetScene()` 返回空")
D.Interface("[[nodiscard]] FRDGTextureRef GetSceneColor() const",
            "共享场景颜色目标（**双重身份**：既是渲染目标，也是被采样源 —— 见下面的布局卡片）")
D.Interface("[[nodiscard]] FRDGTextureRef GetSceneDepth() const", "共享场景深度目标")
D.Interface("[[nodiscard]] const FDrawList& GetTriangleDrawList() const",
            "绘制协议的**测试生产者**：硬编码的全屏三角形列表。`AddPass` 原样消费它，"
            "**永远不知道它来自这里** —— 以后换成真正的场景渲染器时消费方一行不改")
D.Interface("void BeginRender(FRender& R) override",
            "帧首：`EnsureTargets(R)` 按画布尺寸建 / 重建目标。清理**不在这里录** —— "
            "它在 `Render` 里经 `AddPass` 录制并提交，本特性不持有命令列表")
D.Interface("void Render(FRender& R) override", "录场景 pass（清理 + 绘制），在调用点提交")
D.Interface("void EndRender(FRender& R) override",
            "收尾 stage —— **它是别的特性的排序锚**：`AddPass` 在调用点提交，"
            "所以「我的 IRender 要在场景之后」等价于「等 FScene 的 IEndRender」")
D.Interface("void PreUnInstall(FRender& R) override",
            "**在本模块卸载之前**释放共享目标。FScene 是 FRender 的子插件（在 FRender 关闭期间被卸载），"
            "而 `SceneColor` / `SceneDepth` 是登记在**资源系统**里的 —— 资源系统比它活得久。"
            "留着不管就会出「资源系统销毁一个所属模块已经没了的对象」这类已验证过的崩溃"
            "（子插件比 `FResourceSystem::Shutdown` 先卸载）。实现是 "
            "`RS->DestroyResource(\"SceneColor\"/\"SceneDepth\")` + 清引用 / 布局 / 尺寸缓存")
D.Interface("void TransitionSceneColorForSampling(FRender& R)",
            "把 SceneColor 切到 `ShaderResource`（供下游按纹理采样）。"
            "**何时调用：**采样方（典型是编辑器合成 pass）在它采样之前调，"
            "否则 RHI 的描述符写入写死 `SHADER_READ_ONLY`、而图像还在 `COLOR_ATTACHMENT_OPTIMAL`"
            "（或从未转换过的新目标还在 `UNDEFINED`），校验层直接报错")
D.Interface("void TransitionSceneColorForRendering(FRender& R)",
            "把 SceneColor 切回渲染目标布局（采样方用完必须切回；"
            "场景自己在 `Render` 里、写入之前也会确保是渲染目标布局）")
D.SetAccess("private")
D.Nested("ESceneColorLayout", Kind="enum",
         Desc="SceneColor 的**最后已知布局**追踪器。RHI 不会自动转换布局，"
              "而同一个目标一会儿当附件、一会儿当采样源，所以必须自己记账："
              "没记账就会重复转换（浪费屏障）或漏转换（校验层报错）。")
D.Interface("Undefined", "从未转换过（新目标 / 第一次 Render 之前）")
D.Interface("RenderTarget", "上次离开时是 `COLOR_ATTACHMENT_OPTIMAL`")
D.Interface("ShaderResource", "上次离开时是 `SHADER_READ_ONLY_OPTIMAL`")
D.Interface("void EnsureTargets(FRender& R)",
            "按 `R.GetCanvasWidth()/GetCanvasHeight()` 建 / 重建 SceneColor + SceneDepth；"
            "尺寸变化（或首次）时重建并置 `bTargetsNeedTransition = true`"
            "（新目标需要一次 `Common → RenderTarget`）")
D.Field("FRDGTextureRef SceneColor", "共享场景颜色目标（池拥有原生，跨帧存活）")
D.Field("FRDGTextureRef SceneDepth", "共享场景深度目标")
D.Field("FDrawList TriangleDrawList", "硬编码三角形的绘制协议数据（测试生产者）")
D.Field("std::uint32_t CachedWidth = 0", "上次建目标时的画布宽（0 = 还没建）")
D.Field("std::uint32_t CachedHeight = 0", "上次建目标时的画布高")
D.Field("bool bTargetsNeedTransition = true", "新目标需要一次 `Common → RenderTarget` 的初始转换")
D.Field("ESceneColorLayout SceneColorLayout = ESceneColorLayout::Undefined",
        "SceneColor 当前（最后已知）布局 —— 布局切换的去重依据")

D.Card("为什么 SceneColor 要显式切布局",
       "这是这个头里最值得记住的一条约束：**同一个目标，两种用途，两种布局，而且 RHI 不会自动转换**。")
D.Table("环节", "说明")
D.Row("渲染目标侧", "场景自己拥有：`Render()` 写入之前确保 `COLOR_ATTACHMENT_OPTIMAL`")
D.Row("采样源侧", "编辑器视口把 SceneColor 当镜像读，需要 `SHADER_READ_ONLY_OPTIMAL`")
D.Row("切换责任", "渲染目标侧由场景自己负责；采样侧由采样方调 "
                 "`TransitionSceneColorForSampling` / `TransitionSceneColorForRendering` 来回切")
D.Row("漏切的后果", "RHI 的描述符写入写死 `SHADER_READ_ONLY`，而图像还在 `COLOR_ATTACHMENT_OPTIMAL`"
                   "（或 `UNDEFINED`）⇒ 校验层报布局不匹配")

D.Card("本特性与别的特性的依赖边（读各特性的构造函数）")
D.Table("消费者", "声明", "含义")
D.Row("FDrawTriangleFeature", "`MyStage<IRender>().IsWaiting<Scene::FScene>().ForStage<IEndRender>()`",
      "在场景那批提交之后画三角形（叠在清理之上）")
D.Row("FUIFeature", "`MyStage<IRenderUI>().IsWaiting<Scene::FScene>().ForStage<IEndRender>()`",
      "UI 合成在场景之后（合成在场景之上）")
D.Row("FUIFeature", "`MyStage<IInitViews>().IsWaiting<Scene::FScene>().ForStage<IBeginRender>()`",
      "**数据流边**：视口镜像的解析必须看到**当前**目标 —— 尺寸变化时 `EnsureTargets` 会销毁并重建 "
      "SceneColor，没有这条边，并行 worker 上的 `IInitViews` 可能解析到重建前的陈旧镜像，"
      "`RenderUI` 于是把**新** SceneColor 切到 SR 却画**旧**的（还在 `COLOR_ATTACHMENT_OPTIMAL`）"
      "⇒ `vkCmdDraw` 布局不匹配校验错")
