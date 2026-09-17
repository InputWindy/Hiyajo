# Engine

## Code Files

- [Engine.h](Engine.h) — 10 个 stage 能力接口 + FEngineBase + MAHO_DECLARE_ENGINE（含引擎侧 stage 序列别名 `FInitStages`/`FTickStages`/`FShutdownStages`）
- [Frame.h](Frame.h) — 引擎侧 stage 机器：`Invoke` 派发 + `TFrameDispatch<TStages, TContext>`（唯一知道 stage 列表的地方）+ `MAHO_DECLARE_FRAME` + `MAHO_DECLARE_STAGE_DISPATCH`
- [FrameBuilder.h](FrameBuilder.h) — `FFrameBuilder`：帧集合的安装/卸载/重载 + 帧循环，并**私有持有** `FFrameGraph`/`FFrameBridge`/`TFrameDispatch`
- [Query.h](Query.h) — TQuery（编译期类型筛选）/ FQuery / FQueryResult（运行时实例筛选）

> 声明层在 Core：`FFrameExtension`（身份 + 它声明的边）在 [`Core/FrameGraph.h`](../Core/FrameGraph.h)。
> 调度器 `FFrameGraph` 与桥 `FFrameBridge` 也在那里。

## Concept -- Frame Extension + Ordered Stage Pipeline

Engine 侧把应用拆成一组**单职责 frame extension**，每个沿**有序 stage 序列**被展开成一批节点，交给 `FFrameGraph` 调度——有依赖串行、无依赖并行，依赖边即排序。

### 1. 声明层（FFrameExtension，Core）

它只闭合自己：`GetName()`（稳定身份名 = 拓扑键，由 `MAHO_DECLARE_FRAME` 从类型名字符串化而来，是**编译期字面量**）+ 它声明的边（`AddDependency` / `AddDependent`，由 DSL 调用）。**它不知道 stage 序列** —— 那是引擎侧的事；**也不管理依赖生命周期**。frame 可动态加载（DLL），宿主只持有 `FFrameExtension*`。

### 2. 有序阶段序列（IPipeline）

`IPipeline<TStageTypes...>` 定义有序序列（`TStages`）；引擎提供 10 个 stage 能力接口（IPreInit/IInit/IPostInit/IBeginFrame/ITick/IEndFrame/IExit/IPreShutdown/IShutdown/IPostShutdown）。stage→方法调用由 `TFrameDispatch::MakeClosure` 包成 `Invoke<Stage, Context>`（按 (stage, context) 对特化）。**可选组合**：frame 只继承自己要的 stage 接口，没实现的 stage 由 `TFrameDispatch::Implements` 判定，**根本不产出节点**（已经没有 no-op 空节点了）。

### 3. 批次展开（FFrameBridge::Build）

桥把「frame 集合 + stage 序列 + 帧号」展开成一批 `FTask`。每个 frame 为它**实现的**每个 stage 产出一个节点，节点身份是三元组 `{名字, stage, 相位}`，相位 = `帧号 % MAHO_FRAMES_IN_FLIGHT`。

桥另外产出两类**结构边**（都是「stage 序列」本身的语义，而序列只有桥知道）：

1. **帧内链**：同一帧中相邻的已产出节点依次相连，中间被剪掉的 stage 不切断链；
2. **跨帧自边**：每个节点再等**它自己**上一帧（同 stage、相位 `帧号−1`）。

声明的边在这里解析：相对帧偏移变成绝对身份，反向声明（`IsBlocking`）落在**声明者**的节点上、指向目标；绑不上的名字被**报告**（图对悬空的边是静默消解——见 `FFrameGraph` 的头注释）。

### 4. 依赖声明（DSL）

两张依赖表是 `private`；声明经公开的 DSL。**规范拼写**把「帧」并进 stage 选择器：

```cpp
MyStage<IInitViews>().WaitFor<FLog>().OnStage<IInit>();               // 正向，本帧
MyStage<IInitViews>().WaitFor<FLog>().OnLastFrameStage<IInit>();      // 正向，上一帧（跨帧）
MyStage<IShutdown>().BlockOn<FResourceSystem>().OnStage<IShutdown>(); // 反向，本帧
MyStage<IShutdown>().BlockOn("FUIViewRegistry").OnLastFrameStage<IShutdown>();
MyStage<ITick>().WaitFor("FLog").OnStage<IBeginFrame>();              // 按名字
```

- 旧拼写 `IsWaiting` / `IsBlocking` + `LastFrame()` + `ForStage()` 是**别名**，行为相同。
- 按名字的形式给**不能点名对方类型**的消费者用（点名会强加对可选插件的构建依赖）。名字在实际进本批的 frame 里解析，找不到就不绑边（并由桥报告「写错」的那几类）。

### 5. 帧集合（FFrameBuilder）

`Install` / `TryUninstall` / `Reload` 记录进 pending 集，在安全点 `FlushPendingUpdates<TInit, TShutdown>` 统一应用（同一窗口内「先取消被撤销的装载」→ 再 Init 批 → 最后卸载）。卸载用**反向依赖计数最小堆贪心**：被依赖的 frame 拒绝卸载，依赖者先弹出、链式卸载。

宿主可见的动词（其余全是 private）：

| 动词 | 作用 |
|---|---|
| `Install` / `TryUninstall` / `Reload` / `InstallChildrenOf` | 拓扑变更（下个安全点生效）|
| `FlushPendingUpdates<TInitStages, TShutdownStages>()` | 应用挂起变更（内部即安全点）|
| `Execute<TLoopStages>()` | 跑一帧（LINQ 查帧集 → 建批 → 提交；**不排空**，帧会流水）|
| `Wait()` | 收摊静默：图 + 池都排空 |
| `UninstallAll()` / `ReleaseAll()` / `CancelPendingReloads()` | 收摊清扫（见下）|
| `GetStats()` | 规模查询（工具/面板用，勿每帧轮询）|

