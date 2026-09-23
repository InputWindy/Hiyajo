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
# Source/Public/Core/TypeList.h —— 编译期类型列表
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Core/TypeList.h", Title="TypeList.h —— 编译期有序类型列表",
         Desc="类型层面的「数组」：`TTypeList<A, B>` 与 `TTypeList<B, A>` 是**不同类型**（顺序即"
              "语义），但**不编码**遍历是串行还是并行 —— 那是调用方的选择。全套操作都是纯模板，"
              "零运行期开销、无状态。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("cstddef", "`std::size_t`（`Count`）")
D.Row("type_traits", "`std::conditional_t` / `std::false_type` / `std::true_type`（并集与成员判断用）")

D.Struct("TTypeList<TTypes...>", Desc="编译期有序类型列表（「类型数组」）。顺序即语义。")
D.Field("static constexpr std::size_t Count = sizeof...(TTypes)", "元素个数（编译期常量）")

D.Alias("TCons<T, TList>", "TTypeList<T, Ts...>", "把头插到最前")
D.Alias("TAppend<TList, TValue> / TAppend_t", "TTypeList<Ts..., TValue>", "把尾追加到最末")
D.Alias("TContains<TList, T> / TContains_v", "bool", "成员判断（编译期布尔）")
D.Alias("TCatch<TLists...>", "拼接结果", "把多个 `TTypeList` 按序拼成一个")
D.Alias("TUnionList_t<TListA, TListB>", "保序去重的并集", "折叠式并集：逐个追加、已在集合内则跳过")

# ══════════════════════════════════════════════════════════════════════════════
# Source/Public/Core/ThreadPool.h —— 固定规模线程池
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Core/ThreadPool.h", Title="ThreadPool.h —— 固定规模线程池",
         Desc="常驻 worker + FIFO 任务队列。`Submit` 入队即返（乱序在 worker 上跑），`Flush` 是"
              "**锁步屏障**：等真正「执行完」而不是「出队」。workers 首次 `Submit` 时**惰性启动**"
              "（只会增长，从不收缩）。任务是并发跑的，必须自身线程安全。\n"
              "**实现全在 `Private/Core/ThreadPool.cpp`**：池是每个装了收集器的插件里的**成员**"
              "（`FFrameBuilder::Pool`），所以类带 `MAHO_API` —— 跨 DLL 调用，而不是每个模块内联一份。\n"
              "**诊断开关**：默认构造的池读 `MAHO_PARALLELISM`（如 `=1`）覆盖线程数，用来复现串行"
              "执行、把并行失败与已知良好基线直接对比；显式指定宽度的池（如 RHI 的串行录制池）不受影响。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_API` —— 插件会跨 DLL 构造 / 调用它")
D.Row("condition_variable / mutex", "队列等待与「真正空闲」屏障")
D.Row("cstdint", "`std::uint32_t`（宽度与在飞计数）")
D.Row("deque", "FIFO 任务队列")
D.Row("functional", "`std::function<void()>` 任务")
D.Row("thread / vector", "worker 线程与其容器")

D.Class("FThreadPool", Desc="任务执行原语：一组 worker 线程 + 若干 **lane**。lane 是一条独立的"
        "队列 + 完成计数，共用同一批 worker。`NumThreads = 0` 时取 "
        "`std::thread::hardware_concurrency()`（为 0 则退化为 1）。不可拷贝、不可赋值。\n"
        "**为什么有 lane：**引擎需要**每个收集器一条栅栏** —— 收集器的 `Wait()` 只能排空**它自己**"
        "提交的工作，不能等别的收集器（这正是「节点体可以阻塞在另一个图的静默点」成立的前提）。"
        "给每个收集器一个池能做到，但会把**线程数**乘以收集器份数（4 × 核数 = 24 核机器上 96 条"
        "线程，去跑约 2 个并发节点）。lane 把被混在一起的两件事拆开：worker 集共享，队列与计数"
        "按 lane 独立。\n"
        "它还顺手消掉一个共享必然带来的陷阱：栅栏不能被「自己的计数包含了自己的线程」调用 —— "
        "`Flush` 等的是归零，而调用者那颗任务要等 `Flush` 返回才结束。而帧的 stage body 是由**父**"
        "收集器的图派发的，所以收集器在 stage body 里排空**自己**的 lane 是一次**跨 lane** 排空，"
        "与从前「跨池」是同一件事。")
D.SetAccess("public")
D.Interface("using FLane = std::uint32_t", "lane 句柄。`DefaultLane = 0` 是池自身那条；`MaxLanes = 16`")
D.Interface("explicit FThreadPool(std::uint32_t NumThreads = 0)", "构造：**不立刻起线程**，只记宽度")
D.Interface("~FThreadPool()", "置停止标志、唤醒并 join 全部 worker")
D.Interface("void Submit(std::function<void()> Task)",
            "入队即返（默认 lane，**不带身份**：只会开出一个无名条，所以带身份一律走下面那个重载）；"
            "惰性把整个池拉起来（首次调用即补齐到宽度）")
D.Interface("void Submit(FTaskTrace Trace, std::function<void()> Task)",
            "默认 lane + 任务身份。**trace 条在任务 RUN 的地方开，不在提交处开**（见 `RunTracedTask`）："
            "提交方不可能知道工作何时开始，而埋点写在调用点的版本正是会被忘掉的那个")
D.Interface("void Flush()", "默认 lane 上的屏障（见下）")
D.Interface("[[nodiscard]] FLane CreateLane()",
            "在同一批 worker 上开一条新 lane（自己的队列 + 计数）。池满（`MaxLanes`）时**上报并**"
            "返回 `DefaultLane` —— 调用者退化为共用默认栅栏，而不是拿到一条没人能推断的 lane")
D.Interface("void DestroyLane(FLane Lane)",
            "归还 lane。**仍有工作时拒绝并上报**：把还在计数里的工作交还回去，等于把它的完成"
            "记到别人头上。lane 0 永不归还")
D.Interface("void Submit(FLane Lane, FTaskTrace Trace, std::function<void()> Task)",
            "按 lane 入队，并带上任务身份三元组 `{Group, Name, Stage}`（`Name` 同时就是 lane 的键 —— "
            "`FScopedTracePair` 按它哈希选行，所以任务必然落在提交方建立的那一行上）。"
            "**未知 lane 上报后回落到默认 lane** —— 工作永远不会被静默丢掉")
D.Interface("void Flush(FLane Lane)",
            "**真正空闲**屏障：等该 lane 队列空且计数归零（计数在任务**完成后**才减），并在等待"
            "期间容忍并发 `Submit`（嵌套图会从 worker 里再投任务）。调用者若**本身是该池的 worker**"
            "（即它正跑着这个池的任务），会**帮助排空它正在等的那条 lane** —— 否则一个 worker 全"
            "被阻塞在这里的池，队列没人能跑，计数永远回不到零。只排空**目标 lane**，所以既不改变"
            "语义也不会顺手碰别人的工作")
D.Interface("[[nodiscard]] std::uint32_t GetNumThreads() const", "池宽（`= 0` 构造时的解析结果）")
D.Interface("[[nodiscard]] std::uint32_t GetLanePending(FLane Lane) const", "诊断/测试：该 lane 未完成的任务数")
D.SetAccess("private")
D.Interface("void EnsureThreads(std::uint32_t Required)", "把池长到至少 Required（受宽度上限约束，从不收缩）")
D.Interface("void WorkerLoop()",
            "worker 主体：等「任一条 lane 有活」→ 轮转取一条（不让忙的 lane 饿死安静的）→ 跑 → "
            "减该 lane 的计数；任务抛出的异常被隔离上报")
D.Interface("bool TakeAnyTaskLocked(FQueuedTask& OutTask, FLane& OutLane)",
            "从游标开始轮转，取第一条非空 lane 的任务")
D.Interface("bool AnyLaneHasWorkLocked() const", "worker 的唤醒条件（任一条 lane 队列非空）")
D.Interface("void RunTracedTask(const FTaskTrace& Trace, const std::function<void()>& Task)",
            "**全引擎唯一的埋点位置**：在任务真正 RUN 的地方开一个 `FScopedTracePair(Group, Name, "
            "Stage)`，再交给 `RunTaskSafely`。放在任务边界而不是每个提交方的调用点，是因为这是每一份"
            "工作都必经的唯一地点 —— 时间轴因此不可能出现「只因为某个提交方忘了给自己埋点」而缺掉的条。"
            "scope 同时确定 lane，所以 stage body 里的手写埋点依旧落在它所属帧那一行。"
            "`Trace.Name` 为空则什么都不开（没有标签的条只会往默认 lane 上堆无名块）。"
            "提交方忘了带身份，条就消失了；带身份的任务走这里，worker 取到的和 `Flush` 帮助排空的都走这里")
