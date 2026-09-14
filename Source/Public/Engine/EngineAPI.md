# Engine — API 文档

Engine 层 = 层系统：匿名层锚点 + 有序 stage 管线 + 依赖图调度。层只声明身份与逐 stage 依赖，全局调度由 `FLayerTaskGraph` 统一负责。全在 `Source/Public/Engine/` 头文件（模板 + 内联）。

## Layer.h

### Invoke<TStage, TContext> <function（模板）>

阶段派发自由函数模板，按 (stage, context) 对特化。`FLayerTaskGraph` 运行时调用 `Invoke<TStage>(Layer, Context)`；每个 stage 接口针对每种 context 有全特化（如 `Engine.h` 里 `Invoke<IInit, FEngineBase>`、`Render.h` 里 `Invoke<IRender, FRender>`）。未实现该接口的层经 `dynamic_cast` 失败**静默跳过**（这是合法状态，不是错误）。

### MAHO_DECLARE_STAGE_DISPATCH(ContextType, StageType, CastType, Method) <宏>

阶段派发特化糖——把 `Invoke<StageType, ContextType>` 全特化为 dynamic_cast 到 `CastType` 并调用 `Method(Context)`：

```cpp
MAHO_DECLARE_STAGE_DISPATCH(FEngineBase, IInit, IInit, Initialize)
// => Invoke<IInit, FEngineBase>(Layer, Engine) -> cast IInit -> Initialize(Engine)
```

### FLayerBase <class>

**匿名层锚点**——可能动态加载的 feature 的多态基类。携带身份 + 逐 stage 依赖声明。生命周期经 `IPipeline<TStages...>` 组合；层**永不管理依赖生命周期**——执行上下文完整性由 `FLayerTaskGraph` 保证。层只闭合自己。

#### 接口

| 签名 | 说明 |
|------|------|
| `virtual ~FLayerBase()` | 虚析构（跨 DLL 删除经 DLL 自己的代码） |
| `virtual std::string_view GetName() const = 0` | 稳定身份名——TaskGraph 拓扑键；由声明宏从类型名字符串化而来（`#LayerType`，编译期字面量） |
| `virtual const FDependencyTable& GetDependencies() const` | 逐 stage 依赖表（引擎读；子类不访问） |
| `const std::vector<FDependent>& GetDependents() const` | 反向依赖表（引擎读；子类不访问） |

#### 嵌套类型

| 类型 | 说明 |
|------|------|
| `FDependency { Name, Stage }` | 正向依赖元组：dep 名 + dep 的 stage 接口 `type_index` |
| `FDependencyTable` | `map<我的 stage type_index, vector<FDependency>>` |
| `FDependent { Name, Stage, MyStage }` | 反向依赖元组：谁（`Name`@`Stage`）必须等我（`MyStage`） |

#### 依赖声明 DSL（`protected`，子类唯一入口）

裸模板 `WaitFor<...>()` / `BlockOn<...>()` 与两张表都是 `private`：声明只能经 DSL 表达（DSL builder 是嵌套类，才能触到私有落点）。

| 形式 | 含义 |
|------|------|
| `MyStage<TMy>().IsWaiting<TOther>().ForStage<TOtherStage>()` | 我的 `TMy` 等 `TOther` 的 `TOtherStage`（正向，点名类型） |
| `MyStage<TMy>().IsBlocking<TOther>().OnStage<TOtherStage>()` | `TOther` 的 `TOtherStage` 等我 `TMy`（反向，点名类型） |
| `MyStage<TMy>().IsWaiting("FLog").ForStage<TOtherStage>()` | 同上正向，但**按名字**寻址 |
| `MyStage<TMy>().IsBlocking("FUIViewRegistry").OnStage<TOtherStage>()` | 同上反向，但**按名字**寻址 |

按名字的形式用于**不能点名对方类型**的消费者——点名会强加对可选插件的构建依赖（例：通用脚手架安排某个视图注册表的卸载次序）。名字在**实际安装进本图**的层里解析，找不到就不绑边（优雅降级）。

