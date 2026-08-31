# Engine — API 文档

Engine 层 = 层系统：匿名层锚点 + 有序 stage 管线 + 依赖图调度。层只声明身份与逐 stage 依赖，全局调度由 `FLayerTaskGraph` 统一负责。全在 `Source/Public/Engine/` 头文件（模板 + 内联）。

## Layer.h

### Invoke<TStage, TContext> <function（模板）>

阶段派发自由函数模板，按 (stage, context) 对特化。`FLayerTaskGraph` 运行时调用 `Invoke<TStage>(Layer, Context)`；每个 stage 接口针对每种 context 有全特化（如 `Engine.h` 里 `Invoke<IInit, FEngineBase>`、`Render.h` 里 `Invoke<IRender, FRender>`）。未实现该接口的层经 `dynamic_cast` 失败**静默跳过**。

### MAHO_DECLARE_STAGE_DISPATCH <宏>

阶段派发特化糖——把 `Invoke<TStage, TContext>` 全特化为 dynamic_cast 到 `CastType` 并调用 `Method(Context)`：

```cpp
MAHO_DECLARE_STAGE_DISPATCH(FEngineBase, IInit, IInit, Initialize)
// => Invoke<IInit, FEngineBase>(Layer, Engine) -> cast IInit -> Initialize(Engine)
```

### FLayerBase <class>

**匿名层锚点**——可能动态加载的 feature 的多态基类。携带身份 + 逐 stage 依赖声明。生命周期经 `IPipeline<TStages...>` 组合；层**永不管理依赖生命周期**——加载器/TaskGraph 保证执行上下文完整。层只闭合自己。

#### 接口

| 签名 | 说明 |
|------|------|
| `virtual ~FLayerBase()` | 虚析构（跨 DLL 删除经 DLL 自己的代码） |
| `virtual std::string_view GetName() const = 0` | 稳定身份名——TaskGraph 拓扑键 |
| `const FDependencyTable& GetDependencies() const` | 逐 stage 依赖表 |
| `template<TMyStage, TDepObj, TDepStage> void AddDependency()` | 编译期声明：this 在 TMyStage 依赖 TDepObj 在 TDepStage |
| `void AddDependency(type_index MyStage, string_view DepName, type_index DepStage) protected` | 运行时声明：this 在 MyStage 依赖名为 DepName 的层在 DepStage（跨 DLL 字符串寻址） |

#### 嵌套类型

| 类型 | 说明 |
|------|------|
| `FDependency { Name, Stage }` | 依赖元组：dep 名 + dep 的 stage 接口 type_index |
| `FDependencyTable` | `map<我的 stage type_index, vector<FDependency>>` |

### FLayer<TPipelines...> <class>

装配语法糖——把 `FLayerBase`（身份 + 依赖）与一个或多个 `IPipeline`（有序 stage）绑成一个层类型。`FLayerBase` 与 `TPipelines...` 无继承关系，调度时经 `dynamic_cast` 侧向转换。

#### 用法

```cpp
class FWorld : public FLayer<IPipeline<IMain, IShutdown>> {};
class FWorldMulti : public FLayer<IEngineTickPipeline, IEngineInitPipeline> {};
```

### MAHO_DECLARE_LAYER(LayerType, DLL) <宏>

层声明糖——生成 `StaticName()` + `GetName()` + `CreateLayer()` + `GetModulePath()`。名字来自类型名字符串化（`#LayerType`），依赖声明用同一类型推导，拓扑键自洽：

```cpp
#define MAHO_DECLARE_LAYER(LayerType, DLL)             \
    static constexpr std::string_view StaticName()     \
    { return #LayerType; }                             \
    std::string_view GetName() const override          \
    { return StaticName(); }                           \
    static Maho::FLayerBase* CreateLayer()             \
    { return new LayerType(); }                        \
    static std::string_view GetModulePath()            \
    { return DLL; }
```

## LayerTaskGraph.h

### FLayerTaskGraph<TStages, TContext = FEmptyContext> <class : FTaskGraph>

一组匿名 `FLayer*` → 编译 → 执行。`TStages` 是 `TTypeList<StageInterface...>`；每个传入层沿管线展开成**每阶段一个节点**：
- **自推进**：阶段 N 依赖同层阶段 N-1
- **跨对象依赖**：层在该阶段声明的 `AddDependency` 元组

然后 Compile 接线；Execute 把每个就绪节点经自由函数 `Invoke<TStage, TContext>(Layer, Context)` 派发。

#### 接口

| 签名 | 说明 |
|------|------|
| `FLayerTaskGraph(FThreadPool&, TContext&)` | 绑定线程池 + 执行上下文（引用，不拷贝） |
| `void Init(std::vector<FLayerBase*>)` | 重建节点集（公开、可重复调用，每帧/重配） |
| `bool Compile()` | 接线 + 环/缺依赖检测 |
| `void Execute()` | 异步拓扑分派（继承自 FTaskGraph） |
| `void Flush()` | 阻塞到排空（继承自 FTaskGraph） |

