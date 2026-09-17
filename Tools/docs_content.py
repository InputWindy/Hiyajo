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

D.Card("接口")
D.Table("函数签名", "说明")
D.Row("std::string ApplyModuleExtension(std::string_view BaseName)",
      "给模块基名补上宿主平台的动态库后缀：Windows `.dll` / Android、Linux `.so` / macOS "
      "`.dylib`；无动态库的运行时（iOS）原样透传。全仓因此不硬编码任何 `.dll` 字符串。"
      "**已导出**：每个插件由 `MAHO_DECLARE_FRAME` 生成的 `GetModulePath()` 都从自己的 DLL 调它。")
D.Row("void FModuleDeleter::operator()(void* Handle) const noexcept",
      "用 `FreeLibrary` / `dlclose` 释放 OS 模块句柄（而不是 `delete`），空句柄直接返回。"
      "**已导出**：谁销毁 `FAssembly`，这段代码就在谁的模块里跑。")
D.Row("bool FAssembly::Load(std::string_view Path)",
      "装载模块（先 `Unload()` 掉旧句柄）；文件缺失或装载失败返回 `false`。")
D.Row("void FAssembly::Unload()",
      "释放句柄；可重复调用。之后 `IsLoaded()` 为假、`GetProcAddress()` 返回 nullptr。")
D.Row("bool FAssembly::IsLoaded() const", "是否持有有效的模块句柄。")
D.Row("void* FAssembly::GetProcAddress(const char* Name) const",
      "原始符号查找（`GetProcAddress` / `dlsym`）；未装载或符号不存在则返回 nullptr。")
D.Row("template <typename TFunction> TFunction GetProcAs(const char* Name) const",
      "把原始符号转成**函数指针**类型再返回 —— 调用方写 `GetProcAs<CreateFn>(\"CreateEngine\")` "
      "即可，无需自己 `reinterpret_cast`。")

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
