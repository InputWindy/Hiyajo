# UISystem 插件的内容脚本（plugin_docs.py 会 exec 本文件，docs_builder 已注入为 D）。
# 内容与磁盘上的 Public/*.h 逐字对应。

# ══════════════════════════════════════════════════════════════════════════════
# Public/UISystemApi.h —— 导出宏
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/UISystemApi.h", Title="UISystemApi.h —— 导出宏",
         Desc="本插件的导出开关：`MAHO_UISYSTEM_API` 按 `MAHO_UISYSTEM_MODULE_EXPORTS` 在 "
              "`MAHO_EXPORT` / `MAHO_IMPORT` 之间切换。\n"
              "**为什么这个小系统也需要它**：`FUISystem` 由世界（另一个模块）构造、由子收集器析构，"
              "并且 `GetUISystem()` 要从别的 DLL 调用 —— 三者都跨模块边界。"
              "类不带导出标记时 vftable 会每个模块各生成一份，析构就落到已卸载的映像里。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT` 两个底层标记")

D.Macro("MAHO_UISYSTEM_API", "MAHO_EXPORT / MAHO_IMPORT（按 MAHO_UISYSTEM_MODULE_EXPORTS 切换）",
        Desc="挂在 `FUISystem`、`GetUISystem()` 与 cpp 里的 `CreateFrame()` 上。")

# ══════════════════════════════════════════════════════════════════════════════
# Public/UISystem.h —— 世界系统：游戏侧 UI 的所有者
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/UISystem.h", Title="UISystem.h —— 世界系统：游戏侧 UI 的所有者",
         Desc="`FUISystem` 是一个**世界系统**：它不挂引擎 stage，而是挂世界的 ECS 子 stage"
              "（`IOnInstalled` / `IProcessInput` / `IUpdate` / `IPreUnInstall`），"
              "由 `FGameWorld` 在自己的 `Tick` 里调度（装入安全点 → 输入 → 更新 → 卸出安全点）。\n"
              "它拥有游戏侧 UI：每个 `FUIWidget` 实体持有一棵**持久的声明式 UI 树**"
              "（`UI::FUIView`）。树**就是**那个控件 —— 跨帧存活、保留运行期状态（文本缓冲、开关、滚动位置）；"
              "系统每次 `Update` 在 `Edit()` 里**重新声明**本帧内容，同 id 同类型重新声明 = **复用**该节点。\n"
              "**游戏侧一个 ImGui 调用都没有**：声明走 UI 插件的 builder API，翻译成 ImGui 是渲染特性"
              "（`UIFeature`）的事，本系统只持树。\n"
              "**跨线程契约 = 独占写 / 共享读**：本系统的 `Edit()` 是独占写，翻译线程遍历树时是共享读"
              "（树的 `std::shared_mutex`）；两边都不跨边界持树。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("UISystemApi.h", "`MAHO_UISYSTEM_API`：本头里的类型要跨 DLL")
D.Row("GameWorld.h", "`FGameWorld` / `FEntity` / `FEntity` 的组件 API —— 本系统嵌在那个世界上下文里")
D.Row("UIView.h", "`UI::FUIView`（持久树 + `Edit()` 独占写作用域）与 `UI::FUIBuilder`（声明入口）")
D.Row("memory", "`std::shared_ptr<UI::FUIView>`：组件按值 move，共享指针让这条路径保持可平凡拷贝")

D.Struct("FUIWidget", Desc="ECS 侧的 UI 控件：**一棵持久声明式 UI 树的持有者**。它是组件，所以必须可"
        "按值搬移（组件池扩容时整池元素被 move）—— 这就是这里用 `shared_ptr` 而不是 `unique_ptr` 的原因："
        "拷贝语义让「池增长」这条最热的路径保持平凡有效。")
D.SetAccess("public")
D.Field("std::shared_ptr<UI::FUIView> View", "树的所有权（可能为空 = 视图尚未建立）")