D.Interface("void RunTaskSafely(const std::function<void()>& Task)",
            "跑一个任务并隔离异常，期间把本线程标记为**该池的 worker**（`thread_local`，保存/恢复）"
            "—— 这个标记正是 `Flush` 帮助排空的许可来源：不是本池 worker 的线程（游戏线程、关闭路径）"
            "必须留在队列之外，让节点体在那里跑恰恰是池存在的意义")
D.Interface("void NotifyWorkAvailableLocked() / NotifyProgressLocked()",
            "唤醒条件式通知：有 `Flush` 等待者时用 `notify_all`（等待者可能需要帮助排空），"
            "否则 `notify_one`，免得每次入队都惊群")
D.Interface("struct FQueuedTask", "队列条目 = `FTaskTrace Trace` + `std::function<void()> Task`（**身份跟着任务走**，"
            "所以无论谁把它取出来跑 —— worker 或帮助排空的 `Flush` —— 都能画出同一条）")
D.Field("std::vector<std::thread> Workers", "worker 线程（**共享给所有 lane**）")
D.Field("std::array<FLaneState, MaxLanes> Lanes", "lane 表；`FLaneState` = 队列 + `Pending` + `bInUse`")
D.Field("FLane NextLaneToServe", "轮转游标")
D.Field("std::uint32_t FlushWaiters", "当前阻塞在 `Flush` 的线程数（决定通知方式）")
D.Field("std::mutex Mutex / std::condition_variable CondVar",
        "一把锁管所有 lane 的队列、计数与唤醒簿记；临界区只有入队/出队，所以按 lane 分锁买不到"
        "什么，却会让「哪条 lane 有活」的扫描失去原子性")
D.Field("std::uint32_t NumThreads", "池宽")
D.Field("bool bStopping = false", "停止标志（析构时置位）")


# ══════════════════════════════════════════════════════════════════════════════
# Source/Public/Core/ThreadedServer.h —— 常驻单线程服务
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Core/ThreadedServer.h", Title="ThreadedServer.h —— 常驻专用线程",
         Desc="一个持久线程 + FIFO 串行任务队列。给**长期角色**用（渲染线程、IO 装载线程、调度器"
              "本身），不是瞬时并行任务（那种用 `FThreadPool`）。派生 + 覆写 "
              "`OnInitialize` / `OnShutdown` / `GetThreadName` 做角色专属设置。\n"
              "**实现全在 `Private/Core/ThreadedServer.cpp`，连虚函数也在那里** —— 这样这个类在 "
              "`Maho.dll` 里有一个 **key function**：vtable 与 deleting dtor 全进程只有一份，而不是"
              "每个模块一份 COMDAT（后者正是 `Core/Export.h` 警告的那种隐患）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_API` —— 插件侧的角色类派生自它")
D.Row("atomic", "`bRunning`：跨线程读的运行标志")
D.Row("condition_variable / mutex", "队列等待与 `Flush` 屏障")
D.Row("deque / functional", "FIFO 任务队列与任务类型")
D.Row("thread", "那一个持久线程")

D.Class("FThreadedServer", Desc="常驻专用 worker 的基类。不可拷贝、不可赋值。")
D.SetAccess("public")
D.Interface("FThreadedServer() = default", "构造只初始化标志，**不起线程**")
D.Interface("virtual ~FThreadedServer()", "析构调 `Shutdown()` —— 停止 + join，幂等")
D.Interface("bool Initialize()", "启动专用线程，幂等；`OnInitialize()` 返回 false 则启动失败")
D.Interface("void Shutdown()", "停止 + join，幂等；join 后调 `OnShutdown()`")
D.Interface("[[nodiscard]] bool IsRunning() const", "运行标志（任意线程可读）")
D.Interface("void Submit(std::function<void()> Task)", "入队即返；FIFO、**串行**执行。条目**只装工作本身** —— "
            "服务器跑的是闭包、对埋点一无所知：角色要在 profile 里看见自己的活，就在它提交的那个 body 里自己"
            "开条（`Trace.h`），不是在提交处")
D.Interface("void Flush()", "屏障：阻塞到本次调用之前提交的任务全部完成（实现是入队一个普通任务、再等它跑完）。"
            "服务器**不开任何 trace 标签** —— 埋点属于提交方的 body")
D.SetAccess("protected")
D.Interface("[[nodiscard]] virtual bool OnInitialize()", "线程启动前调用；返回 false 中止启动")
D.Interface("virtual void OnShutdown()", "线程 join 之后调用")
D.Interface("[[nodiscard]] virtual const char* GetThreadName() const", "线程名（默认 ThreadedServer）")
D.SetAccess("private")
D.Interface("void RunLoop()", "线程主体：取任务 → 跑（异常隔离上报）→ 循环到停止且队列空。**等待本身不画条**，"
            "服务器也**不替任何人开条**：它跑的是闭包、对埋点一无所知")
D.Field("std::thread Worker", "那一个持久线程")
D.Field("std::deque<FQueuedTask> Queue", "FIFO 串行队列；条目就只有那份工作（**埋点属于提交方的 body**）")
D.Field("std::mutex Mutex / std::condition_variable CondVar", "队列与屏障的同步")
D.Field("std::atomic<bool> bRunning", "是否已启动")
D.Field("bool bStopping = false", "停止标志（`Shutdown` 置位）")

# ══════════════════════════════════════════════════════════════════════════════
# Plugins/Common/Log/Public/Trace.h —— CPU 作用域追踪（住在 Log 插件里）
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Plugins/Common/Log/Public/Trace.h", Title="Trace.h —— CPU 作用域追踪（Log 插件）",
         Desc="**不在 Core**：追踪的全部词汇都在 **Log 插件**里（实现是 `Private/Trace.cpp`）。Log 拥有"
              "进程的诊断（sink 与 `r.Trace` CVar），trace 文件只是它的一个 sink，所以引擎核心里**一个"
              "追踪符号都没有**；接口以 `MAHO_LOG_API` 导出，任何层 include 一下就能埋点。\n"
              "**三个宏，一条 bar**（三种形式写的是同一种行）：\n"
              "`[tr] ts=<us> dur=<us> lane=<group> worker=<k> owner=<track> stage=<short> name=<label> [func=<fn>] tip=<text>`\n"
              "`lane`（GROUP）是**架构分区**（Engine / FRender / RHIServer），在 Perfetto 里折叠成一个 "
              "PROCESS —— 左侧面板于是读起来就是架构；`owner` 是当时在跑的那个帧（常驻线程的 SCOPE "
              "形式里就是它自己的角色名），是分区**里面的一行（TRACK）**。一行只能「嵌套或先后」，而引擎"
              "把一帧的 stage 按帧内链的顺序串起、每个 stage 又对上一帧的同一 stage 串行 ⇒ 同一行的条"
              "**天然嵌套**，从不重叠。\n"
              "`stage` 是机器键（开这根条的 stage 接口的短类型名），`name` 是人读的标签，`func` 是这根"
              "埋点住在哪个函数里，`tip` 是悬停说明 —— 手写的 SECTION 继承环境行，所以它永远落在所属帧"
              "那一行上。\n"
              "**起点带「嵌套深度 x 1us」的固定偏移**（`thread_local` 深度，进出各加减一）：trace 的"
              "分辨率是 1us，父条与它紧接着开的子条常常落在同一微秒，而时间线靠「严格递增」判层级 —— "
              "不偏移就退化成导入报错。\n"
              "**依赖被画成箭头**：stage 知道它自己声明的边（`FFrameExtension::GetDependencies`），于是"
              "条关闭时写下「我的输入来自哪里」；读的人按 `(lane, owner, stage)` 绑定两端，把「这两根条"
              "离得很远」变成「这一根等了那一根」。\n"
              "`ts` 是**单调时钟**上距首次追踪的微秒数，跨线程可比。行是手写装配的（`FLine`：memcpy + "
              "十进制循环 + 一次 `fwrite`），且**装不下的行被丢弃，绝不截断**：截短帧名会给同一个帧凭空"
              "多出一行，比丢一条还糟。\n"
              "**注意它不是什么**：不是完整事件流。每个 scope 两次读钟 + 一行缓冲，所以只该埋几十个"
              "真正要紧的作用域，而不是每个调用。环境开关都只读一次，于是关闭时整条路径就是一个可预测"
              "的分支。")