示例：

```cpp
using FTickStages = TTypeList<IBeginFrame, ITick, IEndFrame, IExit>;
FLayerTaskGraph<FTickStages, FEngineBase> G(Pool, Engine);
G.Init(Engine.Select<IBeginFrame, ITick, IEndFrame, IExit>());
if (G.Compile()) { G.Execute(); G.Flush(); }
```

## LayerCollector.h

### FLayerCollector<TContext> <class : FQuery<FLayerBase>>

层集合管理基类——拥有 + 调度一组匿名 `FLayerBase`。安装/卸载记录进 pending 集，在 `FlushPendingUpdatePipelines` 安全点应用；卸载依赖安全（反向计数最小堆贪心）。`TContext` 是每个 stage 方法收到的调度上下文（引擎是 `FEngineBase`，渲染子系统是 `FRender`），同时充当 FQuery 数据源。

#### 接口

| 签名 | 说明 |
|------|------|
| `const vector<unique_ptr<FLayerBase>>& GetLayers() const` | 活动层实例（只读） |
| `void Install(FLayerBase*)` | 注册层实例（下帧安全点生效） |
| `void Install(string_view DllPath, const char* FactorySymbol = "CreateLayer")` | 经 FAssembly 动态加载层 DLL 并安装（下帧安全点生效） |
| `void RequestUninstall(FLayerBase*)` | 卸载请求（无条件入 pending） |
| `void TryUninstall(string_view LayerName)` | 匿名卸载（按 `GetName()` 查第一匹配） |
| `template<TInitStages...> void FlushPendingUpdatePipelines() protected` | 应用挂起安装（驱动 Init 阶段）+ 挂起卸载（驱动 Shutdown 阶段） |

卸载算法：`RebuildReverseDeps` 重建反向依赖计数（层名 → 被依赖次数），`FlushUnload` 用**最小堆贪心**——被依赖的层拒绝卸载，依赖者先弹出并链式卸载。

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

主循环把它们分三组驱动：Init 组 `TTypeList<IPreInit, IInit, IPostInit>`、Tick 组 `TTypeList<IBeginFrame, ITick, IEndFrame, IExit>`、Shutdown 组 `TTypeList<IPreShutdown, IShutdown, IPostShutdown>`。`Engine.h` 为每组定义了 `MAHO_DECLARE_STAGE_DISPATCH(FEngineBase, ...)` 全特化。

### FEngineBase <class : FLayerCollector<FEngineBase>>

引擎基类——命令行 KV + 主循环 + feature 所有权（`FLayerCollector`）。入口插件继承它并导出 `CreateEngine()`；它是唯一宿主。

#### 接口

| 签名 | 说明 |
|------|------|
| `FEngineBase()` / `virtual ~FEngineBase()` | 构造 / 析构 |
| `virtual void ParseCommandLine(int Argc, char** Argv)` | 解析 `-key`/`--key=value` 等命令行 |
| `virtual void PreMain() = 0` | 入口钩子：安装引擎服务层（Log/Config/...） |
| `virtual void PostMain() = 0` | 出口钩子 |
| `virtual int Main()` | 主循环：Init 图 → Tick 循环 → Shutdown 图 |
| `bool Has(string_view Key) const` | 命令行是否有该 key |
| `string Get(string_view Key) const` | key 的值（缺省空串） |
| `bool GetBool(string_view Key) const` | 值是否 "true/1/yes/on" |
| `int GetInt(string_view Key) const` | `stoi` 解析（失败回 0） |
| `const map<string, string>& GetAll() const` | 全部 KV 对 |
| `void RequestExit()` | 请求主循环下一帧边界退出 |

### MAHO_DECLARE_ENGINE(EngineType, DLL) <宏>

引擎类声明糖——生成 `CreateEngine()` 工厂 + `GetModulePath()`：

```cpp
#define MAHO_DECLARE_ENGINE(EngineType, DLL)          \
    static Maho::FEngineBase* CreateEngine()          \
    { return new EngineType(); }                      \
    static std::string_view GetModulePath()           \
    { return DLL; }
```

## EntryPoint.h

### Maho::Main(int Argc, char** Argv) <function>

统一应用驱动——安装引擎 DLL 并执行其根实例：

```text
main()/WinMain() -> Maho::Main(Argc, Argv)
  InstallFatalHandlers()
  FAssembly Load(argv[1] ?? MAHO_ENGINE_NAME "Engine.dll")   // 安装
  GetProcAs<CreateFunction>("CreateEngine") -> FEngineBase*  // 创建根实例（匿名）
  ParseCommandLine -> PreMain -> Main -> PostMain             // 对称生命周期
  delete App
```

平台入口（Windows WinMain/main、Android `android_main`、iOS/Linux/Xbox main）都收敛到这里。

- [EngineDoc.md](EngineDoc.md) — 概念 · [实现字典](../../Private/Engine/EngineAPI.md) — 算法 · [Core API](../Core/CoreAPI.md) — 基建
