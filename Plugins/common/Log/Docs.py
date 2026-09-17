# -*- coding: utf-8 -*-
# Log 插件文档内容（由 Tools/plugin_docs.py 执行，docs_builder 以变量 D 注入）。

# ══════════════════════════════════════════════════════════════════════════════
# Public/LogApi.h —— 模块导出标签
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/LogApi.h", Title="LogApi.h —— 模块导出标签",
         Desc="本模块的 DLL 导出标签。独立成头：日志是被引用最广的服务，"
              "把符号可见性约定单独放一处，任何只想声明「我会调 `GetLog()`」的模块"
              "都不必连带引入整个 `Log.h`。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT`（引擎核心提供）")

D.Macro("MAHO_LOG_API", "MAHO_EXPORT / MAHO_IMPORT（随构建开关切换）",
        Desc="编译 `Log.dll` 时构建系统定义 `MAHO_LOG_MODULE_EXPORTS` ⇒ `MAHO_EXPORT`；"
             "消费方是 `MAHO_IMPORT`。\n"
             "**为什么必须有**：`FLog* GetLog()` 跨 DLL 返回值类型，`FLog` 的虚表与删除析构符"
             "必须只在本模块生成；`dllimport` 把「消费方自己生成一份」变成编译错误，"
             "而不是留到关服时在 `delete` 里静默崩。")

# ══════════════════════════════════════════════════════════════════════════════
# Public/Log.h —— 引擎唯一的日志出口
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Log.h", Title="Log.h —— 引擎唯一的日志出口（服务层）",
         Desc="全引擎**唯一**的日志出口：`Initialize` 把日志器拉起来（stdout 带色 + 滚动文件，"
              "并解析 `--log-level`）并通过 `GetLog()` 发布，`Shutdown` 冲刷并释放。\n"
              "**为什么唯一**：日志一旦多源，输出顺序、级别过滤、文件轮转就会各写一套，"
              "排查问题时第一件事变成「这条到底从哪条管道出来的」。"
              "因此其它模块只许经 `::Maho::GetLog()` / `MAHO_LOG_CORE_*` 写日志。\n"
              "**为什么头文件不出现 spdlog 类型**：spdlog 是**实现细节**（未来可能换）；"
              "这里只前向声明 `spdlog::logger`，成员是 `std::shared_ptr<spdlog::logger>` 且析构放在 "
              "`Log.cpp` —— 消费方的编译单元不需要 spdlog 头，也就不受它版本变化影响。\n"
              "**为什么日志接口是完美转发模板**：格式串走 `fmt::format_string` ⇒ **编译期**校验"
              "占位符与实参是否匹配，同时不产生运行期类型擦除的开销。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("<spdlog/fmt/fmt.h>", "`fmt::format_string` / `fmt::format`：**编译期格式检查**是这套接口的核心收益；"
                            "只引 fmt 子头，不引 spdlog 本体")
D.Row("LogApi.h", "`MAHO_LOG_API` —— 本模块的导出标签")
D.Row("<Maho.h>", "引擎聚合头（`FFrameExtension` / `IPipeline` 等）")
D.Row("Core/Delegate.h", "`TMulticastEvent` —— `OnLog` 实时日志流（**订阅者线程即回调线程**）")
D.Row("Core/Fatal.h", "`MAHO_ENSURE_NOT_NULL` —— `MAHO_LOG_CORE_*` 宏在日志层尚未上线时的软着陆")
D.Row("<Engine/Engine.h>", "`FEngineBase` 与 stage 接口（本帧只挂 `IInit` / `IShutdown`）")
D.Row("<cstdint> / <memory> / <functional>", "`std::shared_ptr`（日志器的共享所有权）与配套基础类型")
D.Row("<string> / <string_view>", "日志文本的拥有者与观察者（入口用 `string_view`，落库用 `std::string`）")

D.Card("日志级别（ELogLevel）")
D.Table("取值", "含义")
D.Row("Trace", "最细：逐点跟踪（默认被级别过滤掉）")
D.Row("Debug", "调试信息")
D.Row("Info", "常规信息（默认档）")
D.Row("Warn", "可疑但可继续")
D.Row("Error", "失败但未终止")
D.Row("Critical", "严重失败；**注意这里仍然不 abort** —— 终止进程是 `Core/Fatal` 的职责")

D.Card("语法糖宏（引擎核心日志）")
D.Table("宏", "说明")
D.Row("MAHO_LOG_CORE_TRACE(...) / DEBUG / INFO / WARN / ERROR / CRITICAL(...)",
      "转发到 `::Maho::GetLog()` 的对应级别方法。**为什么需要宏**：调用点常常在日志层"
      "尚未上线的时刻（初始化早期、关闭后期），宏里包一层空指针检查，"
      "让「日志层不在」退化成「这条日志被跳过」，而不是段错误")
D.Row("MAHO_LOG(Level, Category, ...)",
      "带分类的变体（UE Output Log 风格）：分类随消息一起走，"
      "订阅方（如编辑器控制台的分类过滤树）可以按来源聚合与筛选")
D.Row("MAHO_ENSURE_NOT_NULL(GetLog(), L)", "宏内部使用的空指针软着陆原语（来自 **Core/Fatal.h**）")

D.Card("自由函数")
D.Table("签名", "说明")
D.Row("MAHO_LOG_API FLog* GetLog()",
      "全局日志访问点：`Initialize` 里发布、`Shutdown` 里清除。"
      "**函数而不是导出裸变量**：跨 DLL 的裸变量在不同运行库 / 编译器组合下拿不到同一份实例，"
      "而日志必须是同一个出口")