### FLayer<TPipelines...> <class>

装配语法糖——把 `FLayerBase`（身份 + 依赖）与一个或多个 `IPipeline`（有序 stage）绑成一个层类型。`FLayerBase` 与 `TPipelines...` 无继承关系，调度时经 `dynamic_cast` 侧向转换。

```cpp
class FWorld : public FLayer<IPipeline<IMain, IShutdown>> {};
class FWorldMulti : public FLayer<IEngineTickPipeline, IEngineInitPipeline> {};
```

### MAHO_DECLARE_LAYER(LayerType) <宏>

层声明糖——生成 `StaticName()` + `GetName()` + `CreateLayer()` + `GetModulePath()`。名字来自类型名字符串化（`#LayerType`），依赖声明用同一类型推导，拓扑键自洽。DLL 名 = 类型名 + 平台后缀（`ApplyModuleExtension`），宏只收一个参数：

```cpp
class FWorld : public FLayer<...> { MAHO_DECLARE_LAYER(FWorld); ... };
// StaticName()/GetName() == "FWorld"；GetModulePath() == "FWorld.dll"（平台后缀在运行时拼接）
```

## Query.h

两个正交的筛选器：`TQuery` 纯类型运算（编译期）、`FQuery`/`FQueryResult` 实例筛选（运行时）。

### TQuery<TList> <class（编译期）>

类型表上的 LINQ：输入 `TTypeList`，输出筛选后的 `TTypeList`，不引用任何实例。

| 链式调用 | 语义 |
|---|---|
| `Select<T...>` | 保留派生自 T 中**任意一个**的类型（OR） |
| `With<T...>` | 保留派生自 T 中**全部**的类型（AND） |
| `Not<T...>` | 去掉派生自 T 中任意一个的类型（NOR） |
| `FResult` | 当前幸存类型表 |

```cpp
using FTable = TTypeList<FLog, FNet, FAudio>;
using FTickable = TQuery<FTable>::Select<ITick>::With<IShared>::FResult;
```

### FQuery<TBase> <class（运行时）>

对**实例集合**做同样的接口谓词筛选（`dynamic_cast`），层集在编译期未知时使用。

| 签名 | 说明 |
|------|------|
| `virtual std::vector<TBase*>& GetQueryData() = 0` | 数据源**写路径**；在收集器里是 `private`（对外只给 const 版本，防止绕过安装校验直接塞指针） |
| `virtual const std::vector<TBase*>& GetQueryData() const = 0` | 数据源读路径（对外可见） |
| `template<T...> FQueryResult<TBase> Select<T...>()` | OR 筛选 |
| `template<T...> FQueryResult<TBase> With<T...>()` | AND 筛选 |
| `template<T...> FQueryResult<TBase> Not<T...>()` | NOR 筛选 |
| `template<T> std::vector<T*> Cast<T>()` | 转成具体接口指针向量（只留非空幸存者） |

### FQueryResult<TBase> <class>

筛选结果的**值类型**，可直接当 `vector` 用（隐式转换保留，`FLayerTaskGraph::Init(Query)` 依赖它）。

| 签名 | 说明 |
|------|------|
| `operator std::vector<TBase*>&()` / `const&()` | 隐式转换（与既有调用点兼容） |
| `Num()` / `IsEmpty()` / `GetData()` | 只读访问 |
| `Select/With/Not/Cast` | 在**当前结果集**上继续链式筛选 |

- 内部 `Data` 是 `private`：外部无法注入裸指针。
- 结果记住**来源集合**，`Cast`/`Filter` 在 Debug 下先做**活性审计**（该指针是否仍在来源里）——不在就丢弃并 `ReportError`。原因：卸载是「先移出 `Pipelines` 再释放模块」，而 `dynamic_cast` 要解引用 vptr，对已卸载对象做就是崩。Release 下退化为非空判断（零成本）。
- 契约：结果只在下一次集合变更前有效；跨帧保存裸指针请改用 `Cast<T>()` 并自行重查。

## LayerTaskGraph.h

