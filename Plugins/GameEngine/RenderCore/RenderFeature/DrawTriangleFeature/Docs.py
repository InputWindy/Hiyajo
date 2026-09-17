# -*- coding: utf-8 -*-
# DrawTriangleFeature 插件文档内容（由 Tools/plugin_docs.py 执行，docs_builder 以变量 D 注入）。

# ══════════════════════════════════════════════════════════════════════════════
# Public/DrawTriangleFeatureApi.h —— 模块导出标签
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/DrawTriangleFeatureApi.h", Title="DrawTriangleFeatureApi.h —— 模块导出标签",
         Desc="本特性的 DLL 导出标签。导出的是 `CreateFrame()` 这个 **C 符号**（宿主按符号名查找），"
              "以及特性自己的类型 —— 因为特性实例由引擎侧经基类指针销毁，"
              "vftable 与 deleting dtor 必须留在本 DLL。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT`")

D.Card("导出标签")
D.Table("宏", "说明")
D.Row("MAHO_DRAWTRIANGLEFEATURE_API",
      "编译本模块时 codegen 定义 `MAHO_DRAWTRIANGLEFEATURE_MODULE_EXPORTS` ⇒ 展开为 `MAHO_EXPORT`，"
      "消费方只拿 `MAHO_IMPORT`。**为什么 `CreateFrame` 的返回类型要带它：**"
      "宿主（FRender）拿到的 `FFrameExtension*` 背后是本 DLL 构造的对象，"
      "卸载时由宿主经虚表销毁 —— 没有导出标签就会在 `delete` 里跳进已释放的映像")

# ══════════════════════════════════════════════════════════════════════════════
# Public/DrawTriangleFeature.h —— 最小渲染特性（全屏三角形）
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/DrawTriangleFeature.h", Title="DrawTriangleFeature.h —— 最小渲染特性（全屏三角形）",
         Desc="**渲染特性的最小范例**，也是 `Render.h` 那套声明式接口的活用法说明书："
              "编译一个全屏三角形着色器，每帧把它画进 `Scene::FScene` 的共享 `SceneColor` 目标"
              "（动态渲染，在场景清理之上 Load）。\n"
              "它**只挂 `IRender` 一个 stage** —— 不需要帧首准备，也不参与收尾；"
              "它在 `IRender` 里填一份管线配置 + 着色器类型 + 渲染附件，交给 `FRender::AddPass`；"
              "AddPass 从池里解析 PSO、开渲染通道、**隐式绑定管线**。"
              "于是特性**不拥有任何**管线、布局、着色器模块，也不持有裸 RHI 指针。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("DrawTriangleFeatureApi.h", "`MAHO_DRAWTRIANGLEFEATURE_API`")
D.Row("Maho.h", "引擎聚合头（日志 / 断言 / 基础类型）")
D.Row("Engine/Frame.h", "`FFrameExtension` + `MAHO_DECLARE_FRAME` + 依赖声明原语（`MyStage`）")
D.Row("Render.h", "`FRender` / `IPipeline<IRender>` / stage 接口 / `TShaderHandle` / `AddPass`")

D.Struct("FTriangleShader", Desc="被 `FRender::TryGetShader<T>` 消费的着色器类型：**纯静态**源码访问器。"
        "编译 / 字节码缓存 / 内容哈希都活在共享的 `TShaderHandle` 状态里，"
        "所以特性**既不拥有字节码也不拥有原生模块**。静态体定义在 .cpp（GLSL 串保持特性私有）。")
D.Interface("static const char* GetVertexSource()", "顶点 GLSL 源码（.cpp 里的私有串）")
D.Interface("static const char* GetFragmentSource()", "片段 GLSL 源码")
D.Interface("static const char* GetVertexEntryPoint()", "顶点入口名（`\"main\"`；"
                                                       "不声明这个静态函数也合法，"
                                                       "`Detail::GetVertexEntryPoint<T>` 会回退到 main）")
D.Interface("static const char* GetFragmentEntryPoint()", "片段入口名")

D.Class("FDrawTriangleFeature", Base="FFrameExtension, IPipeline<IRender>",
        Desc="全屏三角形特性。`IPipeline<IRender>` 说明它只被 `IRender` 这一个 stage 驱动 —— "
             "stage 列表就是它挂载的能力集，没挂的 stage 在批次里**完全不产生节点**。")
D.SetAccess("private")
D.Interface("FDrawTriangleFeature()",
            "构造：登记依赖 —— `MyStage<IRender>().IsWaiting<Scene::FScene>().ForStage<IEndRender>()`。"
            "**为什么等 FScene 的 `IEndRender` 而不是 `IRender`：**`AddPass` 在调用点就提交，"
            "所以真正要保证的是「本特性的 IRender 在 FScene 那批提交之后」。"
            "等它的收尾 stage，就把两个 pass 的提交顺序钉死了")
D.Interface("~FDrawTriangleFeature() override",
            "析构：**什么都不用拥有**。管线 + 布局在 FRender 的 PSO 缓存里（归资源池、"
            "Shutdown 时销毁），着色器模块归每个 T 的 `TShaderHandle` 状态。"
            "FRender 在销毁池**之前**先拆掉自己的特性 ⇒ 没有裸 RHI 对象活得比它久")
D.SetAccess("private")
D.Interface("void Render(FRender& R) override",
            "唯一的 pass：① `Scene::GetScene()` 拿共享目标，无效就直接返回（目标还没建好）；"
            "② `TryGetShader<FTriangleShader>()` + `Wait()`（**用之前先同步**）拿到 VS / FS 模块与内容哈希；"
            "③ 填 `FRenderTarget`（`Color` = SceneColor，`LoadOp::Load` —— 叠在场景清理之上；"
            "有 SceneDepth 就带上深度 `Load` / `DontCare`）；"
            "④ 填 `FRHIGraphicsPipelineDesc`（**`RenderPass = nullptr` = 动态渲染**，"
            "只给颜色 / 深度格式；`CullMode::None`+`VertexStride = 0` 因为顶点由着色器凭 "
            "`gl_VertexIndex` 生成）；⑤ `AllocParameters<FTriangleParameters>()`（空参数结构，"
            "宏引擎产出的仍是一个合法空布局）+ `AddPass(...)`，lambda 里设视口 / 裁剪并按 "
            "`FScene::GetTriangleDrawList()` 的批次录 `Draw(3)`。"
            "**它不知道绘制列表是谁产的** —— 这就是绘制协议的意义")

D.Card("运行期的 pass 形态（读 Private/DrawTriangleFeature.cpp）",
       "这张表说明「声明式」在运行时到底发生了什么，也解释了为什么特性不需要任何 RHI 指针。")
D.Table("环节", "说明")
D.Row("着色器源", "全屏三角形：顶点着色器用 `gl_VertexIndex` 从常量数组取 3 个位置，"
                 "片段着色器输出固定红色 —— **完全不需要顶点缓冲**，所以录制时是 `Draw(3)`")
D.Row("参数结构", "`BEGIN/END_SHADER_PARAMETER_STRUCT(FTriangleParameters)` 是**空的**："
                 "这个 pass 不绑描述符也不推常量；宏引擎产出的仍是一个合法（空）布局，`AddPass` 照用")
D.Row("绘制数据", "批次来自 `FScene::GetTriangleDrawList()`（场景作为「绘制协议的生产者」），"
                 "本特性只消费 —— 换成真正的场景渲染器时这个特性一行不改")
D.Row("工厂导出", "`extern \"C\" CreateFrame()` 返回 `FFrameExtension*`，宿主按符号名查找后装入自己的收集器")
