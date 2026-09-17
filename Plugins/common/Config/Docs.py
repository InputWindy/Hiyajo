# -*- coding: utf-8 -*-
# Config 插件文档内容（由 Tools/plugin_docs.py 执行，docs_builder 以变量 D 注入）。

# ══════════════════════════════════════════════════════════════════════════════
# Public/ConfigApi.h —— 模块导出标签
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/ConfigApi.h", Title="ConfigApi.h —— 模块导出标签",
         Desc="本模块的 DLL 导出标签。单独成一个头，是为了让**只想要访问器声明**的消费方"
              "不必连带把整个 `Config.h` 拉进来。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT` 本体（引擎核心提供，插件不自己定义）")

D.Macro("MAHO_CONFIG_API", "MAHO_EXPORT / MAHO_IMPORT（随构建开关切换）",
        Desc="编译 `Config.dll` 时构建系统定义 `MAHO_CONFIG_MODULE_EXPORTS` ⇒ 展开成 `MAHO_EXPORT`；"
             "消费方展开成 `MAHO_IMPORT`。\n"
             "**为什么必须有**：`FConfig* GetConfig()` 的返回值类型要跨 DLL 使用，"
             "而 `FConfig` 的虚表与删除析构符必须**只在**本模块内生成 —— "
             "打上 `dllimport` 才能禁止消费方各自生成一份（否则 `delete` 时会跳进已卸载的镜像）。")

# ══════════════════════════════════════════════════════════════════════════════
# Public/Config.h —— INI 配置（服务层）
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Config.h", Title="Config.h —— INI 配置读取（服务层）",
         Desc="INI 风格的静态配置中心（对齐 UE `DefaultEngine.ini` 的 `[Section] Key=Value`）："
              "`Load` 解析文件，`GetString/GetInt/GetFloat/GetBool` 读，`SetString` 运行期覆盖。\n"
              "**为什么统一按 `Section + Key` 取名**：配置项天然是二段式的（哪个系统、哪个参数），"
              "两段式查找让不同系统的配置可以共用一个文件而不互相挤占命名空间。\n"
              "**为什么内部一律存字符串、读取时才解析**：INI 的值本来就是文本；"
              "存原文意味着「读成 int」还是「读成 float」由读取方决定，"
              "同一个键不会被第一次解析的类型锁死。\n"
              "**为什么 `GetString` 返回 `optional`、而 `GetInt` 等带默认值**："
              "字符串没有天然默认值（调用方必须知道「没配」这件事），"
              "数值则可以约定「没有就取默认」——这两类的语义差别在签名上就分开了。\n"
              "**并发**：不持锁，`Load` 与读写都应在初始化阶段单线程完成。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("ConfigApi.h", "`MAHO_CONFIG_API` —— 本模块的导出标签")
D.Row("<Maho.h>", "引擎聚合头（`Core/Export.h`、`Core/Fatal.h` 等基础设施）")
D.Row("<Engine/Frame.h>", "`FFrameExtension` / `IPipeline` / `MAHO_DECLARE_FRAME` —— 帧的声明词汇")
D.Row("<cstdint>", "`std::int64_t` —— `GetInt` 的返回类型（**定宽**：32 位下 `long` 不够）")
D.Row("<map>", "`Section → Key → 值` 的两层映射（有序，便于稳定输出与调试）")
D.Row("<optional>", "`GetString` 的「可能不存在」语义")
D.Row("<string> / <string_view>", "内部拥有字符串；外部传入一律用 `string_view`（零拷贝观察）")

D.Card("6 个 stage 的职责（为什么全挂上）",
       "`IPipeline` 里列出全部 6 个 stage，是为了让「时钟点」在**类型层面**完整：即使当前只有 "
       "`Initialize` / `Shutdown` 有实体，其它四个也留成空实现 —— "
       "这样后续想把工作挪到 `PreInit`（更早）或 `PostShutdown`（更晚）时，"
       "只需要填函数体，不需要改帧的 stage 列表（改列表会让所有声明了相对顺序的其它帧重新对表）。")
D.Table("stage", "本帧的实现")
D.Row("IPreInit → PreInitialize", "空：`Load` 由调用方在宿主 `PreMain` 里显式驱动，这里不抢时序")
D.Row("IInit → Initialize", "发布全局访问点（`GetConfig()` 从这里开始有效）")
D.Row("IPostInit → PostInitialize", "空")
D.Row("IPreShutdown → PreShutdown", "空")
D.Row("IShutdown → Shutdown", "撤销全局访问点（`GetConfig()` 之后返回空）")
D.Row("IPostShutdown → PostShutdown", "空")