### FLayerTaskGraph<TStages, TContext = FEmptyContext> <class : FTaskGraph>

一组匿名 `FLayer*` → 编译 → 执行。`TStages` 是 `TTypeList<StageInterface...>`；**每个传入层沿 `TStages` 展开成每阶段一个节点**——层没实现的阶段也照样出节点，派发时 `dynamic_cast` 失败即静默 no-op。

- **自推进**：阶段 N 依赖同层阶段 N-1
- **跨对象依赖**：层在该阶段声明的 `WaitFor` 元组
- **反向依赖**：层声明的 `BlockOn` 元组在 `Init` 时施加到对方节点；对方不在本图则跳过

推论（据此定规则「层必须装进挂载了它 stage 列表的那个 collector」）：若某层的 stage 不在 `TStages` 里，它虽然仍有 no-op 节点，却**永远不会被真正驱动**，且它在那些外来 stage 上声明的依赖**永远不会变成边**——`Compile` 根本看不到它们。

| 签名 | 说明 |
|------|------|
| `FLayerTaskGraph(FThreadPool&, TContext&)` | 绑定线程池 + 执行上下文（引用，不拷贝） |
| `void Init(std::vector<FLayerBase*>)` | 重建节点集（可重复调用；须图静止） |
| `bool Compile()` | 接线 + 环/缺依赖检测；失败时 `GetCompileErrorNode()` 给出坏层名 |
| `void Execute()` | 提交一帧（内联转调 `SubmitFrame`，安装/卸载图沿用） |
| `void SubmitFrame()` / `void WaitFence()` / `void WaitAll()` / `bool IsIdle()` | 继承自 `FTaskGraph` 的帧 API |

```cpp
using FTickStages = TTypeList<IBeginFrame, ITick, IEndFrame, IExit>;
FLayerTaskGraph<FTickStages, FEngineBase> G(Pool, Engine);
G.Init(Engine.Select<IBeginFrame, ITick, IEndFrame, IExit>());
if (G.Compile()) { G.Execute(); G.WaitAll(); }
```

## LayerCollector.h

### FLayerCollector<TContext> <class : FQuery<FLayerBase>>

层集合管理基类——拥有 + 调度一组匿名 `FLayerBase`。安装/卸载/重载记录进 pending 集，在 `FlushPendingUpdatePipelines` 安全点应用；卸载依赖安全（反向计数最小堆贪心）。`TContext` 是每个 stage 方法收到的调度上下文（引擎是 `FEngineBase`，渲染子系统是 `FRender`），同时充当 `FQuery` 数据源。

#### 事件（`public`，每收集器一套）

| 签名 | 说明 |
|------|------|
| `TMulticastEvent<void()> OnLayersChanged` | 层集在安全点发生变化；宿主绑定它重编缓存图（push 而非轮询） |
| `TMulticastEvent<void(const FLayerStatusInfo&)> OnLayerStatus` | **每个终态**一条（见 `ELayerStatus`）——拒绝、取消、编译失败、装载完成、卸载完成……不再有静默路径。**只观察**：handler 里不得再 `Install`/`Uninstall`/`Reload` |
| `TMulticastEvent<void()> OnClosing` | 收集器翻进「关闭」那一刻广播一次（此后 `Install`/`Reload` 一律拒绝） |

事件派发是**隔离**的：某个 handler 抛异常 → `ReportError` 并吞掉（同一广播里其后的 handler 被跳过），收集器自身状态不受影响——`EmitStatus` 是在卸载循环中间调的，不能让订阅者把 teardown 打断在半途。

#### 状态类型

| 类型 | 说明 |
|------|------|
| `enum class ELayerStatus` | 12 态：`InstallQueued / InstallRefused / InstallCompileFailed / Installed / InstallCancelled / UninstallQueued / UninstallNotFound / UninstallRefused / UninstallCompileFailed / Uninstalled / ReloadQueued / ReloadRefused` |
| `FLayerStatusInfo { Status, Name, Path, Detail }` | 事件载荷；字段全是**拷贝**（模块可能随即被卸） |
| `FInstallSummary { Requested, Queued, Refused }` | `InstallChildrenOf` 的返回摘要 |
| `FLayerStats { Active, PendingAdds, PendingRemoves, PendingReloads, Modules }` | `GetStats()` 返回 |