D.Class("FUISystem", Base="FFrameExtension + IPipeline<IOnInstalled, IProcessInput, IUpdate, IPreUnInstall>",
        Desc="世界系统本身：两个基类（`FFrameExtension` 声明层 + `IPipeline<...>` 有序子 stage 列表）。"
             "它只挂世界真正会派发的那四个子 stage —— stage 列表就是它的能力声明，"
             "多挂一个（比如 `IFixedUpdate`）会让世界的固定步进序列给它派发节点，"
             "而它并没有固定步进的语义。\n"
             "**四个 stage 里的实际内容**：`OnInstalled` 发布全局访问器；`Update` 是唯一有逻辑的"
             "（建立/复用视图 → 回传事件 → 独占写重新声明树）；`ProcessInput` 是声明出来但空的能力"
             "（UI 输入由翻译层直接投递给树，游戏侧没有可轮询的东西）；`PreUnInstall` 注销视图。")
D.SetAccess("public")
D.Interface("void OnInstalled(FGameWorld& World) override",
            "装入安全点：登记全局访问器（跨 DLL 走函数，避免每模块一份静态变量）。"
            "**不在这里建视图** —— 此刻 UI 注册表与渲染上下文可能还没就位（游戏世界与渲染特性的初始化"
            "顺序不固定），视图延到第一帧 `Update` 里建")
D.Interface("void ProcessInput(FGameWorld& World) override",
            "空的已声明 stage：UI 输入由**翻译层**直接投递给树（上下文归渲染侧所有），"
            "游戏侧没有要轮询的东西；保留这个 stage 是为了让能力列表完整可读")
D.Interface("void Update(FGameWorld& World) override",
            "一帧的全部工作量：① `EnsureDemoView`（未就位就返回，**下一帧再试**）；"
            "② `DrainEvents()` —— 交互事件先回传，翻译线程只入队、真正的回调在所有者线程（这里）执行，"
            "回调内可以再次 `Edit()`；③ 在 `Edit()` 独占写作用域里重新声明整棵树（同 id 同类型 = 复用）")
D.Interface("void PreUnInstall(FGameWorld& World) override",
            "卸出安全点：**先把视图从注册表注销，再让它被销毁** —— 注册表只持裸指针，"
            "顺序反了就是悬垂指针。顺序本身由 `FGameWorld` 声明（它才是驱动本阶段的层）："
            "世界把 `IShutdown` 阻塞在 `FUIViewRegistry` 上，本系统的 `PreUnInstall` 才有"
            "「注册表还活着」这个前提。随后销毁演示实体、清全局访问器")
D.SetAccess("private")
D.Interface("UI::FUIView* EnsureDemoView(FGameWorld& World)",
            "建立（只一次）演示控件的实体 + 组件 + 持久视图，绑定到游戏渲染上下文并注册进注册表。"
            "注册表或上下文任一未就位就返回 nullptr、由调用方下一帧重试 —— "
            "因为游戏世界与渲染特性的初始化顺序**不是**固定的（声明前向依赖反而会让图编译失败）")
D.Interface("void BuildDemoTree(UI::FUIBuilder& Root)",
            "重新声明演示树（只在活的 `Edit()` 作用域内调用）。节点 id 稳定（`UI::FUIName(\"...\")`）"
            "⇒ 同 id 同类型重新声明即复用，运行期状态跨帧存活")
D.Field("FEntity DemoWidget", "演示控件的实体句柄（无效 = 尚未建立）；卸出时先查 `IsValid` 再销毁")

D.Card("自由函数")
D.Table("签名", "说明")
D.Row("MAHO_UISYSTEM_API FUISystem* GetUISystem()",
      "全局访问器（跨 DLL 走函数，与 `Resource::GetResourceSystem()` 同一模式）。"
      "`OnInstalled` 置位、`PreUnInstall` 清回 nullptr ⇒ **系统未安装或已卸出时为 nullptr**；"
      "调用方每次都要判空，不能缓存过卸出点")

D.Card("跨 DLL 导出面")
D.Table("符号", "说明")
D.Row("Maho::GameWorld::FUISystem", "把 `Install<FUISystem>()` 交给世界的子收集器 ⇒ 世界构造/析构它，必须导出")
D.Row("Maho::GameWorld::FUIWidget", "组件被按值放进池、跨模块取用 ⇒ 必须导出")
D.Row("CreateFrame()（`Private/UISystem.cpp`）", "子收集器按符号名查找的 C 导出，转发到 `FUISystem::CreateFrame()`")
