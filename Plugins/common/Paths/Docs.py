# -*- coding: utf-8 -*-
# Paths 插件文档内容（由 Tools/plugin_docs.py 执行，docs_builder 以变量 D 注入）。

# ══════════════════════════════════════════════════════════════════════════════
# Public/PathsApi.h —— 模块导出标签
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/PathsApi.h", Title="PathsApi.h —— 模块导出标签",
         Desc="本模块的 DLL 导出标签，独立成头 —— 只要「路径访问器」声明的模块不必引入 `Paths.h`。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT`（引擎核心提供）")

D.Macro("MAHO_PATHS_API", "MAHO_EXPORT / MAHO_IMPORT（随构建开关切换）",
        Desc="编译 `Paths.dll` 时构建系统定义 `MAHO_PATHS_MODULE_EXPORTS` ⇒ `MAHO_EXPORT`；"
             "消费方为 `MAHO_IMPORT`。\n"
             "**为什么必须有**：`FPaths* GetPaths()` 跨 DLL 返回类型，"
             "`FPaths` 的虚表与删除析构符只能在本模块生成；"
             "`dllimport` 把「消费方各生成一份」变成编译错误，避免卸载后 `delete` 静默崩。")

# ══════════════════════════════════════════════════════════════════════════════
# Public/Paths.h —— 虚拟路径 → 物理路径
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Paths.h", Title="Paths.h —— 虚拟路径 → 物理路径（服务层）",
         Desc="路径解析层：先把根**别名**登记进来（例如 `\"Engine\"` → 引擎目录、"
              "`\"Project\"` → 项目目录），再用 `Resolve(\"Engine/Config/Default.ini\")` 换出物理路径。\n"
              "**为什么要有这一层**：物理路径一旦散落在代码里，"
              "「换一台机器 / 换个打包目录 / 编辑器与运行时布局不同」就会变成满地改字符串。"
              "把**根**集中登记一次，之后代码里只有虚拟路径，物理布局自由变化。\n"
              "**为什么同时接受 `Alias/Sub/Path` 与 `Alias:Sub/Path`**：前者读起来像目录结构，"
              "后者在配置里更醒目、也不会和真实子目录混淆 —— 两种写法都在 `Resolve` 里归一到同一套解析。\n"
              "**并发**：不持锁 —— `SetRoot` 与 `Resolve` 都应在初始化阶段单线程完成"
              "（它是「启动时把根扎好、之后只读」的典型用法）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("PathsApi.h", "`MAHO_PATHS_API` —— 本模块的导出标签")
D.Row("<Maho.h>", "引擎聚合头（`FFrameExtension` / `IPipeline` 等）")
D.Row("<Engine/Frame.h>", "`MAHO_DECLARE_FRAME` / stage 接口 —— 帧的声明词汇")
D.Row("<filesystem>", "`std::filesystem::path` —— **路径的表示**："
                      "分隔符、宽窄字符与拼接规则交给标准库，插件里不出现任何手写拼接")
D.Row("<map>", "别名表：`别名 → 物理根`（有序 ⇒ 调试打印时的枚举顺序稳定）")
D.Row("<string> / <string_view>", "别名由 `map` 拥有字符串；查询入口用 `string_view`（零拷贝）")

D.Card("自由函数")
D.Table("签名", "说明")
D.Row("MAHO_PATHS_API FPaths* GetPaths()",
      "全局访问点：`Initialize` 里发布、`Shutdown` 里清除（未上线时为 `nullptr`）。"
      "函数而非导出裸变量：跨 DLL 才能拿到同一个解析表")

D.Class("FPaths", Base="FFrameExtension, IPipeline<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>",
        Desc="路径解析器本体 —— **一个帧**（挂 6 个 stage）："
             "根别名由宿主的启动流程在它 `Initialize` 之后登记，"
             "而所有 `Resolve` 都发生在根就绪之后、`Shutdown` 之前。\n"
             "**为什么不做成纯静态函数**：解析结果取决于「谁把哪些根登记进来了」这份运行期状态，"
             "它不是编译期常量；做成帧就能让它参与依赖排序（例如资源层要求「路径已就绪」）。")
D.SetAccess("private")
D.Interface("MAHO_DECLARE_FRAME(FPaths)",
            "声明帧身份（稳定字面量名字 + `CreateFrame` 工厂）—— 使别处能按名字声明"
            "「我在路径就绪之后才解析」（typed 形式不可用时用名字寻址）")
D.Interface("void PreInitialize(FEngineBase&) override", "空：本帧不需要更早的时钟点")
D.Interface("void Initialize(FEngineBase&) override",
            "发布全局访问点（`GetPaths()` 从这里开始非空）")
D.Interface("void PostInitialize(FEngineBase&) override", "空")
D.Interface("void PreShutdown(FEngineBase&) override", "空")
D.Interface("void Shutdown(FEngineBase&) override",
            "撤销全局访问点 —— 此时已没有使用者（依赖图保证），因此可以直接清掉解析表")
D.Interface("void PostShutdown(FEngineBase&) override", "空")
D.SetAccess("public")
D.Interface("void SetRoot(std::string_view Alias, std::filesystem::path Path)",
            "登记一个根别名（如 `\"Engine\"` → 引擎目录）。`Path` 按值收 —— "
            "它要被存进表里，按值传让调用方可以 `std::move` 进来")
D.Interface("[[nodiscard]] std::filesystem::path Resolve(std::string_view VirtualPath) const",
            "把 `Alias/Sub/Path` 或 `Alias:Sub/Path` 解析成物理路径。"
            "**未知别名**的语义由实现决定（当前是按字面路径回退），"
            "因此需要区分「别名拼错」与「文件不存在」时应先用 `HasRoot` 探一下")
D.Interface("[[nodiscard]] bool HasRoot(std::string_view Alias) const",
            "别名是否已登记 —— 用来在**进入解析之前**区分「根没配」与「路径不对」，"
            "这两种失败的处理完全不同（一个要报配置错误，一个是正常的文件缺失）")
D.SetAccess("private")
D.Field("std::map<std::string, std::filesystem::path> Roots",
        "别名 → 物理根。**为什么是 `map` 而不是 `unordered_map`**：根的个数是个位数，"
        "有序容器在打印 / 比较配置时给出的稳定顺序更有价值")
