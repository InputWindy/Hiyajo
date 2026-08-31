# Core

## Code Files

- [Assembly.h](Assembly.h) — DLL 加载原语（FAssembly）
- [Core.h](Core.h) — Core 聚合头
- [Delegate.h](Delegate.h) — 多播事件（TMulticastEvent）
- [Export.h](Export.h) — DLL 导出/导入宏 + MAHO_IF_NOT_NULL
- [Fatal.h](Fatal.h) — 致命路径 + 断言宏
- [Interface.h](Interface.h) — 能力组合器 IPlugin / 阶段管线组合器 IPipeline
- [Singleton.h](Singleton.h) — CRTP 单例标识基类 TSingleton
- [TaskGraph.h](TaskGraph.h) — 依赖图调度器 FTaskGraph
- [ThreadedServer.h](ThreadedServer.h) — 常驻专用线程 FThreadedServer
- [ThreadPool.h](ThreadPool.h) — 固定规模线程池 FThreadPool
- [TypeList.h](TypeList.h) — 编译期类型列表

## Concept -- Type-Agnostic Infrastructure Building Blocks

Core 是一组类型无关的基础设施块，**零 app 假设、零第三方依赖、零 stage 预设**。每个组件不引用 FLayer/app 概念，可独立使用。

### 1. Type List (TypeList)

`TTypeList<T...>` 是编译期有序类型数组。运算：`TCons`（前插）/ `TAppend`（后插）/ `TContains`（成员判断）/ `TCatch`（拼接）/ `TUnionList_t`（保序去重并集）。顺序即语义，遍历顺序由调用方决定。

### 2. Delegate (Delegate)

`TMulticastEvent<Signature>` 多播事件（bind + broadcast）。非线程安全——在拥有线程 broadcast，跨线程走队列。Header-only、无 DLL 边界，可作插件公共 API 的成员类型。

### 3. Capability Composition (Interface)

`IPlugin<TCapabilities...>` 把能力 trait 作为虚基类安装（可选能力组合）；`IPipeline<TStageTypes...>` 定义有序阶段序列并暴露 `TStages`。stage→方法调用的 Invoke 协议由具体管线类实现，Core 本身不预设阶段。

### 4. Singleton (Singleton)

`TSingleton<T>` 是**纯身份/标志基类**，无强制生命周期。派生单例自己声明 `static T& Get()` 并在自己的 `.cpp`（编进其 DLL）定义——跨 DLL 进程唯一。

### 5. Dependency-Graph Scheduling (TaskGraph)

`FTaskGraph` 依赖图调度器：节点 = (对象, 阶段) 对，边来自依赖元组。节点在全部直接依赖完成后立即就绪释放——**无阶段屏障**。生命周期 Init → Compile → Execute → Flush，执行协议经 `ExecuteNode` 委托给子类。

### 6. Thread Pool (ThreadPool)

`FThreadPool` 固定规模线程池（常驻 worker + FIFO 队列）。`Submit` 入队即返，`Flush` 锁步 barrier（等真正"执行完"而非"出队"）。瞬时并行工作用它。

### 7. Threaded Server (ThreadedServer)

`FThreadedServer` 专用常驻 worker：一个持久线程 + FIFO 串行任务队列。给需要私有常开线程 + 串行命令队列的长期角色用（渲染线程、IO 加载线程）——不是瞬时并行任务（那种用 FThreadPool）。

### 8. Loading and Fatal Errors (Assembly / Fatal)

`FAssembly` DLL 加载 RAII（LoadLibrary/dlclose + 符号查找，move-only）。`ReportFatal` / `ReportError` / `InstallFatalHandlers` 统一致命/错误路径 + 崩溃兜底；`MAHO_CHECK/VERIFY/ENSURE` 系列断言宏。

## Related Docs

- [CoreAPI.md](CoreAPI.md) — API 文档
- [../Engine/EngineDoc.md](../Engine/EngineDoc.md) — 层架构
- [../../SourceDoc.md](../../SourceDoc.md) — 源码根