D.Card("环境开关（各自只读一次）")
D.Table("开关", "说明")
D.Row("MAHO_TRACE", "任意值即**初始开启**。它只决定初值：运行期由 `r.Trace`（Log 插件的 CVar）覆盖，"
      "随时可开可关、不必重启，且是**进程级**。关闭时 `FStageTrace` / `FScopeTrace` / `FSectionTrace` "
      "的构造只剩几次指针拷贝加一个分支（一个 relaxed 原子读）")
D.Row("MAHO_TRACE_FORMAT", "**写哪几个文件**：`0` = 只要文本，`1` = 只要原生，`2` = 两个都写（**默认**）。"
      "只读一次；非法值回落到 2 —— 默认同时走两条路，于是两条路都被练习到")
D.Row("MAHO_TRACE_MIN_US=<n>",
      "**stage 条的时长门槛**（默认 0 = 全留）。stage 条是为**每一个**帧的**每一个** stage 生成的，"
      "所以它们才是让 trace 变贵的东西；手写 SECTION / SCOPE 是人对「什么重要」的判断，**永不被过滤**。\n"
      "代价说清楚：行是从事件派生出来的，所以一个帧若全部分条都低于门槛，它会**连行一起消失**"
      "（随后手写埋点落在数字 lane id 下），而被丢掉的条**把它的箭头一起带走**（箭头两端都得存在）；"
      "原生输出**没有门槛** —— BEGIN/END 必须成对，条短就只是短。\n"
      "实测（ExampleEngine，Debug，12s）：门槛 0 → 339501 条 / 23.3MB；门槛 200us → 157985 条 / 11.3MB，"
      "留下的正好是有分量的那些（`FUIFeature::IInitViews` / `IRenderUI` / `FRender::IEndFrame` …）。",
      )
D.Row("MAHO_TRACE_STAGES",
      "帧图节点的 enter/exit 括注（见 `Source/Private/Core/FrameGraph.cpp`）。它只为一个失败模式存在："
      "硬崩（0xC0000005）没有栈、也抛不出任何 C++ 能接的东西，于是「最后进入而没退出」的那个 stage "
      "就是唯一能指出真凶的线索")

D.Card("两种输出（由 `MAHO_TRACE_FORMAT` 选）")
D.Table("文件", "说明")
D.Row("Profile_trace.txt", "**文本**：一根条一行、一根箭头一行，grep 得动，也是**崩溃场景的兜底** —— "
      "进程崩掉时原生文件未必收得完整。`(ts, dur, lane, name)` 到 Chrome Trace Event Format 只差一次"
      "机械改写，所以 `Tools/trace_to_chrome.py` 仍能把它变成 chrome://tracing / Perfetto 直接加载的 "
      "JSON（它是**兜底路径**：旧文本 trace，或只吃 JSON 的工具）。`MAHO_TRACE_MIN_US` **只管这一路**")
D.Row("Profile_trace.perfetto_trace", "**原生**：Perfetto 自己的 protobuf，由插件**自己写**"
      "（`Private/Trace.cpp` 的 `FProtoWriter`：手搓 varint，字段号是从一个真的 `.perfetto_trace` 上"
      "读回来的）。一根 lane 一个 `track_descriptor`（一个 **PROCESS**：`uuid` / `name` / "
      "`process{pid, process_name}`），lane 里每个 owner 再一个（它里面的 **THREAD**：`uuid` / `name` / "
      "`parent_uuid` / `thread{pid, tid, thread_name}`；靠 pid 相等归到那个 process 之下）。"
      "一根条 = `TYPE_SLICE_BEGIN` + `TYPE_SLICE_END` 一对包 ⇒ **从不写时长**，所以条不可能重叠、"
      "也不需要收尾隔板。这就是**常规路径**：直接打开它，没有转换器，也没有「这个时间戳属于哪根条」"
      "的猜。\n"
      "**时间戳的单位是纳秒**（Perfetto 的 `TracePacket.timestamp` 本来就按 ns 读），而引擎的钟是**微秒**"
      "（`TraceNowMicros`）—— 换算只发生在**写包那一处**（`ts * kNanosPerMicro`），内部排序、门槛、"
      "文本路径全是微秒。填错的症状很隐蔽：所有相对关系都对，但每根条短 1000 倍、整根时间轴紧 1000 倍"
      "（实测：2.5ms 的 `Render tick` 显示成 2.548µs，而它的 Start time 2.316395ms 对应原始值 2316400）。\n"
      "除帧自己的分区之外，**每根 stage 条还会多写一条镜像：`lane = ThreadPool`、行 = 执行它的线程**"
      "（见下）")

D.Card("`ThreadPool` 组 —— 每根 stage 条都还有一份「按线程」的镜像（只进原生）")
D.Row("lane=`ThreadPool`，行=`Thread <n>`，名字=`<帧名> <标签>`",
      "整个引擎只有**一个** `FThreadPool` 给所有 FrameGraph 用，而泳道 = 分区（帧组）回答的是「**哪个帧**」"
      "—— 想看清「所有 stage 节点在时序上是怎么被执行的」，就得有一个按**执行线程**排列的视图："
      "于是每根 stage 条在原生文件里**再写一条镜像**，`lane = ThreadPool`、行 = 跑它的那个线程"
      "（`Thread <n>`，n 就是 `worker=` 那个稠密序号）、名字 = `<帧名> <标签>`（这一组的行不再说明来源，"
      "所以名字里带上帧）。\n"
      "镜像与本体**同 ts、同 BEGIN/END 配对**；**箭头**由 join 在定出帧组的边之后**再写一份到两个镜像条上**，"
      "但**只画两端线程不同的那条** —— 同一线程的箭头在这一组里只是某个行内部的环，和帧组"
      "「不画同一行」是同一个理由（这一组的行就是线程）。镜像对用的是**独立的一段 id**（`^ kPoolFlowSalt`，"
      "XOR 保一一对应、且不动最高位），否则一条 flow 会带上四个端点、画出一团乱线。\n"
      "只有原生文件有这份镜像 —— **文本路径不镜像**，体积不动；镜像的 `ThreadPool` 是个固定字面量，"
      "与帧自己声明的分区同名也不会冲突（帧的分区名来自 `TraceGroupName()`）")

D.Card("箭头 —— 声明的依赖变成时间线上的 flow")
D.Table("形式", "说明")
D.Row("[tr] flow ts=<us> lane=<group> owner=<track> stage=<short> from=<目标帧> fromStage=<短 stage> off=<帧偏移>",
      "**文本形式**：`off` 是声明时的帧偏移（0 = 同帧，-1 = 上一帧）。每根条关闭时按该 stage 声明的边"
      "各写一行，帧内链的前驱也写一行（它不在 `GetDependencies()` 里，是编译期推出来的）")
D.Row("flow_ids（字段 36） / terminating_flow_ids（字段 48）",
      "**原生形式**：生产者的 **END** 包带上 `flow_ids`，消费者的 **BEGIN** 包带上 "
      "`terminating_flow_ids`（没有开着的 flow 时它被忽略 ⇒ 每条边每帧恰好一个箭头）。\n"
      "`id = FNV-1a(owner | stage)`，由**生产者的身份**定键，**两端各自独立算不出来** —— 一个帧只看得见"
      "**自己**的声明表（依赖方向由消费者声明，见 `Core/FrameGraph.h`），所以生产者根本不知道自己的消费者"
      "是谁、是哪一帧的实例。于是发射时**只入队结构化记录**（条的身份 + 它声明的边 + 帧偏移），"
      "`TraceFlush` 里由 trace 自己**join**：按 `(owner, stage, BEGIN/END)` 分组、组内按 ts 排序，"
      "**组内序号就是帧实例号**，`FrameOffset` 一加就落到那一帧的条上，两端写同一个 id。\n"
      "流经的**名字在发射时就拷进记录**（不存指针）：名字是模块里的静态数据，而 join 发生在跑完之后，"
      "那时那个模块未必还映射着（实测一次 45% 的待 join 行指向已读不到的内存，join 死在 `strlen` 上）。"
      "行 id 同样在发射时解析，理由一样")