#### 接口

| 签名 | 说明 |
|------|------|
| `template<T> bool Install()` | 按类型安装（`T::GetModulePath()` 解析 DLL 名）；等价于 `Install(dll)` |
| `bool Install(string_view DllPath, const char* FactorySymbol = "CreateLayer")` | **唯一安装入口**（匿名加载，无指针安装）。经 `FAssembly` 装载 + 建实例 + 名字查重；失败原因经 `InstallRefused` 报出（关闭中 / 模块加载失败 / 无工厂符号 / 工厂抛异常 / 工厂返回 null / 重名）。**不做依赖前置检查**：跨 stage 的合法互依赖无法按名判环，交给图 `Compile` 校验 |
| `FInstallSummary InstallChildrenOf(string_view ParentLayer)` | 按安装树装**直系子插件**（`GetChildren` 顺序），父层与收集器层共用同一个调用 |
| `void Reload(string_view LayerName)` | **热重载**：下个安全点卸旧层（依赖安全，被依赖则拒绝并报出）、随后装回同 DLL 新副本。仅对**活动层**有效；仍在 pending 的层给出明确拒绝原因 |
| `void TryUninstall(string_view Query)` | 匿名卸载：按**层名**或**DLL 路径**匹配第一命中（活动层与 pending 层都可按名命中）；命中即入 pending，未命中广播 `UninstallNotFound` |
| `template<TInitStages, TShutdownStages> void FlushPendingUpdatePipelines()` | 应用挂起更新：先处理「装载被同窗口的卸载请求取消」，再 Init 批（`TInitStages`），最后卸载（`TShutdownStages`）。有变化则广播 `OnLayersChanged`。**重入被拒绝**；内部 `try/catch`：插件代码抛异常时 pending 集保持原样，下次重试 |
| `void DropPendingInstalls()` | 丢弃未生效的装载：实例与模块一起释放（`Install` 是即时装载，只延后 Init） |
| `void CloseForLoads()` / `bool IsClosing()` | 关闭闸门：幂等置位并广播 `OnClosing`；置位后 `Install`/`Reload` 拒绝 |
| `FLayerStats GetStats()` | 当前规模（工具/测试/面板用查询，不要每帧轮询） |

#### 失败与终态的语义

- **装载**：`Compile` 失败 ⇒ 报错 + 广播 `InstallCompileFailed`，该批**被释放**（不自动重试：留 pending 会每帧重编重报，只留实例会让下次 `Install` 造出第二个）。调用者修好次序/依赖后再 `Install` 一次即可。
- **卸载**：贪心拿不下的层**报出并带上依赖者**（`UninstallRefused`）；卸载图 `Compile` 失败 ⇒ 报错 + `UninstallCompileFailed`，这些层**保持存活**（绝不在 Shutdown 阶段没跑的情况下析构）。
- 卸载算法：`RebuildReverseDeps` 重建反向依赖计数（层名 → 被依赖次数），`FlushUnload` 用**最小堆贪心**——被依赖的层拒绝卸载，依赖者先弹出并链式卸载。

## Engine.h

### 阶段接口：IPreInit / IInit / IPostInit / IPreShutdown / IShutdown / IPostShutdown / IBeginFrame / ITick / IEndFrame / IExit <class>

引擎的 **10 个 stage 能力接口**，每个只有一个纯虚方法，签名统一为 `void Xxx(FEngineBase&)`：

| 接口 | 方法 |
|------|------|
| `IPreInit` | `void PreInitialize(FEngineBase&)` |
| `IInit` | `void Initialize(FEngineBase&)` |
| `IPostInit` | `void PostInitialize(FEngineBase&)` |
| `IPreShutdown` | `void PreShutdown(FEngineBase&)` |
| `IShutdown` | `void Shutdown(FEngineBase&)` |
| `IPostShutdown` | `void PostShutdown(FEngineBase&)` |
| `IBeginFrame` | `void BeginFrame(FEngineBase&)` |
| `ITick` | `void Tick(FEngineBase&)` |
| `IEndFrame` | `void EndFrame(FEngineBase&)` |
| `IExit` | `void RequestExit(FEngineBase&)` |

