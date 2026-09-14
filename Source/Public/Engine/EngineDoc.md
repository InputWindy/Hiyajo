# Engine

## Code Files

- [Engine.h](Engine.h) — 10 个 stage 能力接口 + FEngineBase + MAHO_DECLARE_ENGINE
- [Layer.h](Layer.h) — FLayerBase / FLayer / Invoke 派发 / 依赖声明 DSL / MAHO_DECLARE_LAYER / MAHO_DECLARE_STAGE_DISPATCH
- [LayerCollector.h](LayerCollector.h) — FLayerCollector（层集合：安装/卸载/重载、状态广播、依赖安全卸载）
- [LayerTaskGraph.h](LayerTaskGraph.h) — FLayerTaskGraph（层 → 依赖图展开）
- [Query.h](Query.h) — TQuery（编译期类型筛选）/ FQuery / FQueryResult（运行时实例筛选）

## Concept -- Anonymous Layer + Ordered Stage Pipeline

Engine 层把应用拆成一组**单职责匿名层**，每层沿**有序 stage 管线**展开成任务图节点，由依赖图调度器自动编排——有依赖串行、无依赖并行，依赖边即隐式屏障。

### 1. 匿名层（FLayerBase）

层只闭合自己：`GetName()`（稳定身份名 = 拓扑键，由 `MAHO_DECLARE_LAYER` 从类型名字符串化而来，是**编译期字面量**，不依赖 `Name` 池）+ `GetDependencies()` / `GetDependents()`（逐 stage 依赖表，引擎读，子类不访问）。**不管理依赖生命周期**——执行上下文完整性由 `FLayerTaskGraph` 保证。层可动态加载（DLL），宿主只持有 `FLayerBase*`。

### 2. 有序阶段管线（IPipeline）

`IPipeline<TStageTypes...>` 定义有序阶段序列（`TStages`）；引擎提供 10 个 stage 能力接口（IPreInit/IInit/IPostInit/IBeginFrame/ITick/IEndFrame/IExit/IPreShutdown/IShutdown/IPostShutdown），stage→方法调用的 `Invoke` 协议按 (stage, context) 对特化。**可选能力组合**：层只继承自己要的阶段接口，没实现的 stage 在派发时 `dynamic_cast` 失败、静默跳过（合法状态）。

### 3. 层展开（FLayerTaskGraph）

每层沿 `TStages` 展开成**每阶段一个节点**：自推进（阶段 N 依赖同层阶段 N-1）+ 跨对象依赖（层声明的正向元组）+ 反向依赖（层声明的 `BlockOn`，`Init` 时施加到对方节点，对方不在本图则跳过）。节点就绪即释放——**无阶段屏障**，跨阶段管线（A 的 Tick 依赖 B 的 BeginFrame）天然成立。

推论→规则：**层必须装进「挂载了它 stage 列表」的那个 collector**（安装树保证这一点：层由驱动它的父层装进自己的收集器）。装错收集器的层仍有 no-op 节点，但永不被真正驱动，且它在外来 stage 上声明的依赖永远不成为边——`Compile` 看不到它们。

### 4. 依赖声明（DSL）

裸模板 `WaitFor<...>` / `BlockOn<...>` 与两张依赖表都是 `private`；声明只能经 `protected` 的 DSL。四种形式——点类型的正向/反向，与按名字的正向/反向：

```cpp
MyStage<ITick>().IsWaiting<FLog>().ForStage<IBeginFrame>();                      // 正向，点类型
MyStage<IShutdown>().IsBlocking<FResourceSystem>().OnStage<IShutdown>();         // 反向，点类型
MyStage<ITick>().IsWaiting("FLog").ForStage<IBeginFrame>();                      // 正向，按名字
MyStage<IShutdown>().IsBlocking("FUIViewRegistry").OnStage<IShutdown>();         // 反向，按名字
```

按名字的形式给**不能点名对方类型**的消费者用（点名会强加对可选插件的构建依赖）。名字在实际安装进本图的层里解析，找不到就不绑边。

### 5. 层集合（FLayerCollector）

`Install` / `TryUninstall` / `Reload` 记录进 pending 集，在安全点 `FlushPendingUpdatePipelines` 统一应用（同一窗口内「先取消被撤销的装载」→ 再 Init 批 → 最后卸载）。卸载用**反向依赖计数最小堆贪心**：被依赖的层拒绝卸载，依赖者先弹出、链式卸载。

**每个终态都广播**（`OnLayerStatus` + `ELayerStatus` 12 态）：装载排队/被拒/编译失败/完成/被取消，卸载排队/未命中/被拒/编译失败/完成，重载排队/被拒。没有静默路径；`InstallChildrenOf` 另有 `{Requested, Queued, Refused}` 摘要。事件派发隔离——订阅者抛异常只报错，不影响收集器状态与 teardown。

