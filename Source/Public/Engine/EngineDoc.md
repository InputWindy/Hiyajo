# Engine

## Code Files

- [Engine.h](Engine.h) — 10 个 stage 能力接口 + FEngineBase + MAHO_DECLARE_ENGINE
- [Layer.h](Layer.h) — FLayerBase / FLayer / Invoke 派发 / MAHO_DECLARE_LAYER / MAHO_DECLARE_STAGE_DISPATCH
- [LayerCollector.h](LayerCollector.h) — FLayerCollector（层集合：安装/卸载/依赖安全卸载）
- [LayerTaskGraph.h](LayerTaskGraph.h) — FLayerTaskGraph（层 → 依赖图展开）
- [Query.h](Query.h) — TQuery（编译期类型筛选）/ FQuery / FQueryResult（运行时实例筛选）

## Concept -- Anonymous Layer + Ordered Stage Pipeline

Engine 层把应用拆成一组**单职责匿名层**，每层沿**有序 stage 管线**展开成任务图节点，由依赖图调度器自动编排——有依赖串行、无依赖并行，依赖边即隐式屏障。

### 1. 匿名层（FLayerBase）

层只闭合自己：`GetName()`（稳定身份名 = 拓扑键）+ `GetDependencies()`（逐 stage 依赖表）。**不管理依赖生命周期**——执行上下文完整性由 `FLayerTaskGraph` 保证。层可动态加载（DLL），宿主只持有 `FLayerBase*`。

### 2. 有序阶段管线（IPipeline）

`IPipeline<TStageTypes...>` 定义有序阶段序列（`TStages`）；引擎提供 10 个 stage 能力接口（IPreInit/IInit/IPostInit/IBeginFrame/ITick/IEndFrame/IExit/IPreShutdown/IShutdown/IPostShutdown），stage→方法调用的 `Invoke` 协议按 (stage, context) 对特化。**可选能力组合**：层只继承自己要的阶段接口，没实现的 stage 被 `dynamic_cast` 静默跳过。

### 3. 层展开（FLayerTaskGraph）

每层沿管线展开成**每阶段一个节点**：自推进（阶段 N 依赖同层阶段 N-1）+ 跨对象依赖（层声明的元组）。节点就绪即释放——**无阶段屏障**，跨阶段管线（A 的 Tick 依赖 B 的 BeginFrame）天然成立。

### 4. 依赖声明（AddDependency）

编译期模板（同 DLL 内点名类型）或运行时字符串寻址（跨 DLL 用层名）：

```cpp
AddDependency<ITick, FLog, IBeginFrame>();                              // 编译期
AddDependency(std::type_index(typeid(ITick)), "FLog", std::type_index(typeid(IBeginFrame)));  // 运行时
```

### 5. 层集合（FLayerCollector）

`Install` / `TryUninstall` 记录进 pending 集，在每帧安全点 `FlushPendingUpdatePipelines` 统一应用（先 Init 后 Shutdown）。卸载用**反向依赖计数最小堆贪心**：被依赖的层拒绝卸载，依赖者先弹出、链式卸载。

## Engine Main Loop

`FEngineBase::Main()` 驱动一个 `FLayerTaskGraph`：

```text
Init 图（IPreInit→IInit→IPostInit，一次）→ 编译 → 执行 → 排空
Tick 循环（每帧）：
  Flush（等上一帧前台排空）→ FlushPendingUpdatePipelines（应用安装/卸载）
  → 若 OnLayersChanged 触发（bLayersDirty）：重建 Tick 图（IBeginFrame→ITick→IEndFrame→IExit）+ 编译
  → Reset + 异步执行（层集不变时跳过重建/重编译/重过滤）
  → 检查 RequestExit 标志
Shutdown 图（IPreShutdown→IShutdown→IPostShutdown，一次）→ 释放实例 + DLL
```

**缓存**：Tick 图由 `FLayerCollector::OnLayersChanged` **广播委托驱动**（push，非轮询）——安装/卸载/重载在安全点应用后广播，`FEngineBase::Main` 绑定它置 `bLayersDirty` 才重建。层集不变的普通帧只 `Reset + Execute`，不再每帧重建节点、重新连线、重新 `dynamic_cast` 过滤。

**失败隔离**（坏插件不杀宿主）：
- 编译失败（缺失依赖 / 依赖环）→ `ReportError`（每种破损**报告一次**，不刷屏）+ 坏层暂不调度（空图停帧），拓扑修复后自动重试。
- 安装失败 → `ReportError`，不再静默；且**失败传播**——重名、DLL 加载失败、或依赖的层未先装，该层安装被拒，它的依赖者也随之装不上（与卸载的"被依赖则拒绝"对称）。
- 节点/worker 抛异常 → `ReportError`（非致命），并**仍释放下游**，图不挂起。

**热重载**：`Reload("LayerName")` 下个安全点卸旧层（依赖安全，被依赖则拒绝）、下一帧装回同 DLL 新副本——旧模块先释放再装载，可用于迭代。

## Plugin Macros

| 宏 | 用途 |
|----|------|
| `MAHO_DECLARE_LAYER(LayerType, DLL)` | 层类内生成 StaticName / GetName / GetModulePath（静态）/ CreateLayer |
| `MAHO_DECLARE_ENGINE(EngineType, DLL)` | 引擎类内生成 CreateEngine 工厂 / GetModulePath |
| `MAHO_DECLARE_STAGE_DISPATCH(Context, Stage, Cast, Method)` | 生成 `Invoke<Stage, Context>` 全特化（dynamic_cast + 调用） |

## Dependency / Linking / Include Rules

链接方向分层单向（箭头 = 链接目标）：

```
engine core (Maho)  <--  engine plugins（每个引擎插件链接 Maho）
engine core + engine plugins  <--  project core（入口层，链接 Maho + 全部挂载引擎插件）
engine core + engine plugins + project core  <--  project plugins（链接父层，经 .cplugin 传递获得 include）
```

Include 方向：**引擎核心 ↔ 引擎插件**单向（插件 include 核心，核心零应用假设）；**项目核心 ↔ 项目插件**双向（核心以编译期类型引用子类，无构建环）。构建依赖（.cplugin）保持分层单向，父层仅以编译期类型引用子层，不构成构建环。

## Lifecycle

生命周期由宿主拥有：`FEngineBase::Shutdown` 释放全部 feature + DLL。feature 析构不静默 teardown；init/shutdown 由用户经 `IInit`/`IShutdown` 能力显式驱动。`FEngineBase::Main` 末尾先 `Features.clear()`（虚析构在各自 DLL），再 `Modules.clear()`（FreeLibrary）。

## Related Docs

- [EngineAPI.md](EngineAPI.md) — API 文档
- [CoreAPI.md](../Core/CoreAPI.md) — Core 基建
- [README.md](../../../README.md) — 引擎总览