D.Card("自由函数")
D.Table("签名", "说明")
D.Row("MAHO_CONFIG_API FConfig* GetConfig()",
      "全局访问器：`Initialize` 里置位、`Shutdown` 里清空。"
      "**为什么用函数而不是导出变量**：跨 DLL 的裸变量导出在不同运行库 / 不同编译器下"
      "很容易只导出符号却拿不到同一份实例；函数调用则是唯一确定的那条路")

D.Class("FConfig", Base="FFrameExtension, IPipeline<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>",
        Desc="配置服务本体。**它是一个帧**（挂 6 个 stage），因此它的建立与拆除由调度图排序，"
             "而不是靠某个全局构造顺序。\n"
             "**为什么不持有锁**：配置在初始化阶段装载完就不再变化（`SetString` 是给启动流程覆盖用的），"
             "读路径上不需要同步原语 —— 为此付出的代价是「运行期跨线程写配置」不受保护，"
             "这也是刻意的约束。")
D.SetAccess("private")
D.Interface("MAHO_DECLARE_FRAME(FConfig)",
            "声明帧身份：给出稳定的字符串字面量名字与 `CreateFrame` 工厂，"
            "让 `FFrameBuilder` 能按名字引它、按模块路径装载它（**名字是字面量**才使身份键安全）")
D.Interface("void PreInitialize(FEngineBase&) override", "空：见上面的 stage 分工表")
D.Interface("void Initialize(FEngineBase&) override",
            "服务上线：让 `GetConfig()` 指向本实例")
D.Interface("void PostInitialize(FEngineBase&) override", "空")
D.Interface("void PreShutdown(FEngineBase&) override", "空")
D.Interface("void Shutdown(FEngineBase&) override",
            "服务下线：清空全局访问点（对称于 `Initialize`，避免析构后仍被 `GetConfig()` 拿到）")
D.Interface("void PostShutdown(FEngineBase&) override", "空")
D.SetAccess("public")
D.Interface("bool Load(std::string_view Path)",
            "解析一个 INI 文件：`[Section]` 开新段，`Key=Value` 填键；返回是否装载成功。"
            "重复 `Load` 的语义是**并入**（后到者覆盖同名键）")
D.Interface("[[nodiscard]] std::optional<std::string> GetString(std::string_view Section, std::string_view Key) const",
            "取字符串；不存在返回 `nullopt`（调用方必须显式处理「没配」）")
D.Interface("[[nodiscard]] std::int64_t GetInt(std::string_view Section, std::string_view Key, std::int64_t Default = 0) const",
            "按整数解析；键缺失或解析失败时给 `Default`。**为什么给默认值**：数值配置绝大多数有合理缺省，"
            "逼调用方写 `optional` 分支只会把「用默认」这件事写得更啰嗦")
D.Interface("[[nodiscard]] double GetFloat(std::string_view Section, std::string_view Key, double Default = 0.0) const",
            "按浮点解析，同上（统一用 `double`：避免 `float` 在跨平台字面量往返里掉精度）")
D.Interface("[[nodiscard]] bool GetBool(std::string_view Section, std::string_view Key, bool Default = false) const",
            "按布尔解析（`true/false` 等文本形式），同上")
D.Interface("void SetString(std::string_view Section, std::string_view Key, std::string Value)",
            "运行期覆盖一个键（值以字符串原文存入，解析仍发生在读取侧）")
D.Interface("[[nodiscard]] bool HasSection(std::string_view Section) const",
            "段是否存在 —— 用于区分「整段没配」与「键没配」")
D.Interface("[[nodiscard]] bool HasKey(std::string_view Section, std::string_view Key) const",
            "键是否存在 —— 想区分「值为空串」与「没这个键」时用它")
D.SetAccess("private")
D.Nested("using FSection = std::map<std::string, std::string>", Kind="alias",
         Desc="一段内部类型别名：键值对表。**为什么用 `map` 而不是 `unordered_map`**："
              "配置规模极小，有序容器的稳定遍历顺序在打印 / 对比配置时更有用，"
              "而哈希容器省下的那点查找在这里毫无意义")
D.Field("std::map<std::string, FSection> Sections",
        "全部配置：段名 → 键值表。**为什么两层 `map`**：段与键都是稀疏的小集合，"
        "嵌套 `map` 让 `HasSection` / `HasKey` 都能 O(log n) 直接命中，不需要额外的索引结构")