D.Row("原生画的是**换行的声明边**（含同帧、含反向）",
      "`off = 0` 的跨 stage 依赖（例如渲染 tick 等平台的消息泵）同样是真实依赖，照画；"
      "`BlockOn` 那类**反向声明**画成「我的 END → 被放行者的 BEGIN」（声明方即生产者）。"
      "**两端在同一行的边不画**（同一 lane + 同一 owner）：那是条对自己那一行说话，画出来是个闭环、"
      "什么也没说 —— `WaitFor<自己>` 那种**自身链**只是它的退化情形。**也不画**帧内链那类**结构**边，"
      "以及本帧跨到自己的**自身**边 —— 它们不是声明出来的东西")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("LogApi.h", "`MAHO_LOG_API` —— 类与自由函数住在 Log 插件 DLL 里（行状态/深度是 `.cpp` 拥有的 "
      "`thread_local`，定义只能在那边）")
D.Row("Core/FrameGraph.h", "`FFrameExtension` / `FEdge`：箭头来自声明层")
D.Row("Core/TypeList.h", "`TTypeList` / `TFrameStageList`：帧内链的前驱是**编译期**推出来的")
D.Row("cstddef / cstdint / string_view / type_traits / typeindex / typeinfo", "名称切片、时间戳、"
      "`typeid` 比较 —— 不引第三方库")

D.Card("编译期辅助 —— 帧内链的前驱")
D.Table("名字", "说明")
D.Row("TFrameStageList<TFrame>", "取出帧 `IPipeline<TStages...>` 里那份 stage 列表；没有列表的类型"
      "（宿主的生命周期 stage）得到空列表 —— 「没有前驱」，而不是编译不过")
D.Row("StagePredecessorOf(Frame, Stage)", "**编译期**算出调度器在 `Stage` 之前跑的那个 stage，"
      "`MAHO_TRACE_STAGE` 把它交给 `FStageTrace::PrevStage`。比较用的是 `type_info::operator==`"
      "（比**名字**不比地址：宏与帧的派发可能在不同模块，同一个类型每个模块各有一份 `type_info`）")

D.Card("宏 —— 三种形状，同一条 bar")
D.Table("宏", "说明")
D.Row("MAHO_TRACE_STAGE(StageType, Label, Tip)", "**stage body 的第一条语句**（这根条就是帧图那个节点的"
      "条，必须盖住整个 body）。它**建立行**：lane = 帧**类型**的 `TraceGroupName()`、owner = 它的 "
      "`StaticName()`、stage = `typeid(StageType)` 的短名；关闭时写一根条，再按它声明的边写箭头。"
      "`this` 必须是帧（配合 `MAHO_DECLARE_FRAME` / `MAHO_DECLARE_FRAME_UNDER`，见 `Engine/Frame.h`）")
D.Row("MAHO_TRACE_SECTION(Label, Tip)", "**stage 里手写的段落**（stage 调用的任何 helper 里也行）。"
      "它**什么都不建立**：继承环境行，所以落在正在跑的那个帧上、嵌在那根 stage 条里面；另带 "
      "`func=__FUNCTION__`，于是悬停仍然说得清这段埋点住在哪。条名是手写标签 —— 同一个函数里的多个"
      "埋点因此各有各的名字，而不叫同一个 `__FUNCTION__`")
D.Row("MAHO_TRACE_SCOPE(Group, Tip)", "**自带一条 lane 的 scope**：常驻线程的任务体，或没有帧驱动的"
      "自由代码。`Group` 同时是 lane 与 owner（`FThreadedServer` 角色传 `GetThreadName()`），于是"
      "常驻线程在时间线上就是「一个概念、一行」；条名取自 `__FUNCTION__`（所以要埋进**具名函数**里，"
      "别埋进提交用的 lambda，否则条名会读成 `<lambda_1>::operator()`）。\n"
      "这份也进**原生**：`lane = owner = Group` ⇒ 每个常驻角色（`RHIServer` / `ShaderCompiler` / "
      "`ResourceServer` …）在自己的 process 下**只有一条泳道**；它**不带箭头**（scope 没有声明表），"
      "也**不镜像进 `ThreadPool` 组** —— 那一组是图节点的 worker，常驻线程不是池子")
D.Row("MAHO_TRACE_CONCAT(A, B)", "用 `__LINE__` 拼出唯一变量名 —— 于是同一个作用域里也能放多个埋点")

D.Card("导出的接口（MAHO_LOG_API）")
D.Table("签名", "说明")
D.Row("bool TraceEnabled()", "记录是否开启：热路径上的一个 relaxed 原子读（初值来自 `MAHO_TRACE`）")
D.Row("void TraceSetEnabled(bool bEnabled)", "**运行期开关**，由 `r.Trace` 驱动（CVar 属于 "
      "ConsoleVariable 插件，而 Core 不能依赖它，所以 CVar 在插件侧、这里只留函数）。中途打开就从"
      "那一刻开始记录（文件在第一个事件时惰性创建），关掉就停 —— 开关是进程级的，不需要重启")
D.Row("std::uint64_t TraceNowMicros()", "距追踪原点（第一次被追踪的调用）的**单调**微秒数 —— 也就是 "
      "`ts`。原点先取再读钟：顺序反了会让第一个事件的 `ts` 下溢")
D.Row("std::uint64_t TraceTaskFloorMicros()", "`MAHO_TRACE_MIN_US` 的解析结果（只读一次）：stage 条的门槛")
D.Row("void TraceFlush()", "**原生文件在这里才拼包写出**：各线程发射时只入队结构化记录，flush 时"
      "**先 join**（给每条声明的边配到另一端、定出 flow id）、再**按时间戳排序**写出（worker 并发发射，"
      "直写会让时间戳乱序，查看器把迟到的那几个当「DATA LOSSES」丢掉；丢一个 END，那根条会一直画到屏幕"
      "边缘；同 ts 时 BEGIN 在前，否则报 MISPLACED_END_EVENT）。"
      "描述符的时间戳是 0、在发射时直接写，所以它们始终在最前面。进程收尾时调用，也可以手工调来中途快照"
      "（快照会把队列切成两段：跨过切口的箭头缺一根，但**不会画错** —— 序号是窗口内相对算的）")

D.Struct("FStaticName", Desc="**静态字符串的一片** —— 事件里每个名字都是它（字面量、stage 的 "
        "`type_info::name()`、`__FUNCTION__`）。裁命名空间与缩短都是**切片**，从不拷贝：这条路一秒钟"
        "要跑几万次")
D.Field("const char* Data = \"\"", "指向**静态存储**，绝不拥有")
D.Field("std::size_t Size = 0", "长度（所以不靠 `strlen`）")

D.Class("FStageTrace", Desc="`MAHO_TRACE_STAGE` 的 RAII scope —— **唯一知道 stage 身份**的那一种："
        "它属于哪个 stage 接口、哪个帧。它**建立行**：stage body 里发出的一切（嵌套的 section、stage "
        "调用的任何 helper）都归到这一行。关闭时写一根条，再按帧声明的边写箭头。定义在 `.cpp` 里"
        "（行/深度状态是 `.cpp` 拥有的 `thread_local`）；关闭追踪时构造只是拷几个指针，`Start` 保持 0、"
        "析构什么都不做")
D.SetAccess("public")
D.Interface("FStageTrace(const FFrameExtension* InFrame, const std::type_info& InStage, const char* InGroup, "
            "const char* InTrack, const char* InLabel, const char* InTip, const std::type_info* InPrevStage)",
            "构造：记起始时间戳（带嵌套深度偏移）、把当前 `lane/owner/stage` 三者存起来再换成自己的"
            "（lane = 帧类型的分区名，owner = 帧的静态名，stage = 短 stage 名），并**在同一时刻**把该 stage "
            "声明的边**拷成入队的 key**（帧在构造里写完声明、整个 stage 调用期间不变；拷而不是存指针，"
            "因为名字所属的模块到 flush 时未必还在）")
D.Interface("~FStageTrace()", "`Start != 0` 才记录：写条 → 按声明的边写箭头（文本，各自一行）/ "
            "把本 stage 的 BEGIN 记录入队（原生，带它**等待**的边）与 END 记录入队（原生，带它**挡住**的"
            "反向边）→ 把三个字段还原、深度减一。flow id 不在这一层定：它要等两端都在了才由 `TraceFlush` "
            "的 join 定出来。时长以**偏移后的起点**为基准，故短于偏移量的 scope 记 0 而不是回绕")
