# Core

## Code Files

- [Assembly.h](Assembly.h) — DLL 加载原语（FAssembly）
- [Delegate.h](Delegate.h) — 多播事件（TMulticastEvent）
- [Export.h](Export.h) — DLL 导出/导入宏 + MAHO_IF_NOT_NULL
- [Fatal.h](Fatal.h) — 致命路径 + 断言宏
- [FrameGraph.h](FrameGraph.h) — 帧调度器 `FFrameGraph` + 声明层 `FFrameExtension` + 桥 `FFrameBridge`
- [Interface.h](Interface.h) — 能力组合器 IPlugin / 阶段管线组合器 IPipeline
- [Singleton.h](Singleton.h) — CRTP 单例标识基类 TSingleton
- [ThreadedServer.h](ThreadedServer.h) — 常驻专用线程 FThreadedServer
- [ThreadPool.h](ThreadPool.h) — 固定规模线程池 FThreadPool
- [TypeList.h](TypeList.h) — 编译期类型列表

## Concept -- Type-Agnostic Infrastructure Building Blocks

Core 是一组类型无关的基础设施块，**零 app 假设、零第三方依赖、零 stage 预设**。每个组件不引用 `FFrameExtension`/app 概念，可独立使用。

### 1. Type List (TypeList)

`TTypeList<T...>` 是编译期有序类型数组。运算：`TCons`（前插）/ `TAppend`（后插）/ `TContains`（成员判断）/ `TCatch`（拼接）/ `TUnionList_t`（保序去重并集）。顺序即语义，遍历顺序由调用方决定。

### 2. Delegate (Delegate)

`TMulticastEvent<Signature>` 多播事件（bind + broadcast）。线程安全——任意线程可 bind/broadcast/unbind，Broadcast 在锁外回调处理器（处理器可安全再入）。Header-only、无 DLL 边界，可作插件公共 API 的成员类型。

### 3. Capability Composition (Interface)

`IPlugin<TCapabilities...>` 把能力 trait 作为虚基类安装（可选能力组合）；`IPipeline<TStageTypes...>` 定义有序阶段序列并暴露 `TStages`。stage→方法调用的 `Invoke` 协议由具体调度上下文实现，Core 本身不预设阶段。

### 4. Singleton (Singleton)

`TSingleton<T>` 是**纯身份/标志基类**，无强制生命周期。派生单例自己声明 `static T& Get()` 并在自己的 `.cpp`（编进其 DLL）定义——跨 DLL 进程唯一。

### 5. 帧调度（FrameGraph）

三件东西在同一头文件里，因为强绑定：

- **`FFrameExtension`** = 声明层：名字（字符串字面量）+ 它声明的边。**不知道 stage 序列**。
- **`FFrameBridge`** = 桥：把「frame 集合 + stage 序列 + 帧号」建成一批 `FTask`；解析相对帧偏移；为**实现的** stage 产节点（未实现的不产节点）；产出两类结构边（帧内链 + 每 stage 跨帧自边）；报告绑不上的声明。
- **`FFrameGraph`** = 调度器：节点身份是三元组 `{名字, stage, 相位}`（相位 = 环索引 mod `MAHO_FRAMES_IN_FLIGHT`），只有两个驾驶动词 —— `Submit(batch)`（校验 + **准入**：阻塞调用方直到它点名的相位排空）与 `Wait()`（**提交栅栏**：等"我提交过的都完成"）。

要点：

- **提交即合并**，没有 `Init`/`Compile`/重建；在飞帧不受影响。
- **悬空的边静默消解**（边需要两端），不报告、不计数；诊断责任在桥。
- **调度线程独占全部图状态**（`FFrameGraph : FThreadedServer` 的串行命令队列）⇒ 无图锁，且**绝不阻塞**。
- 完成由**调度器**置位（不是执行体），异常也置位 ⇒ 下游永不被永久挂住。
- **事件是标志表下标**（`Using FEvent = uint32`，`EventOf(Id, Phase) = Id*K + Phase`），不是对象、不分配不回收。
- `MAHO_TRACE_STAGES=1` 打开 stage enter/exit 追踪（硬崩场景下唯一能指名肇事 stage 的手段）。

### 6. Thread Pool (ThreadPool)

`FThreadPool` 固定规模线程池（常驻 worker + FIFO 队列）。`Submit` 入队即返，`Flush` 锁步 barrier（等真正"执行完"而非"出队"）。瞬时并行工作用它。

### 7. Threaded Server (ThreadedServer)

`FThreadedServer` 专用常驻 worker：一个持久线程 + FIFO 串行任务队列。给需要私有常开线程 + 串行命令队列的长期角色用（调度器、渲染线程、IO 加载线程）——不是瞬时并行任务（那种用 FThreadPool）。

### 8. Loading and Fatal Errors (Assembly / Fatal)

`FAssembly` DLL 加载 RAII（LoadLibrary/dlclose + 符号查找，move-only）。`ReportFatal` / `ReportError` / `InstallFatalHandlers` 统一致命/错误路径 + 崩溃兜底；`MAHO_CHECK/VERIFY/ENSURE` 系列断言宏。

## Related Docs

- [CoreAPI.md](CoreAPI.md) — API 文档
- [../Engine/EngineDoc.md](../Engine/EngineDoc.md) — 帧系统（引擎侧）
- [../../SourceDoc.md](../../SourceDoc.md) — 源码根