主循环把它们分三组驱动：Init 组 `TTypeList<IPreInit, IInit, IPostInit>`（`PreMain` 里跑）、Tick 组 `TTypeList<IBeginFrame, ITick, IEndFrame, IExit>`（每帧）、Shutdown 组 `TTypeList<IPreShutdown, IShutdown, IPostShutdown>`（`PostMain` 里跑）。`Engine.h` 为每组定义了 `MAHO_DECLARE_STAGE_DISPATCH(FEngineBase, ...)` 全特化。

### FEngineBase <class : FLayerCollector<FEngineBase>>

引擎基类——命令行 KV + 主循环 + feature 所有权（`FLayerCollector`）。入口插件继承它并导出 `CreateEngine()`；它是唯一宿主。

#### 接口

| 签名 | 说明 |
|------|------|
| `FEngineBase()` / `virtual ~FEngineBase()` | 构造 / 析构 |
| `virtual void ParseCommandLine(int Argc, char** Argv)` | 解析 `-key`/`--key=value` 等命令行 |
| `virtual void PreMain() = 0` | 入口钩子（**纯虚**）：宿主在此装载并初始化服务层（`Install` + `InstallChildrenOf(GetName())` + `FlushPendingUpdatePipelines`） |
| `virtual int Main()` | 主循环：只调度的 Tick 环——帧边界应用挂起更新（拓扑变更先 `WaitAll`）、`bLayersDirty` 时重展开并 `Compile`、`EngineGraph.SubmitFrame()`、读 `ShouldExit()`；退出前 `WaitAll + Pool.Flush` |
| `virtual void PostMain()` | 出口钩子：关闭闸门、丢弃未生效装载、卸载所有活层并循环 flush 到静、`Pool.Flush`、残留层报出 |
| `bool Has/Get/GetBool/GetInt/GetAll` | 命令行 KV 读取 |
| `void RequestExit()` | 请求主循环下一帧边界退出（置收集器的关闭闸门，任意线程可调） |
| `bool ShouldExit() const` | 是否已请求退出（宿主对同一闸门的措辞） |

**帧语义**：`SubmitFrame` 只等自己要复用的环槽（`MAHO_FRAMES_IN_FLIGHT` 深），帧之间**会重叠**；跨帧安全由每层 gate 保证（同层下一帧等上一帧整层跑完），不同层之间跨帧并行。

### MAHO_DECLARE_ENGINE(EngineType) <宏>

引擎类声明糖——生成 `CreateEngine()` 工厂 + `GetModulePath()` + `StaticName()` + `GetName()`；同样只收一个参数（DLL 名由类型名 + 平台后缀推出）：

```cpp
class FExampleEngine : public FEngineBase { MAHO_DECLARE_ENGINE(FExampleEngine); ... };
```

## EntryPoint.h

### Maho::Main(int Argc, char** Argv) <function>

统一应用驱动——安装引擎 DLL 并执行其根实例：

```text
main()/WinMain() -> Maho::Main(Argc, Argv)
  InstallFatalHandlers()
  FAssembly Load(argv[1] ?? MAHO_ENGINE_NAME + 平台后缀)   // 安装
  GetProcAs<CreateFunction>("CreateEngine") -> FEngineBase*  // 创建根实例（匿名）
  ParseCommandLine -> PreMain -> Main -> PostMain            // 对称生命周期
  delete App
```

平台入口（Windows WinMain/main、Android `android_main`、iOS/Linux/Xbox main）都收敛到这里。

- [EngineDoc.md](EngineDoc.md) — 概念 · [实现字典](../../Private/Engine/EngineAPI.md) — 算法 · [Core API](../Core/CoreAPI.md) — 基建