D.Field("const FFrameExtension* Frame", "开这根条的那个帧（`GetDependencies()` / `GetDependents()` "
        "从这里取）")
D.Field("const std::vector<FFrameExtension::FEdge>* Edges", "**这个 stage** 声明的边；存指针而不是副本")
D.Field("const std::type_info* PrevStage", "调度器在这个 stage 之前跑的那个 stage = 帧内链的**结构边**，"
        "**编译期**从帧自己的 stage 列表推出来（`StagePredecessorOf`）—— 结构边不出现在 "
        "`GetDependencies()` 里，不推就没有这根箭头")
D.Field("const char* PreviousGroup / PreviousTrack / FStaticName PreviousStage",
        "被这根条替换掉的那一行，析构时还回去（一个 worker 接着跑下一个节点，环境行不能从这一帧的 stage "
        "漏到下一帧的）")

D.Class("FScopeTrace", Desc="`MAHO_TRACE_SCOPE` 的 RAII scope —— **自带一条 lane**。给没有帧驱动的代码用："
        "常驻 worker 的任务体，或还没有行的自由函数。`Group` 同时是 lane 与 owner（`FThreadedServer` "
        "角色传 `GetThreadName()`），所以常驻线程在时间线上是「一个概念、一行」。它活着期间**建立**这一"
        "行（嵌套的 scope 也落在它上面），退出时还原")
D.SetAccess("public")
D.Interface("FScopeTrace(const char* InGroup, const char* InTip, const char* InFunc)",
            "构造：记住起始时间戳（同样带深度偏移）并把行切成 `Group`；`InTip` 可选，`InFunc` 由宏用 "
            "`__FUNCTION__` 给出")
D.Interface("~FScopeTrace()", "`Start != 0` 才记录：写一根 lane = owner = `Group`、名字取自函数的条"
            "（**手写埋点永不被门槛过滤**），还原上一行、深度减一")
D.Field("const char* Group / Tip / Func", "行与条名；加上还原用的 Previous* 三件")

D.Class("FSectionTrace", Desc="`MAHO_TRACE_SECTION` 的 RAII scope —— stage **里面**手写的一段。它"
        "**什么都不建立**：捕获包住它的 stage（或 scope）留下的那一行，于是落在同一个行上、嵌套在它的"
        "条里。`Func` 跟着进 `func=`：条是手写标签命名的，悬停仍然要说清这段埋点住在哪")
D.SetAccess("public")
D.Interface("FSectionTrace(const char* InLabel, const char* InTip, const char* InFunc)",
            "构造：把当前的 `Group/Track/Stage` **捕获进来**（即使中途有嵌套 scope 建立了新行，这根条也"
            "不会被记到别人头上），取起始时间戳并加深度")
D.Interface("~FSectionTrace()", "`Start != 0` 才记录：写一根条，名字是 `Label`、`func=` 是函数短名、"
            "stage 沿用捕获到的那个（**不还原任何行**，因为它没替换过），深度减一")

# ══════════════════════════════════════════════════════════════════════════════
# Source/Public/Engine/Engine.h —— 引擎层：stage 接口 + 宿主基类
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Engine/Engine.h", Title="Engine.h —— 引擎层：10 个 stage 接口 + FEngineBase",
         Desc="引擎侧对外的一页：**10 个 stage 能力接口**（一帧的生命周期）、宿主基类 "
              "`FEngineBase`、以及三个 stage 序列别名。本文件还给出每个引擎 stage 的 "
              "`Invoke<Stage, FEngineBase>` 全特化（`MAHO_DECLARE_STAGE_DISPATCH`）—— "
              "「stage → 方法」的映射就落在这里。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Assembly.h", "`ApplyModuleExtension`：`MAHO_DECLARE_ENGINE` 的 `GetModulePath()` 用")
D.Row("Core/Interface.h", "`IPipeline`：frame 挂载 stage 序列")
D.Row("Engine/Query.h", "`FQuery` 的 `Select<...>()`：按 stage 接口筛帧集")
D.Row("Engine/Frame.h", "`FFrameExtension` / `Invoke` / `TFrameDispatch`")
D.Row("Engine/FrameBuilder.h", "`FFrameBuilder` —— 引擎宿主的基类")
D.Row("algorithm / atomic / map / memory / queue / set / string / vector", "宿主自身的状态与命令行存储")

D.Card("宏与别名")
D.Table("签名", "说明")
D.Row("MAHO_DECLARE_ENGINE(EngineType)",
      "引擎类里生成：`CreateEngine()` 工厂 / `GetModulePath()`（DLL 名 = 类型名 + 平台后缀）/ "
      "`StaticName()` / `GetName()`。入口 `EntryPoint` 就是按 `CreateEngine` 这个符号名找引擎的")
D.Row("FInitStages = TTypeList<IPreInit, IInit, IPostInit>", "一次性 **init** 批次的 stage 序列")
D.Row("FTickStages = TTypeList<IBeginFrame, ITick, IEndFrame, IExit>", "帧循环的 stage 序列")
D.Row("FShutdownStages = TTypeList<IPreShutdown, IShutdown, IPostShutdown>", "一次性 **shutdown** 批次的 stage 序列")

D.Card("10 个 stage 能力接口")
D.Table("接口", "方法", "何时跑")
D.Row("IPreInit", "PreInitialize(FEngineBase&)", "init 批：最早")
D.Row("IInit", "Initialize(FEngineBase&)", "init 批：主体")
D.Row("IPostInit", "PostInitialize(FEngineBase&)", "init 批：最晚")
D.Row("IPreShutdown", "PreShutdown(FEngineBase&)", "shutdown 批：最早")
D.Row("IShutdown", "Shutdown(FEngineBase&)", "shutdown 批：主体")
D.Row("IPostShutdown", "PostShutdown(FEngineBase&)", "shutdown 批：最晚")
D.Row("IBeginFrame", "BeginFrame(FEngineBase&)", "每帧：开帧（换缓冲 / 取输入前的准备）")
D.Row("ITick", "Tick(FEngineBase&)", "每帧：主体")
D.Row("IEndFrame", "EndFrame(FEngineBase&)", "每帧：收帧")
D.Row("IExit", "RequestExit(FEngineBase&)", "每帧：退出闸门（调 `RequestExit()` 即关闸）")

D.Class("FEngineBase", Base="FFrameBuilder<FEngineBase>",
        Desc="宿主基类。继承 `FFrameBuilder<FEngineBase>`（收集器 + 帧循环），把生命周期拆成两个"
             "钩子 + 一个纯调度主循环：`PreMain`（安装 + 初始化）→ `Main`（`while (!ShouldExit())`："
             "`FlushPendingUpdates` + `Execute<FTickStages>`，末尾 `Wait`）→ `PostMain`（关闸门 → "
             "扫尾卸载 → 排干）。入口 `EntryPoint` 只认识这个锚，不认识具体引擎类型。")
D.SetAccess("public")
D.Interface("virtual void ParseCommandLine(int Argc, char** Argv)",
            "把 `-key` / `-key value` / `--key=value` 归一化后交给 CLI11 解析，结果进 KV 表")
D.Interface("virtual void PreMain() = 0", "纯虚：装载（`FPluginManager::Load` → `InstallChildrenOf`）并驱动一次 init 批次")
D.Interface("virtual int Main()", "主循环：`while (!ShouldExit()) { FlushPendingUpdates<Init,Shutdown>(); Execute<Tick>(); }` → `Wait()`。返回退出码")
D.Interface("virtual void PostMain()", "收摊：关闸门 → 丢弃未生效装载 → `UninstallAll` → 循环 flush 到静 → `Wait()` → 残留报出")
D.Interface("[[nodiscard]] bool Has(std::string_view Key) const", "命令行里有这个键吗（带不带值都算）")
D.Interface("[[nodiscard]] std::string Get(std::string_view Key) const", "取键的值；不存在为空串")
D.Interface("[[nodiscard]] bool GetBool(std::string_view Key) const", "按布尔解释（true/1/yes/on）")
D.Interface("[[nodiscard]] int GetInt(std::string_view Key) const", "按整数解释；缺失 / 不可解析取 0 或回退值")
D.Interface("[[nodiscard]] const std::map<std::string, std::string>& GetAll() const", "全部 KV 对（只读）")
D.Interface("void RequestExit()", "请求主循环在本帧边界退出（幂等；同时让 Install/Reload 开始拒绝）")
D.Interface("[[nodiscard]] bool ShouldExit() const noexcept",
            "是否已请求退出 —— 宿主对收集器「关闭」状态的词汇（`IsClosing()`）")