**每个终态都广播**（`OnFrameStatus` + `EFrameStatus` 12 态）：装载排队/被拒/编译失败/完成/被取消，卸载排队/未命中/被拒/编译失败/完成，重载排队/被拒。没有静默路径；`InstallChildrenOf` 另有 `{Requested, Queued, Refused}` 摘要。事件派发隔离——订阅者抛异常只报错，不影响收集器状态与 teardown。

**失败与门禁**：
- `Install`/`Reload` 在收集器「关闭」后一律拒绝（`OnClosing` 广播一次）；
- 安装批被 **`Submit` 拒绝**（结构性：批内身份重复 / 相位越界 / 成环）⇒ 报错并**释放该批**（不自动重试）；
- 卸载批被拒 ⇒ 报错并让这些 frame **保持存活**（绝不在 Shutdown 没跑的情况下析构）；
- **缺依赖不再拒绝安装**：目标不存在就是「这条边不存在」（帧照跑），由桥报告写错的那几类（名字不在帧集 / stage 不在序列 / 目标未实现该 stage）；
- `ReleaseAll()`：收摊的最后手段——**不跑 teardown 阶段**硬释放，但仍保持「在 `Pipelines` 里 == 活着」的不变量（渲染层必须在 RHI 死之前销毁 feature 实例时用它）。

### 6. 筛选（Query）

- **`TQuery<TList>`**：纯类型运算，`Select`(OR) / `With`(AND) / `Not`(NOR) → `FResult`，不引用实例。
- **`FQuery<TBase>` / `FQueryResult<TBase>`**：运行时按接口谓词筛实例（`dynamic_cast`），可当 `vector` 用（但**不能直接 range-for**，先赋给 vector）。结果内部向量私有（外部不能注入裸指针），并记住来源集合做 Debug 活性审计：指针已不在来源里（frame 已卸载）就丢弃并报错——因为卸载是「先移出集合再释放模块」，对已卸载对象做 `dynamic_cast` 会解引用悬垂 vptr。

## Engine Main Loop

`FEngineBase` 把生命周期拆成两个钩子和一个纯调度主循环（`EntryPoint` 驱动 `PreMain → Main → PostMain`）：

```text
PreMain  安装 + 初始化：Load 安装树 → InstallChildrenOf(GetName()) → FlushPendingUpdates<Init, Shutdown>
Main     while (!ShouldExit())
           FlushPendingUpdates<FInitStages, FShutdownStages>()   // 应用挂起拓扑变更
           Execute<FTickStages>()                                 // 建批 + 提交（不排空 ⇒ 帧流水）
         Wait()                                                   // 收摊静默（图 + 池）
PostMain 关闭闸门 → 丢弃未生效装载 → UninstallAll → 循环 flush 到静 → Wait() → 残留报出
```

**帧重叠**：`Execute` 不排空，`Submit` 只做「按相位准入」（复用一个环槽前等它排空）⇒ 最多 `MAHO_FRAMES_IN_FLIGHT` 帧在飞（默认 3）。跨帧安全由**桥的结构边**保证（每个 stage 与自己的上一帧），不同 frame extension 之间照常跨帧并行。

**帧集缓存**：`bLoopDirty` 由收集器在 `FlushPendingUpdates` 变更有变化时**自己置位**，下一帧 LINQ 重查——宿主不持有缓存，也无从忘记失效。

**失败隔离**（坏插件不杀宿主）：
- 批被拒（环/重复/越界）→ `ReportError`（**同因只报一次**，不刷屏）；
- 安装被拒 → `ReportError` + `InstallRefused` 广播，不再静默；
- 节点/worker 抛异常 → `ReportError`（非致命），并**仍置位完成事件**，图不挂起；
- 绑不上的声明 → 桥报告一次（每帧集一次，不每帧刷屏）；
- **stage 追踪（Debug）**：`MAHO_TRACE_STAGES=1` 打出每个 stage 的 enter/exit —— 硬崩（0xC0000005）没有栈也没有异常，最后一条「enter 无 exit」是唯一能指名肇事 stage 的手段。

**热重载**：`Reload("FName")` 下个安全点卸旧 frame（依赖安全，被依赖则拒绝）、随后装回同 DLL 新副本——旧模块先释放再装载，可用于迭代。

## Plugin Macros

| 宏 | 用途 |
|----|------|
| `MAHO_DECLARE_FRAME(FrameType)` | frame 类内生成 StaticName / GetName / CreateFrame / GetModulePath（DLL 名 = 类型名 + 平台后缀） |
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

生命周期由宿主拥有：装载与初始化在 `PreMain`，卸载与 Shutdown 在 `PostMain`（`UninstallAll` + 循环 flush）。**排空点在两个地方**：`FlushPendingUpdates` 内部（每个一次性批次前后各排空一次 ⇒ 它自身就是安全点），以及 `~FFrameBuilder → Wait()`（图 + 池，在实例与模块释放之前）。`EntryPoint` 随后才 `delete App`，再析构引擎 DLL。frame 析构不静默 teardown；Shutdown 阶段由卸载批显式驱动。frame 必须自洽：自己的异步工作者、资源、视图订阅，都要在自己的 Shutdown / `IPreUnInstall` 里收干净（否则 teardown 会报出残留 frame 或资源）。

## Related Docs

- [EngineAPI.md](EngineAPI.md) — API 文档
- [CoreDoc.md](../Core/CoreDoc.md) — Core 基建（含调度器 `FFrameGraph`）
- [README.md](../../../README.md) — 引擎总览