D.Enum("ELogLevel",
       Desc="日志级别枚举 —— 它的存在是为了**把 spdlog 的级别类型挡在边界外**："
            "引擎内部只认这 6 档，映射发生在 `Log.cpp` 里；换日志库不影响任何调用点。")

D.Struct("FLogMessage",
         Desc="**一条被捕获的日志**。订阅者按 `const&` 收到它。\n"
              "**为什么是值快照**：`std::string` 是拷贝过的，所以订阅者可以把它存下来"
              "（例如投递到别的线程 / 塞进环形缓冲）而不用担心源缓冲区失效 —— "
              "这与 `string_view` 风格的「只在回调期间有效」形成明确对比。")
D.SetAccess("public")
D.Field("ELogLevel Level", "本条级别（订阅者按它过滤）")
D.Field("std::string Category",
        "来源分类；空串表示 `None`（兼容不使用分类的旧调用点）")
D.Field("std::string Message", "格式化后的正文（已拷贝，订阅者可留存）")

D.Class("FLog", Base="FFrameExtension, IPipeline<IInit, IShutdown>",
        Desc="日志层本体 —— **帧而不是单例**，因为它要持有 spdlog 日志器这份需要被显式建立 / "
             "冲刷 / 释放的资源：`IInit` 建起来，`IShutdown` 冲刷并放掉。\n"
             "**为什么只挂两个 stage**：日志不参与更早 / 更晚的时序决策，"
             "它只要求「在别人开始记日志之前就绪，在别人还在记日志时别走」——"
             "后半句由别的层声明 `BlockOn` 反向边表达（依赖方向由消费者声明）。\n"
             "调用方从不接触 spdlog 类型：`Trace/Debug/...` 是完美转发模板。")
D.SetAccess("public")
D.Interface("MAHO_DECLARE_FRAME(FLog)",
            "声明帧身份（稳定字面量名字 + `CreateFrame` 工厂）—— "
            "宿主可以用 `Install(ApplyModuleExtension(\"FLog\"))` 按名字装载它")
D.Interface("FLog()",
            "构造：只准备成员，日志器留给 `Initialize` 建（**构造期不做 I/O**）")
D.Interface("~FLog() override",
            "析构：spdlog 日志器是**不完整类型**，其 shared_ptr 的删除必须落在本编译单元"
            "（`Log.cpp`），所以析构显式声明在头、定义在 cpp，而不是 `= default`")
D.Interface("template <typename... Args> void Trace(fmt::format_string<Args...> Fmt, Args&&... A)",
            "`Trace` 级：`fmt::format` 编译期校验后交给 `LogLine`")
D.Interface("template <typename... Args> void Debug(fmt::format_string<Args...> Fmt, Args&&... A)",
            "`Debug` 级，同上")
D.Interface("template <typename... Args> void Info(fmt::format_string<Args...> Fmt, Args&&... A)",
            "`Info` 级，同上")
D.Interface("template <typename... Args> void Warn(fmt::format_string<Args...> Fmt, Args&&... A)",
            "`Warn` 级，同上")
D.Interface("template <typename... Args> void Error(fmt::format_string<Args...> Fmt, Args&&... A)",
            "`Error` 级，同上")
D.Interface("template <typename... Args> void Critical(fmt::format_string<Args...> Fmt, Args&&... A)",
            "`Critical` 级，同上（仍不终止进程）")
D.Interface("template <typename... Args> void Log(ELogLevel Level, std::string_view Category, fmt::format_string<Args...> Fmt, Args&&... A)",
            "带分类的通用入口：级别由参数给出（**这正是 `MAHO_LOG` 宏转发到的那个函数**）。"
            "分类按值收成 `std::string` 再走内部 `LogLine`，因为分类要随消息存活")
D.Field("TMulticastEvent<void(const FLogMessage&)> OnLog",
        "实时日志流：**在发出日志的那个线程上**投递（回调线程 = 上报线程）。"
        "`TMulticastEvent` 自带锁 ⇒ 多线程同时写日志是安全的；"
        "订阅方应在**自己的** `Shutdown` 里解绑，使这股流在日志层拆除前已经空掉")
D.SetAccess("private")
D.Interface("void Initialize(FEngineBase& Engine) override",
            "拉起日志器：stdout 彩色 sink + 滚动文件 sink，解析 `--log-level` 决定阈值，"
            "随后发布 `GetLog()`")
D.Interface("void Shutdown(FEngineBase& Engine) override",
            "`flush` + 释放日志器，并清除 `GetLog()` —— **先冲刷再清指针**，"
            "否则关闭期最后的日志会丢在缓冲里")
D.Interface("void LogLine(ELogLevel Level, std::string Message)",
            "内部落盘路径（无分类）：写 spdlog 日志器 + 按消息快照广播 `OnLog`")
D.Interface("void LogLine(ELogLevel Level, std::string Category, std::string Message)",
            "内部落盘路径（带分类）：把分类一并放进 `FLogMessage` 广播出去，"
            "使「过滤」这件事属于订阅方而不是日志层")
D.Field("std::shared_ptr<spdlog::logger> Logger",
        "spdlog 日志器（**不完整类型**：删除器必须落在 `Log.cpp`，这就是析构函数不能 inline 的原因）。"
        "用 `shared_ptr` 而非 `unique_ptr`：sink / 内部共享更省事，且与 spdlog 的持有方式一致")