D.SetAccess("private")
D.Field("std::map<std::string, std::string> Store", "命令行解析结果的 KV 表")

# ══════════════════════════════════════════════════════════════════════════════
# Source/Public/Engine/Frame.h —— 引擎侧 stage 机器
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Engine/Frame.h", Title="Frame.h —— 引擎侧 stage 机器",
         Desc="Core 的 `FFrameExtension` 是**声明层**（名字 + 它声明的边），它刻意不知道 stage 序列；"
              "这一页就是另一半：`MAHO_DECLARE_FRAME`（身份 + DLL 工厂）、`Invoke`（stage 分派协议）、"
              "以及 `TFrameDispatch`（**唯一**知道 stage 列表的地方）。\n"
              "于是「一个 frame」= `FFrameExtension` + `IPipeline<...>` 两个基类，二者无继承关系。\n"
              "frame 没实现的 stage **根本不产出节点**（桥先问 `TFrameDispatch::Implements`）—— "
              "所以没有空节点、没有 no-op 要调度。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Assembly.h", "`ApplyModuleExtension`：`CreateFrame` 的 `GetModulePath()` 用")
D.Row("Core/FrameGraph.h", "`FFrameExtension` / `FFrameBridge::IDispatch`")
D.Row("Core/Interface.h", "`IPipeline`（有序 stage 序列）")
D.Row("Core/TypeList.h", "`StageIndicesOf`：把 `TStages` 摊成运行期 `type_index` 数组")
D.Row("functional / string / string_view / typeindex / type_traits", "闭包、名字、阶段索引")

D.Card("宏 / 类型")
D.Table("签名", "说明")
D.Row("MAHO_DECLARE_FRAME(FrameType)",
      "frame 类里生成：`StaticName()`（`#FrameType` 字面量 ⇒ 静态存储 ⇒ `FTaskKey::Name` 安全）/ "
      "`GetName()` / `CreateFrame()`（DLL 工厂）/ `GetModulePath()`。名字同时决定 DLL 名 = 类型名 + 平台后缀")
D.Row("MAHO_DECLARE_STAGE_DISPATCH(Context, Stage, Cast, Method)",
      "生成 `Invoke<Stage, Context>` 全特化：`dynamic_cast<Cast*>(frame)` 成功才调用 "
      "`Method(Context)` —— 失败即静默跳过（未实现该 stage 是合法状态）")
D.Row("template <TStage, TContext> void Invoke(FFrameExtension*, TContext&)",
      "stage 分派协议：按 (stage, context) 对特化；`TFrameDispatch::MakeClosure` 就是把它包成闭包")
D.Row("struct FEmptyContext", "不需要上下文的 stage 序列用的占位类型")

D.Class("TFrameDispatch<TStages, TContext>", Base="FFrameBridge::IDispatch",
        Desc="桥的策略实现：**代码库里唯一知道 stage 列表的东西**。桥把它当不透明接口用 —— "
             "`Implements` 答「这个 frame 实现该 stage 吗」，`MakeClosure` 给出该 stage 的闭包。\n"
             "派发对象必须**活得比它产出的批次久**（闭包捕获了它和 frame 的引用）；宿主本来的栈"
             "寿命规则已经保证这一点（批次在当前栈帧退出前排空）。")
D.SetAccess("public")
D.Interface("explicit TFrameDispatch(TContext& InContext)", "绑定调度上下文（引用，不拷贝）")
D.Interface("[[nodiscard]] bool Implements(const FFrameExtension&, std::type_index) const override",
            "沿 `TStages` 递归比对 `typeid`，命中则 `dynamic_cast` 判定是否真的实现")
D.Interface("[[nodiscard]] std::function<void()> MakeClosure(FFrameExtension&, std::type_index) override",
            "命中的 stage ⇒ `[&frame, this]{ Invoke<Stage, TContext>(&frame, Context); }`")
D.Interface("[[nodiscard]] static constexpr auto StageIndices() noexcept",
            "把 `TStages` 摊成 `std::array<type_index, N>` —— 直接喂给 `FFrameBridge::Build`")
D.SetAccess("private")
D.Interface("ImplementsImpl / MakeClosureImpl", "沿 `TTypeList` 的递归展开（模板，空表为终点）")
D.Field("TContext& Context", "调度上下文（如 `FEngineBase&` / `FRender&`）")

# ══════════════════════════════════════════════════════════════════════════════
# Source/Public/Engine/FrameBuilder.h —— 帧集合 + 帧循环
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Engine/FrameBuilder.h", Title="FrameBuilder.h —— 帧集合 + 帧循环（宿主词汇）",
         Desc="宿主只认这一页的词汇：装 / 卸 / 重载帧集合、驱动帧循环、收摊排空。\n"
              "类本身**一个**（`FFrameBuilder<TContext>`），另有 `IFrameCollector` 接口（见下）—— "
              "它的存在让装载方能在不知道 `TContext` 的情况下，把父收集器的任务池递给子收集器。\n"
              "类模板的成员天生都是模板，函数体无法 out-of-line（那需要显式实例化，而引擎模块不许 "
              "include 插件头、无法为插件上下文实例化）⇒ 只有**无状态**的辅助函数下沉进 "
              "`Private/Engine/FrameBuilder.cpp`（`Detail::ReportBridgeDiagnostics`、`IFrameCollector` "
              "的虚析构）。\n"
              "**私有持有的三件**：`FFrameGraph`（图）、`FFrameBridge`（桥）、`TFrameDispatch`"
              "（分派）—— 宿主全程看不见它们。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/FrameGraph.h", "`FFrameGraph` / `FFrameBridge` / `FFrameExtension`（私有实现类型）")
D.Row("Core/Assembly.h", "`FAssembly`：模块句柄与符号查找（装载时用）")
D.Row("Core/Delegate.h", "三个广播事件（`OnFramesChanged` / `OnFrameStatus` / `OnClosing`）")
D.Row("Core/ThreadPool.h", "`FThreadPool`：任务池；收集器**按 lane** 共享它（见 `OwnedPool` / `Pool` / `Lane`）")
D.Row("Core/Fatal.h / Core/TypeList.h", "上报路径；`StageIndicesOf`")
D.Row("Engine/Frame.h", "`TFrameDispatch`（驱动层用）")
D.Row("Engine/Query.h", "`FQuery`：`GetQueryData()` 的数据源是 `Pipelines`")
D.Row("algorithm / map / memory / set / string / string_view / typeindex / vector", "集合状态与并行槽位向量")

D.Card("12 个终态（EFrameStatus）")
D.Table("状态", "含义")
D.Row("InstallQueued / Installed / InstallCancelled", "已入队（下个安全点跑 Init）/ Init 跑完已激活 / 先收到卸载请求，装载被取消（模块已释放、从未初始化）")
D.Row("InstallRefused / InstallCompileFailed", "未接受（关闭中 / 加载失败 / 无工厂符号 / 工厂抛异常 / 重名）；init 批被**拒绝并释放**（实例 + 模块）")
D.Row("UninstallQueued / Uninstalled / UninstallNotFound", "已记录 / Shutdown 跑完且实例与模块已释放 / 查询没命中任何活动帧")
D.Row("UninstallRefused / UninstallCompileFailed", "仍被依赖（Detail 列出依赖者）/ shutdown 批被拒 ⇒ 这些帧**保持存活**（绝不跳过 teardown 就析构）")
D.Row("ReloadQueued / ReloadRefused", "卸载 + 重新装载已入队 / 被拒（无模块路径 / 仍被依赖 / 不存在）")

D.Class("FFrameBuilderBase", Base="FQuery<FFrameExtension>",
        Desc="**集合本身**：与 `TContext` 无关，实现在 `Private/Engine/FrameBuilder.cpp`（类带 "
             "`MAHO_API` —— 宿主是它的子类）。拥有全部状态，并实现「与调度上下文无关」的那部分逻辑。")
