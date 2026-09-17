#!/usr/bin/env python3
# Run via Tools/maho_python.bat — engine Tools/python only.
"""
docs_content.py —— 文档内容（**唯一的数据源**）

每一条都是手工声明的一条原子内容：一个头、一个类、一个接口、一个字段……
顺序就是渲染顺序。不扫描源码，不解析 C++（见 Tools/docs_builder.py 的原子接口）。

渲染：
  Tools\\maho_python.bat Tools\\docs_content.py                 # -> Source/Docs.html
  Tools\\maho_python.bat Tools\\docs_content.py --out X.html
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import docs_builder as D

# 每次渲染都从空开始（内容全部来自本文件）。
# D.Reset()  // 内容已清空：先声明 Header，再逐类 Class / Interface / Field

# ══════════════════════════════════════════════════════════════════════════════
# Public/Core/FrameGraph.h —— 调度器 + 声明层 + 桥
# ══════════════════════════════════════════════════════════════════════════════

# D.Header("Public/Core/FrameGraph.h", Title="FrameGraph —— 帧调度器 / 声明层 / 桥",
#          Desc="三件强绑定的东西放在同一个头里：节点调度器 FFrameGraph、声明层 FFrameExtension、"
#               "把声明翻译成一批任务的 FFrameBridge。")

# D.Macro("MAHO_FRAMES_IN_FLIGHT", "3",
#         "环深度：同时在飞的帧槽数。相位就是 [0, K) 的环索引。")

# D.Struct("FTaskKey", Desc="节点实例的身份三元组。依赖就是指向目标实例的身份。")
# D.Field("std::string_view Name", "稳定身份名（必须指向静态存储，即字符串字面量）")
# D.Field("std::type_index Stage", "阶段类型")
# D.Field("std::int32_t Phase", "相位 = 环索引，不是绝对帧号")
# D.Interface("bool operator==(const FTaskKey& Other) const", "精确比较（注册表用精确键）")

# D.Class("FFrameGraph", Base="FThreadedServer",
#         Desc="节点调度器。全部图状态只被调度线程触碰，所以没有图锁，且该线程绝不阻塞。")
# D.SetAccess("public")
# D.Interface("bool Submit(std::vector<FTask> Tasks, std::string* OutReason = nullptr)",
#             "提交一批任务：校验（批内重复身份 / 相位越界 / 成环）→ 准入（阻塞调用方，"
#             "直到这批点名的相位排空）→ 入队。提交即合并，没有重建。")
# D.Interface("void Wait()",
#             "提交栅栏：阻塞到「我提交过的都完成」。计数由调用方在入队前自增、"
#             "调度线程完成时自减 —— 所以它不会漏掉自己的工作。")
# D.SetAccess("private")
# D.Field("std::deque<FTaskNode> Nodes", "节点表，下标即 FNodeId（deque：元素含 atomic，不可移动）")
# D.Field("std::unordered_map<FTaskKey, FNodeId, FTaskKeyHash> Registry", "身份 → 节点下标")
# D.Field("std::array<std::atomic<std::uint32_t>, kPhaseCount> SlotInFlight",
#         "每相位在飞计数：准入与 Wait 的依据")
# D.Field("std::atomic<std::uint32_t> AwaitingCompletion", "待完成节点数（提交栅栏的计数）")

# ══════════════════════════════════════════════════════════════════════════════
# Source/Maho.h —— 引擎聚合头
# ══════════════════════════════════════════════════════════════════════════════

D.Reset()

D.Header("Public/Maho.h", Title="Maho.h —— 引擎聚合头",
         Desc="插件只需 `#include <Maho.h>`，就同时拿到 Core 基建与引擎帧系统。"
              "它是**唯一的聚合点**（原先的 Core.h 已折进来：一个聚合头只被另一个聚合头包含，"
              "就是纯间接）。下面这张表就是它带进来的东西。")

D.Card("包含的模块")
D.Table("头文件", "功能")
D.Row("Core/TypeList.h", "编译期有序类型列表 `TTypeList` 及运算（拼接 / 成员判断 / 保序去重并集）")
D.Row("Core/Delegate.h", "多播事件 `TMulticastEvent`：bind / broadcast / unbind，线程安全，无 DLL 边界")
D.Row("Core/Singleton.h", "CRTP 单例标识基类 `TSingleton`：纯标记，不强制生命周期")
D.Row("Core/Interface.h", "能力组合器 `IPlugin` / 有序阶段序列 `IPipeline`")
D.Row("Core/FrameGraph.h", "帧调度器 `FFrameGraph` + 声明层 `FFrameExtension` + 桥 `FFrameBridge`")
D.Row("Core/ThreadPool.h", "固定规模线程池 `FThreadPool`：`Submit` 入队即返，`Flush` 锁步屏障")
D.Row("Core/ThreadedServer.h", "常驻专用线程基类 `FThreadedServer`：单线程 + FIFO 串行队列")
D.Row("Core/Assembly.h", "DLL 加载原语 `FAssembly` + `ApplyModuleExtension`（平台后缀）")
D.Row("Core/Fatal.h", "致命 / 错误上报路径 + `MAHO_CHECK` / `MAHO_VERIFY` / `MAHO_ENSURE` 断言宏")
D.Row("Engine/Engine.h", "引擎侧：10 个 stage 能力接口 + `FEngineBase` + 引擎 stage 序列别名")

# ══════════════════════════════════════════════════════════════════════════════
# Source/Public/EntryPoint.h —— 统一应用入口驱动
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/EntryPoint.h", Title="EntryPoint.h —— 统一应用入口驱动",
         Desc="入口只做一件事：把引擎 DLL 装起来、把匿名根实例跑完、再对称地拆掉。"
              "引擎没有任何工程 / 工具预设 —— 它就是一个自包含的 DLL，导出 `CreateEngine()` "
              "返回 `FEngineBase*`。入口负责装载（`FAssembly` 按符号名查找）、驱动生命周期"
              "（`PreMain` → `Main` → `PostMain`）、最后 `delete App`（虚析构，经由 DLL 释放"
              "整个对象）。宿主因此永远不认识具体的引擎类型，只认识 `FEngineBase` 这个锚。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Assembly.h", "`FAssembly` + `ApplyModuleExtension`：装载引擎 DLL、解析平台后缀")
D.Row("Core/Fatal.h", "`InstallFatalHandlers` / `ReportFatal`：进程级崩溃与致命错误兜底")
D.Row("Engine/Engine.h", "`FEngineBase` —— 入口认识的全部（生命周期钩子 + 主循环）")
D.Row("exception", "标准库异常基类：顶层 `catch` 用 `std::exception` 收口")

D.Card("Main 函数")
D.Table("函数签名", "说明")
D.Row("inline int Main(int Argc, char** Argv)",
      "入口主函数，五步：① `InstallFatalHandlers()`；② 取引擎 DLL 路径 —— 第一个命令行参数，"
      "否则 `ApplyModuleExtension(MAHO_ENGINE_NAME)`；③ `FAssembly` 装载 + "
      "`GetProcAs(\"CreateEngine\")` 建根实例（失败即 `ReportFatal`）；④ 按序驱动 "
      "`ParseCommandLine` → `PreMain` → `Main` → `PostMain`；⑤ `delete App`（虚析构经 DLL "
      "释放整个对象）。任何逃出的异常落到顶层 `catch` ⇒ `ReportFatal`（写 stderr + Fatal.log 后 "
      "abort，不尝试恢复，因为此时状态已不可信）。返回引擎 `Main` 的返回值。")

# ══════════════════════════════════════════════════════════════════════════════
# Source/Public/EntryPoint{Windows,Android,Linux,IOS,Xbox}.h —— 平台入口 shim
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/EntryPointWindows.h", Title="EntryPointWindows.h —— Windows 入口 shim",
         Desc="只在一个 .cpp（极薄的 `Main.cpp`）里包含。同时给出 GUI 子系统的 `WinMain`"
              "（不弹控制台黑框）与控制台子系统的 `main`，两者都转调 `Maho::Main`。"
              "平台差异只到这一行为止 —— 往下的生命周期完全共用。")
D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("EntryPoint.h", "统一入口驱动 `Maho::Main`（本 shim 的全部逻辑都在那里）")
D.Row("Windows.h", "Win32：`WinMain` 签名 / `HINSTANCE` / `FreeConsole`"
                   "（先定义 `NOMINMAX`，避免 min/max 宏污染 `std::min/max`）")
D.Card("入口函数")
D.Table("函数签名", "说明")
D.Row("int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)",
      "GUI 子系统入口。先 `FreeConsole()` 摘掉继承来 / 调试器带进来的控制台（否则会闪出黑框），"
      "再 `return Maho::Main(__argc, __argv)`。")
D.Row("int main(int Argc, char** Argv)", "控制台子系统入口：`return Maho::Main(Argc, Argv)`。")

D.Header("Public/EntryPointAndroid.h", Title="EntryPointAndroid.h —— Android 入口 shim",
         Desc="只在一个 .cpp 里包含。`android_main` 运行在 NDK 的 glue 线程上，`Maho::Main` "
              "就在那里阻塞，直到 app 结束。")
D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("EntryPoint.h", "统一入口驱动 `Maho::Main`")
D.Row("android_native_app_glue.h", "NDK glue：`android_app` 与 `android_main` 的宿主循环")
D.Card("入口函数")
D.Table("函数签名", "说明")
D.Row("void android_main(struct android_app* App)",
      "glue 线程上的入口。Android **没有 argv** ⇒ 造一个占位 `Argv` 调 `Maho::Main(1, Argv)`；"
      "该调用阻塞到 app 退出。返回类型是 `void`（不是 `int`），所以引擎的返回值在此被丢弃。")

D.Header("Public/EntryPointLinux.h", Title="EntryPointLinux.h —— Linux 入口 shim",
         Desc="只在一个 .cpp 里包含：一个 `main` 转调 `Maho::Main`，无额外平台依赖。")
D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("EntryPoint.h", "统一入口驱动 `Maho::Main`")
D.Card("入口函数")
D.Table("函数签名", "说明")
D.Row("int main(int Argc, char** Argv)", "`return Maho::Main(Argc, Argv)` —— 引擎返回值原样透传。")

D.Header("Public/EntryPointIOS.h", Title="EntryPointIOS.h —— iOS 入口 shim",
         Desc="只在一个 .cpp 里包含：一个 `main` 转调 `Maho::Main`。iOS 运行时没有动态库，"
              "`ApplyModuleExtension` 对基名原样透传（不加 `.dylib` 后缀）。")
D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("EntryPoint.h", "统一入口驱动 `Maho::Main`")
D.Card("入口函数")
D.Table("函数签名", "说明")
D.Row("int main(int Argc, char** Argv)", "`return Maho::Main(Argc, Argv)` —— 引擎返回值原样透传。")

D.Header("Public/EntryPointXbox.h", Title="EntryPointXbox.h —— Xbox 入口 shim",
         Desc="只在一个 .cpp 里包含：一个 `main` 转调 `Maho::Main`。")
D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("EntryPoint.h", "统一入口驱动 `Maho::Main`")
D.Card("入口函数")
D.Table("函数签名", "说明")
D.Row("int main(int Argc, char** Argv)", "`return Maho::Main(Argc, Argv)` —— 引擎返回值原样透传。")

# ══════════════════════════════════════════════════════════════════════════════
# Source/Public/Core/Assembly.h —— 动态库装载原语
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Core/Assembly.h", Title="Assembly.h —— 动态库装载原语",
         Desc="只回答两个问题：**怎么把一个动态库装进来**，以及**怎么按符号名取函数**。"
              "它不知道模块里是什么 —— 不认插件、清单、工厂；怎么解释一个已装载的模块完全由"
              "消费者决定（插件管理器 / 极薄的启动器 / 工程自己的加载器）。"
              "实现与平台分支都在 `Private/Core/Assembly.cpp`，所以这个头是**平台无关**的"
              "（不引入 `Windows.h` / `dlfcn.h`）。\n"
              "**所有权契约**：`FAssembly` 是模块句柄的唯一持有者（move-only）。只要还有从它"
              "构造出来的实例活着，宿主就必须让这个 `FAssembly` 也活着 —— vtable 与析构函数"
              "都在那个模块里，先卸载再用就是 use-after-free。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_API` 导出 / 导入宏 —— 这里的类型要跨 DLL（插件的 "
                       "`GetModulePath()` 就直接调 `ApplyModuleExtension`）")
D.Row("memory", "`std::unique_ptr<void, FModuleDeleter>`：句柄的 RAII 持有")
D.Row("string", "路径与返回串")
D.Row("string_view", "入参：不拷贝调用方给的路径")

D.Card("自由函数")
D.Table("函数签名", "说明")
D.Row("std::string ApplyModuleExtension(std::string_view BaseName)",
      "给模块基名补上宿主平台的动态库后缀：Windows `.dll` / Android、Linux `.so` / macOS "
      "`.dylib`；无动态库的运行时（iOS）原样透传。全仓因此不硬编码任何 `.dll` 字符串。"
      "**已导出**：每个插件由 `MAHO_DECLARE_FRAME` 生成的 `GetModulePath()` 都从自己的 DLL 调它。")

D.Struct("FModuleDeleter",
         Desc="自定义删除器：模块句柄用 `FreeLibrary` / `dlclose` 释放，**不是** `delete`。"
              "`FAssembly` 用它以 RAII 持有句柄，宿主只需持有 `FAssembly` 值。")
D.Interface("void operator()(void* Handle) const noexcept",
            "释放句柄（空句柄直接返回）。**已导出**：谁销毁 `FAssembly`，这段代码就在谁的模块里跑。")

D.Class("FAssembly",
        Desc="一个已装载的代码单元：OS 模块句柄 + 符号查找。纯装载原语 —— 不认插件 / 清单 / "
             "工厂，怎么解释这个模块完全由消费者决定。句柄唯一持有（move-only）：只要还有从它"
             "构造出来的实例活着，就必须让它也活着，否则先卸载再用就是 use-after-free。")
D.SetAccess("public")
D.Interface("explicit FAssembly(std::string_view Path)",
            "从路径构造：立即 `Load()`；失败时对象处于未装载状态（`IsLoaded()` 为假）。")
D.Interface("FAssembly(FAssembly&&) noexcept = default",
            "可移动（句柄转移）。拷贝被删除：`FAssembly(const FAssembly&) = delete` / "
            "`operator=(const FAssembly&) = delete`。")
D.Interface("bool Load(std::string_view Path)",
            "装载模块（先 `Unload()` 掉旧句柄）；文件缺失或装载失败返回 `false`。")
D.Interface("void Unload()",
            "释放句柄；可重复调用。之后 `IsLoaded()` 为假、`GetProcAddress()` 返回 nullptr。")
D.Interface("bool IsLoaded() const", "是否持有有效的模块句柄。")
D.Interface("void* GetProcAddress(const char* Name) const",
            "原始符号查找（`GetProcAddress` / `dlsym`）；未装载或符号不存在则返回 nullptr。")
D.Interface("template <typename TFunction> TFunction GetProcAs(const char* Name) const",
            "把原始符号转成**函数指针**类型再返回 —— 调用方写 "
            "`GetProcAs<CreateFn>(\"CreateEngine\")` 即可，无需自己 `reinterpret_cast`。")
D.SetAccess("private")
D.Field("std::unique_ptr<void, FModuleDeleter> Module",
        "模块句柄的唯一持有者：析构即 `FreeLibrary` / `dlclose`。空 = 未装载。")

# ══════════════════════════════════════════════════════════════════════════════
# Source/Public/Core/Delegate.h —— 多播事件积木
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Core/Delegate.h", Title="Delegate.h —— 多播事件积木",
         Desc="类型无关的多播事件：绑一批处理器、广播一组值。**header-only、无状态、无 DLL 边界**"
              "，插件可以把它直接暴露成公共 API 的成员类型。\n"
              "**线程安全**：任意线程可 Bind / Unbind / Broadcast / RemoveAll；Broadcast 在锁内"
              "**快照**处理器集合，出锁后再调用 —— 所以处理器可以安全地再入（Bind / Unbind 自己）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("algorithm", "`std::remove_if`：按订阅号摘除处理器")
D.Row("cstdint", "`FSubscriptionID = uint64_t`")
D.Row("functional", "`std::function`：处理器的类型擦除")
D.Row("mutex", "`std::mutex`：保护处理器表（含 Broadcast 的快照时刻）")
D.Row("utility", "`std::move`")
D.Row("vector", "处理器表")

D.Alias("FSubscriptionID", "uint64_t",
        "不透明的订阅号：`Bind` 返回、`Unbind` 消费。**永不重用**，所以陈旧号的 Unbind 只会是无操作。")

D.Class("TMulticastEvent<Signature>", Desc="线程安全的多播事件（bind + broadcast）。"
        "任意线程可绑定 / 解绑 / 广播；Broadcast 锁内复制处理器集合、锁外调用，"
        "因此处理器可以安全再入本事件。")
D.SetAccess("public")
D.Interface("FSubscriptionID Bind(FHandler Handler)",
            "注册一个处理器并返回订阅号。订阅号只摘除**它自己**这一个 —— 每个订阅者拥有自己的"
            "订阅，解绑一个永不影响其它。")
D.Interface("void Unbind(FSubscriptionID ID)",
            "摘除该订阅号对应的处理器。未知号（已解绑 / RemoveAll 之后的陈旧号）是无操作。")
D.Interface("void Broadcast(Args... Values) const",
            "锁内快照、锁外调用：处理器可再入（Bind / Unbind / RemoveAll）而不会自锁。")
D.Interface("void RemoveAll()", "清空全部处理器。")
D.SetAccess("private")
D.Field("std::vector<FEntry> Handlers", "处理器表（订阅号 + 处理器）")
D.Field("FSubscriptionID NextID = 1", "下一个订阅号（单调递增，永不重用）")
D.Field("mutable std::mutex Mutex", "保护处理器表；Broadcast 是 const，故为 mutable")

# ══════════════════════════════════════════════════════════════════════════════
# Source/Public/Core/Export.h —— 模块边界宏
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Core/Export.h", Title="Export.h —— DLL 导出 / 导入（模块边界）",
         Desc="UE 风格的模块边界宏，覆盖五个平台：Windows / Xbox 走 `__declspec`，"
              "Linux / Android / iOS 走 visibility 属性。Xbox 基于 MSVC 但**不定义 `_WIN32`**，"
              "所以显式判断 `_MSC_VER`。\n"
              "未定义 `MAHO_BUILD_SHARED` 时（静态链接）三个宏都展开为空 —— 同一份头两种构建都可用。\n"
              "**规范**：实例可能比构造它的模块活得久、且由**另一个模块**销毁的类型，必须带上所属"
              "插件的 `MAHO_<NAME>_API` 标签。全 inline 的类没有 key function，vftable + deleting "
              "dtor 会**每个模块各生成一份**，`delete` 就可能经由已卸载镜像里的 vptr 分派"
              "（静默 0xC0000005）。")

D.Card("宏")
D.Table("宏", "说明")
D.Row("MAHO_EXPORT", "`__declspec(dllexport)`（MSVC / MinGW）或 `__attribute__((visibility(\"default\")))`（GCC / Clang）")
D.Row("MAHO_IMPORT", "对应平台的导入侧声明")
D.Row("MAHO_API", "按 `MAHO_EXPORTS` 自动选 `MAHO_EXPORT` / `MAHO_IMPORT` —— "
                  "**这是各处类型与函数上真正用的那一个**（由 codegen 为每个插件定义 `MAHO_<NAME>_MODULE_EXPORTS`）")
D.Row("#pragma warning(disable : 4251)",
      "MSVC：导出类里的 STL 成员（`unique_ptr` / `string` …）是安全的 —— 前提是 CRT 一致（`/MD`）")

# ══════════════════════════════════════════════════════════════════════════════
# Source/Public/Core/Fatal.h —— 致命 / 错误路径
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Core/Fatal.h", Title="Fatal.h —— 致命 / 错误上报与断言",
         Desc="把所有「出错」收成两条路：**致命**（写日志后 abort）与**非致命**（只上报，继续跑）。"
              "断言宏也在这里，分硬（假 ⇒ 崩）与软（假 ⇒ 只报一次）两种。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_API` —— 这三个函数是引擎 DLL 的导出符号，其它模块按导入引用")
D.Row("cstdio", "`std::snprintf`：`MAHO_CHECKF` 的格式化消息")

D.Card("自由函数")
D.Table("函数签名", "说明")
D.Row("[[noreturn]] void ReportFatal(const char* Message)",
      "**致命**：写 stderr + `Saved/Logs/Fatal.log`，然后 abort。不返回、不尝试恢复 —— "
      "走到这里说明状态已不可信。")
D.Row("void ReportError(const char* Message)",
      "**非致命**：写 stderr + `Saved/Logs/Fatal.log`，然后返回。坏插件 / 单点失败用它隔离。")
D.Row("void InstallFatalHandlers()",
      "安装 `std::terminate` 处理器；进程入口（`Maho::Main`）第一件事调它，只装一次。")

D.Card("宏")
D.Table("宏", "说明")
D.Row("MAHO_CHECK(Expr)", "**硬不变量**：假 ⇒ `ReportFatal`。Shipping 下表达式被编译掉（副作用不保留）")
D.Row("MAHO_CHECKF(Expr, Fmt, ...)", "同上，但消息经 `snprintf` 格式化（缓冲区 512）")
D.Row("MAHO_VERIFY(Expr)", "与 `MAHO_CHECK` 相同，但表达式**永远求值** —— 需要保留副作用时用它")
D.Row("MAHO_ENSURE(Expr)", "**软不变量**：假 ⇒ 只报一次（`static` 一次性标志），不崩。"
                          "用于「不该发生但不致命」的场景（例如某服务还没初始化）")
D.Row("MAHO_ENSURE_NOT_NULL(PtrExpr, Name)", "软空指针守卫：为空则只报一次并跳过整块；表达式只求值一次")
D.Row("MAHO_IF_NOT_NULL(PtrExpr, Name)",
      "**静默**空指针守卫：表达式只求值一次，非空时执行后面的语句块；为空则**什么都不做**"
      "（不报告）。用于「还没起来是正常的」的可选服务，如 `GetLog()`。写法同 `if`："
      "`MAHO_IF_NOT_NULL(::Maho::GetLog(), L) { L->Info(\"…\"); }`（原先误放在 Export.h，"
      "它是控制流而非模块边界，故与软兄弟 `MAHO_ENSURE_NOT_NULL` 并列于此）")

# ══════════════════════════════════════════════════════════════════════════════
# Source/Public/Core/FrameGraph.h —— 帧调度器 + 声明层 + 桥
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Core/FrameGraph.h", Title="FrameGraph.h —— 帧调度器 / 声明层 / 桥",
         Desc="三件强绑定的东西放在同一个头里：**调度器** `FFrameGraph`、**声明层** "
              "`FFrameExtension`、把声明翻译成一批任务的**桥** `FFrameBridge`。\n"
              "**图的立场**：它自己不产出任何边，只物化交给它的东西；悬空依赖 ⇒ 边不存在"
              "（节点少一个依赖、可能提前就绪，不报告不计数）。**调度线程独占全部图状态** ⇒ "
              "没有图锁，且该线程绝不阻塞（等待都发生在调用方）。\n"
              "两类**结构边**由桥产出（不是声明的）：帧内 stage 链 + 每 stage 的跨帧自边 —— "
              "它们是「stage 序列」本身的语义，而只有桥知道序列。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/ThreadedServer.h", "`FFrameGraph` 继承它：调度器就是「一个持久线程 + FIFO 串行命令队列」")
D.Row("Core/ThreadPool.h", "节点体跑在池上（`Dispatch` 提交、完成通知回投调度线程）")
D.Row("Core/TypeList.h", "`StageIndicesOf` 把编译期 stage 序列摊成运行期数组")
D.Row("array", "每相位一个在飞计数（`SlotInFlight`）")
D.Row("atomic / condition_variable / mutex", "事件标志表、准入与提交栅栏的等待")
D.Row("deque", "节点表与事件表：元素含 `std::atomic` ⇒ 不可移动 ⇒ 不能用 `vector`")
D.Row("functional", "节点体（闭包）")
D.Row("span / string / string_view", "批次的输入视图；诊断与身份名")
D.Row("typeindex / unordered_map / vector", "阶段类型索引；身份 → 节点下标的注册表")

D.Card("宏 / 常量 / 别名")
D.Table("签名", "说明")
D.Row("MAHO_FRAMES_IN_FLIGHT", "环深度（默认 3）：同时在飞的帧槽数")
D.Row("kPhaseCount", "= `MAHO_FRAMES_IN_FLIGHT`：相位空间就是环 `[0, K)`，没有独立相位")
D.Row("constexpr bool IsValidPhase(std::int32_t)", "相位是否落在环内")
D.Row("Detail::EventOf(FNodeId Id, std::int32_t Phase)",
      "算完成事件的下标：`Id * K + Phase` —— 不分配、不回收，事件只是一把钥匙")
D.Row("FEvent / FNodeId / InvalidEvent / InvalidNodeId",
      "事件下标 / 节点下标（各带一个无效哨兵）。事件**不是对象**：没有锁、没有等待者列表")
D.Row("StageIndicesOf(TTypeList<...>)", "编译期 stage 序列 → `std::array<type_index, N>`（桥的输入）")

D.Struct("FTaskKey", Desc="一个节点实例的**身份三元组**。依赖就是指向目标实例的身份 —— "
                          "没有 delta、没有弱依赖标志。")
D.SetAccess("public")
D.Field("std::string_view Name", "稳定身份名（**必须指向静态存储**，即字符串字面量）")
D.Field("std::type_index Stage", "阶段类型")
D.Field("std::int32_t Phase", "相位 = 环索引，不是绝对帧号")
D.Interface("bool operator==(const FTaskKey&) const", "精确比较（注册表用精确键，故无碰撞场景）")

D.Struct("FTaskKeyHash", Desc="`FTaskKey` 的哈希（`unordered_map` 注册表用）。")
D.Interface("std::size_t operator()(const FTaskKey&) const noexcept", "混入 Name / Stage / Phase 三者")

D.Struct("FDependency", Desc="一条依赖 / 阻塞：就是目标实例的身份。")
D.Field("FTaskKey Target", "指向的目标实例")

D.Struct("FTask", Desc="一条**声明**（每帧提交、可校验）：身份 + 依赖 + 执行体。")
D.Field("FTaskKey Key", "本节点的身份")
D.Field("std::vector<FDependency> Dependencies", "我等待谁（前向）")
D.Field("std::vector<FDependency> Blocks", "谁被我阻塞（反向，允许单向声明）")
D.Field("std::function<void()> Closure", "节点体。**拿不到图** —— 完成由调度器置位")

D.Struct("FTaskNode", Desc="运行时节点：调度线程私有状态。")
D.Field("FTaskKey Key", "身份")
D.Field("std::vector<FEvent> Dependencies", "入边：等待的事件")
D.Field("std::uint32_t UnmetCount", "其中还没置位的有几个")
D.Field("std::vector<FNodeId> Successors", "反向表：谁在等我")
D.Field("FEvent CompletionEvent", "我的完成事件")
D.Field("std::function<void()> Closure", "节点体")
D.Field("std::atomic<bool> bDispatched", "派发标志：区分「在飞」与「尚未派发 / 已回收」")

D.Class("FFrameExtension", Desc="**声明层**：只闭合自己 —— 一个稳定名字 + 它声明的边。"
        "它不知道 stage 序列（那是引擎侧的事），也不管理依赖生命周期。")
D.SetAccess("public")
D.Interface("virtual std::string_view GetName() const = 0",
            "稳定身份名。由 `MAHO_DECLARE_FRAME` 从类型名字符串化而来，是**编译期字面量**")
D.Interface("void AddDependency(std::type_index MyStage, FEdge Edge)",
            "前向：我在 `MyStage` 等这条边指向的目标")
D.Interface("void AddDependent(std::type_index MyStage, FEdge Edge)",
            "反向：我在 `MyStage` 阻塞它。**单向即够** —— 对方不必知道")
D.Interface("const std::unordered_map<...>& GetDependencies() const", "逐 stage 的前向表（桥读）")
D.Interface("const std::vector<...>& GetDependents() const", "反向声明表（桥读）")
D.Field("FEdge{ TargetName, TargetStage, FrameOffset }", "嵌套类型：一条边 = 目标的名字 + 阶段 + "
        "**相对**帧偏移（0 = 本帧，-1 = 上一帧；由桥解析成绝对相位）")
D.Field("Dependencies / Dependents", "两张表（private；声明只经上面的 DSL/sugar）")

D.Class("FFrameBridge", Desc="**桥**：把「frame 集合 + stage 序列 + 帧号」建成一批 `FTask`。"
        "它只构建、不提交（提交是调用方的事），所以桥不涉及准入 / 占用。")
D.SetAccess("public")
D.Interface("static FResult Build(std::span<FFrameExtension* const> Frames, "
            "std::span<const std::type_index> Stages, std::int32_t Frame, IDispatch& Dispatch)",
            "解析相对偏移 → 绝对身份；为**实现的** stage 产节点（未实现不产）；产出两类结构边；"
            "报出写错的声明")
D.Interface("static std::int32_t PhaseOf(std::int32_t Frame) noexcept",
            "绝对帧号 → 相位（环运算属于桥；图只见绝对身份）")
D.Interface("IDispatch::Implements(frame, stage)", "策略：这个 frame 实现该 stage 吗")
D.Interface("IDispatch::MakeClosure(frame, stage)", "策略：把「调用该 frame 的该 stage」包成闭包")
D.Field("FDiagnostic{ Reason, Frame, Stage, Target, TargetStage, FrameOffset, bReverse }",
        "嵌套：只报**写错**（名字不在帧集 / 阶段不在序列 / 目标未实现该阶段）；相位不同不报")
D.Field("FResult{ Tasks, Diagnostics }", "嵌套：一批 `FTask` + 报告")

D.Class("FFrameGraph", Base="FThreadedServer",
        Desc="**调度器**。全部图状态只被调度线程触碰，所以**没有图锁**；调度线程也**绝不阻塞** "
             "（准入与栅栏的等待都发生在调用方）。")
D.SetAccess("public")
D.Interface("bool Submit(std::vector<FTask> Tasks, std::string* OutReason = nullptr)",
            "提交一批：① 校验（批内同一身份重复 / 相位越界 / 成环）② **准入**（阻塞调用方，"
            "直到这批点名的相位排空）③ 入队。提交即合并 —— 没有重建动作")
D.Interface("void Wait()",
            "**提交栅栏**：阻塞到「我提交过的都完成」。待完成计数由调用方在入队**前**自增、"
            "调度线程完成时自减 —— 所以它不会漏掉自己的工作（等相位计数则会在派发空档漏掉）")
D.SetAccess("private")
D.Field("std::deque<FTaskNode> Nodes", "节点表，下标即 FNodeId")
D.Field("std::unordered_map<FTaskKey, FNodeId, FTaskKeyHash> Registry", "身份 → 节点下标（精确键）")
D.Field("std::deque<std::atomic_bool> EventStates", "事件标志表（用下标寻址，不分配不回收）")
D.Field("std::array<std::atomic<std::uint32_t>, kPhaseCount> SlotInFlight", "每相位在飞计数：准入依据")
D.Field("std::atomic<std::uint32_t> AwaitingCompletion", "待完成节点数：提交栅栏的计数")
D.Field("SlotCv / SlotMutex", "等待 / 唤醒（只在提交方与完成通知之间配对）")

# ══════════════════════════════════════════════════════════════════════════════
# Source/Public/Core/Interface.h —— 能力 / 阶段组合器
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Core/Interface.h", Title="Interface.h —— 能力组合器 / 阶段管线组合器",
         Desc="两个类型层面的组合器，都**不含任何行为**：`IPlugin` 把一组能力 trait 作为虚基类装"
              "进来（**无序**集合，供能力查询 / `dynamic_cast` 用）；`IPipeline` 声明一条**有序**"
              "阶段序列并暴露 `TStages`。stage → 方法调用的 `Invoke` 协议由具体调度上下文实现，"
              "Core 本身不预设任何阶段。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_API`：两者都是跨 DLL 使用的类型")
D.Row("Core/TypeList.h", "`IPipeline::TStages` 用 `TTypeList` 表示有序序列")
D.Row("type_traits", "`std::is_base_of_v` 等能力查询")

D.Class("IPlugin<TCapabilities...>", Desc="**能力组合器**：一组能力的无序集合，"
        "没有顺序语义 —— 只用于能力查询（`dynamic_cast`）与按能力分派。")
D.Interface("virtual ~IPlugin() = default", "虚析构：跨 DLL 通过基类指针销毁是合法的")

D.Class("IPipeline<TStageTypes...>", Desc="**阶段管线组合器**：参数顺序就是该管线的节点顺序"
        "（例如 `IPipeline<IInit, IMain, IShutdown>` = Init → Main → Shutdown）。"
        "它只承载阶段列表，`Invoke` 协议由具体管线类实现。")
D.Interface("virtual ~IPipeline() = default", "虚析构")
D.Field("using TStages = TTypeList<TStageTypes...>", "有序阶段接口列表（public）：调度侧据此展开节点")

# ══════════════════════════════════════════════════════════════════════════════
# Source/Public/Core/Singleton.h —— CRTP 单例基类
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Core/Singleton.h", Title="Singleton.h —— CRTP 单例标识基类",
         Desc="**纯身份 / 标志基类**：没有 inline Meyers 单例、不强制生命周期。派生类自己声明 "
              "`static T& Get()` 并在**自己的 .cpp**（编进它所在那个 DLL）里定义 ⇒ 实例只存在于"
              "某个 DLL 的一个 TU 中 ⇒ **跨 DLL 进程唯一**（若把 `inline static` 局部变量写在头里，"
              "每个包含它的 DLL 都会各有一份）。\n"
              "`is_base_of_v<TSingleton<T>, T>` 仍能识别一个类是不是单例（查询遍历不变）。"
              "需要生命周期时用 `IPlugin<IInit, IShutdown>` 组合，而不是无条件继承。")

D.Class("TSingleton<T>", Desc="CRTP 基类：把「我是单例」这件事做成一个类型标志。"
        "构造 / 析构 / 拷贝都是 protected 或删除的 —— 只有派生类能造实例。")
D.SetAccess("protected")
D.Interface("TSingleton() = default", "protected 构造：外部无法直接创建")
D.Interface("~TSingleton() = default", "protected 析构：同上")
D.SetAccess("public")
D.Interface("TSingleton(const TSingleton&) = delete", "不可拷贝（拷贝赋值同样删除）")


# ══════════════════════════════════════════════════════════════════════════════


def main() -> int:
	Parser = argparse.ArgumentParser()
	Parser.add_argument("--out", default="Source/Docs.html")
	Args = Parser.parse_args()
	Headers, Entities, Members = D.Build(Args.out)
	print(f"[docs] {Args.out}: {Headers} 个头, {Entities} 个实体, {Members} 个成员")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
