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
