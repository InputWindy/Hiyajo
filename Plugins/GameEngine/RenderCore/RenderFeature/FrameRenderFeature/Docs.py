# -*- coding: utf-8 -*-
# FrameRenderFeature 插件文档内容（由 Tools/plugin_docs.py 执行，docs_builder 以变量 D 注入）。

# ══════════════════════════════════════════════════════════════════════════════
# Public/FrameRenderFeatureApi.h —— 模块导出标签
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/FrameRenderFeatureApi.h", Title="FrameRenderFeatureApi.h —— 模块导出标签",
         Desc="本特性的 DLL 导出标签：导出 `CreateFrame()` 这个 C 符号与特性类型本身，"
              "让引擎侧能经基类指针安全地销毁本 DLL 构造的实例。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT`")

D.Card("导出标签")
D.Table("宏", "说明")
D.Row("MAHO_FRAMERENDERFEATURE_API",
      "编译本模块时 codegen 定义 `MAHO_FRAMERENDERFEATURE_MODULE_EXPORTS` ⇒ `MAHO_EXPORT`，"
      "消费方 `MAHO_IMPORT`。**为什么：**别的特性要声明「我在 `FFrameRenderFeature` 的 IPresent 之前」"
      "（反向边），它们引用的是本 DLL 的类型 —— 类型标识必须跨模块一致")

# ══════════════════════════════════════════════════════════════════════════════
# Public/FrameRenderFeature.h —— 引擎唯一的上屏点（排序锚）
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/FrameRenderFeature.h", Title="FrameRenderFeature.h —— 引擎唯一的上屏点（排序锚）",
         Desc="帧上屏特性 —— **引擎唯一的上屏点**，并且与任何单个 UI 特性的离屏合成解耦。"
              "它**不拥有任何 ImGui 状态、也不画任何东西**：只读本帧的上屏目标"
              "（`FRender::GetPresentTarget()`，任何 UI 特性在自己的 `RenderUI` 末尾写的那个槽），"
              "再把它 blit 到交换链。\n"
              "**这种解耦的价值**：编辑器构建里编辑器 UI 特性可以把 `EditorRT` 设成上屏目标，"
              "而运行时构建仍然呈现游戏 UI 的合成结果 —— **上屏点固定，目标可切换**。\n"
              "`IPresent` 是 `FRenderStages` 的**最后一个** stage。它靠各生产者自己声明**反向边**"
              "来保证「总是在生产者的最终合成之后跑」；某个生产者没装时该边根本不存在"
              "（优雅降级，不需要本特性去枚举谁装了谁没装）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("FrameRenderFeatureApi.h", "`MAHO_FRAMERENDERFEATURE_API`")
D.Row("Engine/Frame.h", "`FFrameExtension` / `MAHO_DECLARE_FRAME` / `IPipeline`")
D.Row("Render.h", "`FRender` / `IPresent`（本特性唯一实现的 stage）")
D.Row("RDG.h", "`FRDGTextureRef`（上屏目标的类型；存在 `FRender::GetPresentTarget` 里）")

D.Class("FFrameRenderFeature", Base="FFrameExtension, IPipeline<IPresent>",
        Desc="上屏特性：**只挂 `IPresent`**。它同时是「排序锚」—— 别的特性把反向边挂到它的 "
             "`IPresent` 上，从而保证自己「先于上屏」完成合成。"
             "它自己**不发上屏原语**（那条被移回 `FRender::EndFrame`，见下）。")
D.SetAccess("public")
D.Interface("FFrameRenderFeature()",
            "构造：**刻意不声明任何正向等待**。上屏点必须是最后一个 stage，但它**无法枚举**生产者"
            "（它是通用的，而且编辑器特性在运行时构建里可能根本不存在）。"
            "所以顺序由生产者在自己的构造函数里**反向声明**："
            "写上屏目标的 UI 特性声明 `MyStage<IRenderUI>().IsBlocking<FFrameRenderFeature>()"
            ".OnStage<IPresent>()`；没装的生产者根本不会产生这条边")
D.Interface("void Present(FRender& R) override",
            "**只作排序锚**：上屏原语已由 `FRender::EndFrame` 发出，这里不做实际工作。"
            "历史原因值得记下来：这个 stage 以前也调 `R.PresentTexture()`，"
            "把两件事混在一起 —— ①「呈现**哪个**目标」（渲染序列内部的排序决策，"
            "生产者把反向边挂在这里）与 ②「发出上屏原语」（帧边界操作，必须与 "
            "BeginFrame / EndFrame **串行**）。坏的是第二半：这个 stage 活在 FRender 的**收集器图**里，"
            "而 `FRender::IBeginFrame` / `IEndFrame` 是**宿主图**里的节点 —— 两张图之间没有依赖边，"
            "没有任何东西能给它们排序，尽管 RHI 早就写明帧原语必须由调用方保持串行。"
            "把调用移进 `EndFrame` 后，三个帧原语落在同一条链上，由每层自带的准入闸跨帧串行化。"
            "「最后写者胜」依然确定：`EndFrame` 在渲染图**排空之后**才读那个目标")

D.Card("生产者的反向边（读各特性的构造函数）",
       "「反向声明」是这套解耦的关键：**依赖方向由消费者声明**，而这里消费者其实是「被排序的一方」。"
       "生产者知道自己必须在上屏之前完成，所以由它把边挂到上屏点上。")
D.Table("生产者", "声明", "含义")
D.Row("FUIFeature", "`MyStage<IRenderUI>().IsBlocking<FFrameRenderFeature>().OnStage<IPresent>()`",
      "UI 合成必须在上屏之前：它的 `RenderUI` 写 `UIRenderTarget` 并 `SetPresentTarget`，"
      "上屏点随后读它。`FFrameRenderFeature` 总是装着，所以这条边总能钉住")
