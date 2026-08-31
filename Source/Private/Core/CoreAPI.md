# Core（Private）— 实现算法字典

cpp 侧每个函数的算法伪代码解释。Public 侧 API 文档通过 `#fn-...` 锚点跳转落到这里。

## Assembly.cpp

`FAssembly` 现在是 **header-only**（`unique_ptr<void, FModuleDeleter>` 持有句柄，Load/Unload/GetProc 全内联）。此文件保持为空——CMake glob 收录它，未来平台相关实现可移回这里。

## Fatal.cpp

<a id="fn-fatal-install"></a>
### InstallFatalHandlers()

← [公开 API](../../Public/Core/CoreAPI.md) · `void`

安装 `std::terminate` 兜底，进程入口最先调。互斥保护 + 幂等标记，重复调用 no-op。

```text
InstallFatalHandlers():
1. lock(GFatalMutex)
2. if bHandlersInstalled: return      // 幂等
3. std::set_terminate(TerminateHandler)
4. bHandlersInstalled = true
```

<a id="fn-fatal-report"></a>
### ReportFatal(const char* Message)

← [公开 API](../../Public/Core/CoreAPI.md) · `[[noreturn]] void`

致命路径终点：输出到 stderr + 落盘 `Saved/Logs/Fatal.log` + abort。递归调用检测防死循环。

```text
ReportFatal(Message):
1. Windows: SetConsoleOutputCP(CP_UTF8)   // 控制台切 UTF-8，无控制台时 no-op
2. lock(GFatalMutex):
     if bInsideReportFatal:               // 递归致命 → 直接 abort
         fprintf(stderr, "Maho: recursive ReportFatal: %s\n", Message); abort()
     bInsideReportFatal = true
3. Text = Message ?? "(null)"
4. fprintf(stderr, "Maho FATAL: %s\n", Text); fflush(stderr)
5. AppendFatalLogFile(Text)               // 追加到 Saved/Logs/Fatal.log
6. abort()
```

<a id="fn-fatal-error"></a>
### ReportError(const char* Message)

← [公开 API](../../Public/Core/CoreAPI.md) · `void`

非致命错误：stderr + 落盘，**不 abort**。

```text
ReportError(Message):
1. Text = Message ?? "(null)"
2. fprintf(stderr, "Maho ERROR: %s\n", Text); fflush(stderr)
3. AppendFatalLogFile(Text)
```

<a id="fn-fatal-terminate"></a>
### TerminateHandler()（内部）

匿名命名空间，仅被 `std::set_terminate` 引用。

`std::terminate` 触发时提取活跃异常信息，转给 `ReportFatal`：

```text
TerminateHandler():
1. Message = "std::terminate called (no active exception)"
2. if 有活跃异常:
       rethrow + catch(std::exception& E)  → "std::terminate: " + E.what()
       catch(...)                          → "std::terminate: unknown exception"
3. ReportFatal(Message)                    // noreturn
```

<a id="fn-fatal-logfile"></a>
### AppendFatalLogFile(const char* Message)（内部）

匿名命名空间，仅被 ReportFatal/ReportError 引用。

```text
AppendFatalLogFile(Message):
1. create_directories("Saved/Logs")       // 忽略错误码
2. ofstream Out("Saved/Logs/Fatal.log", app)
3. if !Out: return
4. Out << '[' << MakeTimestamp() << "] " << Message << '\n'; Out.flush()
```

## TaskGraph.cpp

<a id="fn-graph-init"></a>
### FTaskGraph::Init(vector<FNode*> Nodes)

← [公开 API](../../Public/Core/CoreAPI.md) · `void`

装载完整节点集（仅拓扑数据）。清空旧任务表，为每个节点分配 index，并建 `(Name, Stage) → index` 查找表。

```text
Init(Nodes):
1. Tasks.clear(); Lookup.clear(); Tasks.reserve(Nodes.size())
2. for Node in Nodes:
     if Node == nullptr: continue
     Index = Tasks.size()
     Tasks.push_back(FTask{ Node })
     Lookup[{Node->Name, Node->Stage}] = Index
```

<a id="fn-graph-compile"></a>
### FTaskGraph::Compile()

← [公开 API](../../Public/Core/CoreAPI.md) · `bool`

接线边 + 校验。对每个节点，把每条依赖解析成"下游 index"记录前向边，并累计 `InitPending`。任何依赖查不到 → 返回 false（缺失依赖）。

```text
Compile():
1. for Task in Tasks: 清 Downstreams / InitPending / Pending
2. for I in [0, Tasks.size()):
     for Dep in Tasks[I].Node->Dependencies:
         It = Lookup.find({Dep.Name, Dep.Stage})
         if It == Lookup.end(): return false     // 缺失依赖
         Tasks[It->second].Downstreams.push_back(I)   // 我是它的下游
         Tasks[I].InitPending += 1
3. Task.Pending = Task.InitPending               // 所有节点
4. return true
```

<a id="fn-graph-reset"></a>
### FTaskGraph::Reset()

← [公开 API](../../Public/Core/CoreAPI.md) · `void`

复用当前已编译图：把每个节点的 `Pending` 重置回 `InitPending`，`Remaining` 重置为节点总数。拓扑不变时主循环可直接 Reset 而非重编译。

```text
Reset():
1. for Task in Tasks: Task.Pending = Task.InitPending
2. Remaining = Tasks.size()
```

<a id="fn-graph-execute"></a>
### FTaskGraph::Execute()

← [公开 API](../../Public/Core/CoreAPI.md) · `void`

Reset 后收集所有零依赖节点作为种子，逐个 SubmitTask（异步）。

```text
Execute():
1. if Tasks.empty(): return
2. Reset()
3. Ready = { I : Tasks[I].Pending == 0 }         // 种子：无依赖节点
4. for Index in Ready: SubmitTask(Index)         // 异步派发
```

<a id="fn-graph-submit"></a>
### FTaskGraph::SubmitTask(size_t Index)

内部——任务执行 + 下游释放。跑在池 worker 上：

```text
SubmitTask(Index):
1. Pool.Submit([this, Index]:
     ExecuteNodeFor(Index)                       // 执行节点（子类协议钩子）
     lock(Mutex):
         for Down in Tasks[Index].Downstreams:   // 释放下游
             if Pending != 0 && --Pending == 0:
                 BecameReady.push_back(Down)
         Remaining -= 1
     for Down in BecameReady: SubmitTask(Down)   // 递归派发就绪节点
   )
```

要点：**释放路径是锁保护的递减 + 就绪发现**；任务提交本身通过 lambda 闭包 `{this, Index}` 构造 `std::function`。

<a id="fn-graph-executenodefor"></a>
### FTaskGraph::ExecuteNodeFor(size_t Index)（内部）

把节点 index 转成 `FTaskGraphNode*` 并交给子类的 `ExecuteNode` 协议钩子。

```text
ExecuteNodeFor(Index):
1. ExecuteNode(Tasks[Index].Node)
```

<a id="fn-graph-flush"></a>
### FTaskGraph::Flush()

← [公开 API](../../Public/Core/CoreAPI.md) · `void`

阻塞到图排空：直接委托线程池 barrier（`Pool.Flush()` 等 `PendingCount == 0`）。

```text
Flush():
1. Pool.Flush()
```

- [CoreDoc.md](CoreDoc.md) — 实现目录 · [公开 API](../../Public/Core/CoreAPI.md) — 签名入口