**失败与门禁**：`Install`/`Reload` 在收集器「关闭」后一律拒绝（`OnClosing` 广播一次）；装载批 `Compile` 失败 ⇒ 报错并**释放该批**（不自动重试，调用者修好后再装）；卸载图 `Compile` 失败 ⇒ 报错并让这些层**保持存活**（绝不在 Shutdown 没跑的情况下析构）。宿主侧的 `Install`/`TryUninstall` 与子收集器共用同一套代码。

### 6. 筛选（Query）

- **`TQuery<TList>`**：纯类型运算，`Select`(OR) / `With`(AND) / `Not`(NOR) → `FResult`，不引用实例。
- **`FQuery<TBase>` / `FQueryResult<TBase>`**：运行时按接口谓词筛实例（`dynamic_cast`），可当 `vector` 用。结果内部向量私有（外部不能注入裸指针），并记住来源集合做 Debug 活性审计：指针已不在来源里（层已卸载）就丢弃并报错——因为卸载是「先移出集合再释放模块」，对已卸载对象做 `dynamic_cast` 会解引用悬垂 vptr。

## Engine Main Loop

`FEngineBase` 把生命周期拆成两个钩子和一个纯调度主循环（`EntryPoint` 驱动 `PreMain → Main → PostMain`）：

```text
PreMain  安装 + 初始化：Load 安装树 → InstallChildrenOf(GetName()) → FlushPendingUpdatePipelines<Init, Shutdown>
Main     纯调度 Tick 环：
           帧边界：挂起更新？→ WaitAll() → FlushPendingUpdatePipelines<Init, Shutdown>
                   bLayersDirty？→ WaitAll() → 重展开 Tick 图 + Compile（失败报一次 + 退化成空图）
                   SubmitFrame()   // 只等自己要复用的环槽，帧之间会重叠
                   ShouldExit()？→ break
           退出前：WaitAll() + Pool.Flush()
PostMain 关闭闸门 → 丢弃未生效装载 → 卸载所有活层 → 循环 flush 到静 → Pool.Flush → 残留层报出
```

**帧重叠**：`SubmitFrame` 只等「自己要复用的那一环槽」（`MAHO_FRAMES_IN_FLIGHT` 深，默认 3），不等上一帧跑完。跨帧安全由**每层 gate**保证：同层下一帧的 root 必须等上一帧整层（root→…→sink）跑完才被派发；不同层之间照常跨帧并行。gate 的交接是**无计数**的（抢闸或排队，实例的 sink 恰好 pop 一个等待者），不存在计数漂移。

**缓存**：Tick 图跨帧缓存并由 `OnLayersChanged`（push，非轮询）驱动重建——`FEngineBase::Main` 绑定它置 `bLayersDirty`。层集不变的普通帧只 `SubmitFrame`，不重建、不重连、不重新过滤。

**失败隔离**（坏插件不杀宿主）：
- 图编译失败（缺失依赖 / 依赖环）→ `ReportError`（每种破损**报告一次**，不刷屏）+ 坏层暂不调度（空图），拓扑修复后重试。
- 安装失败 → `ReportError` + `InstallRefused` 广播，不再静默。
- 节点/worker 抛异常 → `ReportError`（非致命），并**仍释放下游**，图不挂起。
- 卡死审计（Debug）：帧超过 `MAHO_TASKGRAPH_STALL_MS` 未排干 → 报出卡住的节点/门，并落盘 `TaskGraphStall.txt`（图的固有失败模式是「缺边=挂」，必须说出来）。

**热重载**：`Reload("LayerName")` 下个安全点卸旧层（依赖安全，被依赖则拒绝）、随后装回同 DLL 新副本——旧模块先释放再装载，可用于迭代。

## Plugin Macros

| 宏 | 用途 |
|----|------|
| `MAHO_DECLARE_LAYER(LayerType)` | 层类内生成 StaticName / GetName / CreateLayer / GetModulePath（DLL 名 = 类型名 + 平台后缀） |
| `MAHO_DECLARE_ENGINE(EngineType)` | 引擎类内生成 CreateEngine 工厂 / GetModulePath / StaticName / GetName |
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

生命周期由宿主拥有：装载与初始化在 `PreMain`，卸载与 Shutdown 在 `PostMain`（`TryUninstall` 所有活层 + 循环 flush + `Pool.Flush`），之后 `EntryPoint` 才 `delete App`。feature 析构不静默 teardown；Shutdown 阶段由卸载图显式驱动。层必须自洽：自己的异步工作者、资源、视图订阅，都要在自己的 Shutdown / `PreUnInstall` 里收干净（否则 teardown 会报出残留层或资源）。

## Related Docs

- [EngineAPI.md](EngineAPI.md) — API 文档
- [CoreAPI.md](../Core/CoreAPI.md) — Core 基建
- [README.md](../../../README.md) — 引擎总览
