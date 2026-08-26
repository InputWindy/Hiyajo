<!-- mahogen -->
# Core

## 代码文件

- [Assembly.h](Assembly.h) — DLL 加载单元（LoadLibrary/FreeLibrary RAII）
- [Core.h](Core.h) — Core 聚合头
- [Export.h](Export.h) — DLL 导出/导入宏（MAHO_API）
- [Extension.h](Extension.h) — MAHO_EXTEND_DEPS（依赖声明锚点）
- [Fatal.h](Fatal.h) — 致命错误报告
- [Interface.h](Interface.h) — 能力接口（IInit/IShutdown/IMain/IExit/IPlugin）
- [Query.h](Query.h) — 类型无关编译期 LINQ（TQuery）
- [Queue.h](Queue.h) — 类型无关 FIFO 命令队列（FQueue）
- [Schedulers.h](Schedulers.h) — 遍历协议 + 并行调度器（FParallelScheduler）
- [Singleton.h](Singleton.h) — CRTP 单例标识基类（TSingleton）
- [ThreadPool.h](ThreadPool.h) — 线程池（FThreadPool）
- [ThreadedServer.h](ThreadedServer.h) — 常驻单线程（FThreadedServer）
- [Topology.h](Topology.h) — 编译期依赖拓扑（TLevels_t / TNodeLevel）
- [TypeList.h](TypeList.h) — 编译期类型表（TTypeList 及操作）
<!-- mahogen end -->

## 概念——类型无关的基建积木

Core 是**零 app 假设、零三方依赖、零 stage 预设**的纯积木。每个组件类型无关（不引用 FLayer/应用概念），可独立使用。

### ① 类型表（TypeList）

`TTypeList<T...>` 是编译期有序类型数组。操作：`TCons`（前插）/ `TAppend`（后插）/ `TContains`（成员）/ `TCatch`（拼接）/ `TUnionList_t`（去重并集）。

### ② 依赖拓扑（Topology）

`MAHO_EXTEND_DEPS`（Extension.h）是**通用依赖声明锚点**——任何类可标记依赖（codegen 扫描它生成 `.gen.h` 宏填 `FDepends`）。`TResolveDependsPack`/`TNodeLevel`/`TLevels_t` 编译期算分层序：

```cpp
class FWorld : public FLayer<> {
    MAHO_EXTEND_DEPS(FWorld, FDefaultSlot, (FNoParent, FAI));  // 声明锚点
    // codegen → World.gen.h: #define MAHO_DEPS_FWorld_FDefaultSlot FAI
    // 宏填 FDepends = TTypeList<FDefaultSlot, TTypeList<FAI>>
};
using FLevels = Topo::TLevels_t<FChildrenList, FDefaultSlot>;  // 分层序
```

### ③ 类型查询（Query）

`TQuery<FList>` 类型无关编译期 LINQ——Select（OR）/ With（AND）/ Not（NOR）链式筛选，输出 `FResult`（TTypeList）：

```cpp
using FTickable = TQuery<FTable>::Select<ITick>::With<IShared>::Not<ITest>::FResult;
```

### ④ 命令队列（Queue）

`FQueue` **类型无关**：存 `unique_ptr<ICommand>`，命令自带 `GetCatalogId()`（uint64）路由到 catalog lane。FIFO，多线程 Enqueue，消费方 Dequeue 后自行执行：

```cpp
FQueue Q;
Q.Enqueue(std::make_unique<FInstallCmd>(...));          // 任意线程
while (auto Cmd = Q.Dequeue(kInstallLane)) { /* apply */ }  // 消费方执行
```

`ICommand` 是纯数据载体（仅 `GetCatalogId()`），无执行协议。

### ⑤ 并行执行（Schedulers + ThreadPool）

`FParallelScheduler`（无模板）——两个泛型 ForEach：变参 callable 包 + 运行时容器（MakeTask 投影）。内部 `FThreadPool` 并行 + barrier 收尾。层间串行 / 层内并行的"分层语义"是调用方的事（FLayer 持有）。

`FThreadedServer`——常驻单线程 + FIFO 任务队列（IO 线程/渲染线程等专用角色）。

### ⑥ 单例（Singleton）

`TSingleton<T>` 是**标识基类**（CRTP），无强制生命周期。子类自声明 `static T& Get();`（定义在 .cpp，进程唯一）。需要生命周期经 `IPlugin<IInit,IShutdown>`（Interface.h）组合。

### ⑦ 能力接口（Interface）

`IInit`（Initialize(int,char**)）/ `IShutdown`（Shutdown()）/ `IMain`（Main()）/ `IExit`（Exit()）/ `IPlugin<Caps...>`（virtual-base 组合器）。能力**可选组合**——不是所有对象都需要生命周期。

### ⑧ 加载与致命错误（Assembly / Fatal）

`FAssembly`——DLL 加载 RAII。`Fatal::ReportFatal`——致命错误（不恢复）。

## 相关文档

- [Core.h](Core.h) — 聚合头
- [CoreAPI.html](CoreAPI.html) — API 文档
- [../Engine/EngineDoc.md](../Engine/EngineDoc.md) — 层架构（FLayerBase 组装以上基建）
- [../../SourceDoc.md](../../SourceDoc.md) — 源码根