D.SetAccess("public")
D.Interface("virtual ~FFrameBuilderBase()", "析构先 `Wait()`（图 + 本 lane 的池屏障），再用例析构：Graph → Features（跑插件析构）→ Pool（join worker）→ Modules（释放 DLL）。**Features 必须早于 Pool**：收集器子实例在这里被销毁，而它的析构要 flush —— 并归还自己那条 lane —— 父收集器共享的那个池，池必须还活着")
D.Interface("[[nodiscard]] FFrameStats GetStats() const", "当前规模（工具 / 面板查询用，勿每帧轮询）")
D.Field("TMulticastEvent<void()> OnFramesChanged", "活动帧集在安全点变化时广播")
D.Field("TMulticastEvent<void(const FFrameStatusInfo&)> OnFrameStatus", "**每个终态**都广播（12 态，无静默路径）")
D.Field("TMulticastEvent<void()> OnClosing", "翻到「关闭」时广播一次（teardown 之前，帧都还活着）")
D.Field("enum class EFrameStatus / struct FFrameStatusInfo / struct FFrameStats", "嵌套类型（见上面的状态表）")
D.SetAccess("protected")
D.Interface("void Wait()", "收摊静默：图的栅栏 + 池屏障。**循环不每帧调**（帧要流水）")
D.Interface("[[nodiscard]] std::size_t UninstallAll()", "给每个活动帧发卸载请求（按名字；名字比模块活得久）")
D.Interface("void ReleaseAll()", "收摊最后手段：**不跑 teardown 阶段**硬释放，但仍保持「在 Pipelines 即活着」不变量")
D.Interface("void DropPendingInstalls() / void CancelPendingReloads()", "丢弃未生效的装载 / 重载")
D.Interface("void CloseForLoads()", "翻到关闭（幂等）：之后 Install / Reload 一律拒绝")
D.Interface("void ReportDiagnostics(const std::vector<FFrameBridge::FDiagnostic>&)",
            "报出桥解不出的声明（**非致命**：悬空的边本来就该消失；这里只报「写错了」）")
D.Interface("void RequestUninstall(FFrameExtension*) / void DeleteUnloaded(FFrameExtension*)",
            "入卸载请求 / 释放实例 + 模块（连带清名字 / 路径 / 句柄槽）")
D.Interface("void RebuildReverseDeps()", "重建反向依赖计数（名字 → 被依赖次数），卸载的最小堆贪心用")
D.Interface("void EmitStatus(EFrameStatus, Name, Path, Detail = {})", "广播一个终态（负载里的字符串全是**拷贝**）")
D.Interface("[[nodiscard]] std::string_view StoredName(const FFrameExtension*) const",
            "装载时拷下的名字 ⇒ **不调用对方模块**就能命名它（槽位向量比模块活得久）")
D.Interface("[[nodiscard]] std::size_t SlotOf(const FFrameExtension*) const", "实例在并行槽位里的下标（`NPos` = 未知）")
D.Interface("[[nodiscard]] bool HasLayerName(std::string_view) / bool IsOwned(const FFrameExtension*) const",
            "某名字是否活动 / 待装载（O(log n)）；该实例是否**仍归收集器所有**（唯一存活性判据 —— "
            "卸载是「先移出 Pipelines 再释放」，所以外部拿到的指针可以查而不是盲解引用）")
D.Interface("[[nodiscard]] std::string ModulePathOf(const FFrameExtension*) const", "装载它的 DLL 路径（同样不碰对方模块）")
D.Interface("[[nodiscard]] bool HasPendingUpdates() const / bool IsClosing() const noexcept", "有没有待应用的变更 / 是否已关闸")
D.Interface("FFrameGraph& GetGraph()", "唯一那张图（帧循环与一次性批次共用；靠「批次前后都排空」+「身份按 stage 区分」保证安全）")
D.SetAccess("private")
D.Field("std::vector<FFrameExtension*> Pipelines", "活动帧（不变量：**先移出这里再释放** ⇒ 在表里即活着）")
D.Field("PendingAdded / PendingRemoveRequests / PendingReloads", "待装载 / 待卸载 / 待重载")
D.Field("std::map<std::string, int> ReverseDepCount", "名字 → 被依赖次数（卸载拒绝的依据）")
D.Field("Modules / ModulePaths / LayerNames", "三条并行向量：DLL 句柄保活 / DLL 路径 / 装载时拷下的名字")
D.Field("std::map<std::string, std::size_t> NameToSlot", "名字 → 槽位（O(log n) 的唯一查表）")
D.Field("std::vector<std::unique_ptr<FFrameExtension>> Features", "实例所有权（析构顺序的关键一环）")
D.Field("FThreadPool OwnedPool / FThreadPool* Pool / FThreadPool::FLane Lane",
        "本收集器驱动节点的池，以及**属于它自己**的那条 lane。`OwnedPool` 是兜底（宿主、或没人递池的"
        "收集器有自己的 worker）；`UseSharedPool` 把 `Pool` 指向**父**收集器那个池并取一条 lane，"
        "于是嵌套收集器（宿主 → FRender → FExampleEditor）全落在最外层那个 worker 集上，同时各自"
        "保有独立栅栏。**这是 lane 存在的全部理由**：引擎需要的隔离是**每收集器**的，而它从前是用"
        "「每收集器一套线程」买的（4 × 核数条线程，去跑约 2 个并发节点）。\n"
        "`Features` **刻意声明在 `Pool` 之后**：成员按声明逆序销毁，而收集器子实例正是在那里被销毁，"
        "它的析构要 flush 父收集器共享的池 —— 顺序写反就是收摊期的一次 use-after-free")
D.Field("std::unique_ptr<FFrameGraph> Graph", "那张图（声明在 Pool 之后 ⇒ 先于 Pool 析构）")
D.Field("LoopFrames / LoopFrameNumbers / bLoopDirty / bLoopJustExpanded / LastLoopRejectReason",
        "帧集缓存 + 每序列一个生成计数器 + 两个「只报一次」守卫")
D.Field("std::atomic<bool> bClosing", "关闭标志（阶段里可能从任意线程置位，故原子）")
D.Field("bool bFlushing", "flush 重入守卫（RAII 管理）")

D.Class("IFrameCollector", Desc="**本身也是收集器**的帧（它通过自己的 `FFrameBuilder` 驱动一组子帧）—— "
        "即使共用父收集器的池，它也需要**自己的 lane**。\n"
        "为什么是接口而不是基类：装载是泛型的。父收集器经 DLL 工厂建出子实例，全程只把它当 "
        "`FFrameExtension` 看；而收集器子实例必须在它第一次 `Execute` **之前**拿到共享池。向本接口"
        "的一次**旁转**（side cast，两者都是那个具体收集器的基类）就是这次交接，而且它是**唯一**"
        "需要知道「哪些帧是收集器」的地方。\n"
        "它把一条设计规则固化下来：收集器的 stage body 由**父**收集器的图派发，所以跑在**父**的 lane 上，"
        "而它的 `Wait()` 排空的是**自己**的 lane —— 于是「调用者那颗任务不在它等的计数里」是构造上成立"
        "的。那正是「从池任务内部调用栅栏」所必需的性质，也是从前「每收集器一个池」用重复线程买来的东西。")
D.SetAccess("public")
D.Interface("virtual ~IFrameCollector()",
            "**out-of-line 定义在 `Source/Private/Engine/FrameBuilder.cpp`**（与 `FThreadedServer` 的虚函数"
            "同理）：本接口是插件侧对象的基类，把首个 out-of-line 虚函数留在引擎自己的模块里就有了"
            "**key function** —— 一份 vtable / 一份 deleting dtor 在 Maho.dll，而不是每个模块一份 COMDAT 副本；"
            "这也让装载方的 `dynamic_cast` 在整个 DLL 边界上比对到**同一条 RTTI 记录**")
D.Interface("virtual void UseSharedPool(FThreadPool& InPool) = 0",
            "把给定池当作本收集器的任务池，并在其中取一条**属于自己**的 lane。由装载它的父收集器调用；"
            "**绝不在第一次 `Execute` 之后调用**（持池引用的图还不存在，而这正是放在这个时机的原因）")

D.Class("FFrameBuilder<TContext>", Base="FFrameBuilderBase, IFrameCollector",
        Desc="**驱动层**：只保留真正需要 `TContext` 或 stage 列表的东西 —— 三个 stage 分派入口、"
             "LINQ 帧集展开、以及安装/重载/卸载入口。")
D.SetAccess("protected")
D.Interface("template <typename T> bool Install()", "按类型装载：`Install(T::GetModulePath())`")
D.Interface("bool Install(std::string_view DllPath, const char* FactorySymbol = \"CreateFrame\")",
            "按路径装载（下个安全点生效）：查符号 → 建实例 → 名字查重 → 入 pending。关闭中 / 重名一律拒绝并上报。"
            "**实例被接受之后、入 pending 之前**做一次 `dynamic_cast<IFrameCollector*>`：是收集器就 "
            "`UseSharedPool(*Pool)` —— 装载是泛型的，这是唯一需要问「你是不是也是收集器」的地方；"
            "两端都是本类，所以谁装收集器就把自己拿到的池继续往下递，最外层 builder 的 worker 一路用到最里")
D.Interface("void UseSharedPool(FThreadPool& InPool) override",
            "`IFrameCollector` 的实现：把 `Pool` 指向父收集器的池（`OwnedPool` 就此闲置 —— 池是惰性的，"
            "所以留着这个成员不会多起线程），并 `CreateLane()` 取一条自己的 lane")
D.Interface("void Reload(std::string_view Name)", "热重载：下个安全点卸旧（依赖安全）、随后装回同 DLL 新副本")
D.Interface("void TryUninstall(std::string_view Query)", "按**名字或 DLL 路径**匹配第一命中，命中即入卸载 pending")
D.Interface("template <TInitStages, TShutdownStages> void FlushPendingUpdates()",
            "应用挂起变更（**函数内部就是安全点**）：先处理「装载被同窗口的卸载取消」→ init 批 → 卸载批。"
            "重入被拒；插件异常被捕获上报、pending 保持原样等下次重试")
D.Interface("template <TLoopStages> void Execute()",
            "**跑一帧**：按 `TLoopStages` 重查帧集（脏时）→ 建批 → 提交。**不排空** ⇒ 帧会流水")
D.Interface("FlushPendingUpdatesImpl / FlushUnload<TShutdownStages>",
            "上两者的实现：建批（`TFrameDispatch` + `FFrameBridge::Build`）→ `Submit` → 排空 → 收割")
D.Interface("template <TStages> void ExpandLoopFrames(TTypeList<TStages...>)",
            "LINQ：`Select<TStages...>()` 重查帧集（脏标志由集合侧自己置位，宿主没有缓存要失效）")
D.Interface("TContext& GetContext()", "把 `this` 取回 `TContext&`（宿主就是上下文）")
D.Interface("template <TEvent, TArgs...> void BroadcastIsolated(TEvent&, const TArgs&...)",
            "隔离广播：订阅者抛异常只报错，不影响收集器状态与 teardown")

# ══════════════════════════════════════════════════════════════════════════════
# Source/Public/Engine/Query.h —— 编译期 / 运行期筛选
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Engine/Query.h", Title="Query.h —— 编译期类型筛选 + 运行期实例筛选",
         Desc="两套筛选，都是纯查询、不改任何东西：\n"
              "**`TQuery<TList>`** —— 纯类型运算（不引用实例）：`Select`(OR) / `With`(AND) / "
              "`Not`(NOR) → `FResult`。用来在编译期把一串 stage 接口折成帧集类型。\n"
              "**`FQuery<TBase>` / `FQueryResult<TBase>`** —— 运行期按接口谓词筛实例"
              "（`dynamic_cast`），结果**可当 `vector` 用但不能直接 range-for**（先赋给 vector）。"
              "结果内部向量私有（外部注入不了裸指针），并记住来源集合做 Debug 活性审计："
              "指针已不在来源里（帧已卸载）就丢弃并报错 —— 因为卸载是「先移出集合再释放模块」，"
              "对已卸载对象做 `dynamic_cast` 会解引用悬垂 vptr。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/TypeList.h", "`TQuery` 的全部类型运算基于 `TTypeList`")
D.Row("algorithm / typeindex / vector", "运行期筛选与结果容器")

D.Struct("TQuery<TList>", Desc="**编译期**筛选：只操作类型列表，零运行期开销。")
D.Interface("Select<...>() / With<...>() / Not<...>()", "按 OR / AND / NOR 过滤元素")
D.Field("using FResult = ...", "筛完的类型列表（交给上层做容器／遍历）")

D.Class("FQuery<TBase>", Desc="**运行期**筛选的基类：派生类给出数据源（`GetQueryData()`），"
        "本类提供按接口谓词筛选与 Debug 活性审计。")
D.Interface("virtual std::vector<TBase*>& GetQueryData()", "数据源（收集器里就是 `Pipelines`）")
D.Interface("[[nodiscard]] FQueryResult<TBase> Select<...>() const",
            "按 stage 接口筛：命中任一接口即入选（`dynamic_cast` 判定）")
D.Interface("[[nodiscard]] FQueryResult<TBase> With<...>() / Not<...>() const", "AND / NOR 变体")
D.Field("std::vector<TBase*> Source", "来源集合（私有；审计时用来判断指针是否已失效）")

D.Class("FQueryResult<TBase>", Desc="筛选结果：**隐式转换为 `std::vector<TBase*>`**（赋给 vector 即可用），"
        "但它本身**不能直接 `range-for`**（`begin/end` 找不到）—— 这一点是实测：在 `Engine.cpp` 上"
        "直接范围 for 会被编译器顶回来。结果内部向量私有，外部注入不了裸指针。")
D.Interface("std::vector<TBase*> Data", "结果容器（public：只读使用；外部无法注入）")

# ══════════════════════════════════════════════════════════════════════════════
# Source/Public/Engine/PluginManager.h —— 运行期插件安装树
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Engine/PluginManager.h", Title="PluginManager.h —— 运行期插件安装树",
         Desc="运行期的插件**安装树**，来自 `PluginManager.json`（codegen 的 EntryPoint POST_BUILD "
              "把它放到二进制旁边）。清单是一棵「层类型」树：**工程是根节点**，每个节点的 children "
              "就是它装进**自己的收集器**的那些插件。\n"
              "一次查询对所有人都够用，而且每个节点本来就认识自己的名字 ⇒ 宿主与收集器跑**同一句**"
              "调用：`InstallChildrenOf(GetName())`。反方向不携带任何名字：目录只存树和引擎根，"
              "不存「谁是谁」。\n"
              "**进程唯一**：访问器在这里声明、在引擎 DLL（`Maho`）里定义 ⇒ 任何链接 Maho 的插件 DLL "
              "共享同一个实例。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_API` —— 类跨 DLL（进程唯一实例在 Maho 里）")
D.Row("filesystem", "`std::filesystem::path`：`ExecutableDir()` 的返回类型")
D.Row("map", "节点 → 直接子节点（清单顺序）")
D.Row("string / string_view", "名字与路径；查询入参用 view，不拷贝")

D.Class("FPluginManager", Desc="插件安装树的持有者（进程唯一）。构造 / 析构 / 拷贝都是私有的 —— "
        "只能经 `Get()` 拿到那一个实例。")
D.SetAccess("public")
D.Interface("static FPluginManager& Get()", "进程唯一实例（首次访问时惰性装载）")
D.Interface("bool Load()",
            "装载（或重载）`PluginManager.json`：先找当前工作目录、再找可执行文件目录。缺失 / "
            "不可解析返回 `false` —— 由调用方走回退策略")
D.Interface("[[nodiscard]] bool IsLoaded() const", "是否已经拿到可用的目录")
D.Interface("[[nodiscard]] const std::vector<std::string>& GetChildren(std::string_view LayerName) const",
            "某节点的**直接**子层类型，按清单顺序 —— 宿主与每个收集器共用的**唯一**查询，各自传自己的"
            "名字。没有子节点返回空（那是正常情况，不是错误）")
D.Interface("[[nodiscard]] const std::string& GetEngineRoot() const",
            "codegen 记下的引擎源码根（绝对路径，生成器用的 posix 形式）。缺失为空 ⇒ 调用方自行探测。"
            "**在 C++ 里它永远不是编译期常量**：清单是运行期数据")
D.Interface("[[nodiscard]] static std::filesystem::path ExecutableDir()",
            "可执行文件所在目录（各平台的规范查询），失败为空。所有运行期路径探测（目录查找、虚拟根）"
            "都共用它")
D.SetAccess("private")
D.Field("bool bLoaded = false", "是否已成功装载过")
D.Field("std::string EngineRoot", "引擎源码根（来自清单）")
D.Field("std::map<std::string, std::vector<std::string>> Children", "节点 → 它的直接子节点")

# ══════════════════════════════════════════════════════════════════════════════
# 下面继续按你的口述追加：再 Header(...) 换一个头，Class/Interface/Field 往下挂。


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
