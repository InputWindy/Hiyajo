# Engine

## 代码文件

- [Layer.h](Layer.h) — 匿名层 + stage 管线 + 任务图（header-only：`FLayerBase` / `FLayer` / `FLayerTaskGraph` / `MAHO_DECLARE_LAYER`）
- [Engine.h](Engine.h) — 引擎主循环（`IBeginFrame` / `ITick` / `IEndFrame` / `IEnginePipeline` / `FEngineLayer` / `IEngine`）

## 概念——匿名层 + 有序 stage 管线

Engine 层 header-only（模板 + 内联，无 .cpp）。它定义层体系：一个「层」是**可安装、可被任务图调度的运行时节点**，身份匿名（只有名字），行为由它实现的 stage 序列（pipeline）决定。

与旧「layer 套 layer 递归拉起」不同，本架构**不自闭环生命周期**：层只声明自己是谁、在每个 stage 依赖谁；全局调度（谁先谁后、并行、缺依赖检测）由 `FLayerTaskGraph` 统一负责。

### ① FLayerBase —— 匿名锚点

```cpp
class FLayerBase
{
    virtual std::string_view GetName() const = 0;   // 稳定身份名（任务图的拓扑键）
    const FDependencyTable& GetDependencies() const; // 每个 stage 的依赖表
protected:
    template <typename TMyStage, typename TDepObj, typename TDepStage>
    void AddDependency();   // 声明：this 在 TMyStage 依赖 TDepObj 在 TDepStage
};
```

- 层**只闭合自己**：身份（`GetName`）+ 逐 stage 依赖（`AddDependency`）。
- **不管理依赖的生命周期**——执行上下文是否完整由 `FLayerTaskGraph` 保证。
- 依赖表结构：`map<我的 stage 接口 type_index, vector<{dep 名, dep 的 stage type_index}>>`。

### ② IPipeline —— 有序 stage 序列（Core/Interface.h）

```cpp
template <typename... TStageTypes>
class IPipeline : public virtual TStageTypes...
{
    using TStages = TTypeList<TStageTypes...>;   // 有序 stage 列表
};
```

- 模板参数顺序 = 该层自己的节点顺序：`IPipeline<IInit, IMain, IShutdown>` 表示节点按 Init → Main → Shutdown 展开（自动自递进边）。
- `IPipeline` **只携带 stage 列表**；stage → 方法调用的 `Invoke` 协议由**具体 pipeline 类**实现。

### ③ FLayer —— 装配语法糖

```cpp
template <typename TPipeline>
class FLayer : public FLayerBase, public TPipeline {};
```

`FLayer<IPipeline<IMain, IShutdown>>` == `FLayerBase` + `IPipeline<IMain, IShutdown>`。层同时继承两者（无继承关系），调度时经 `dynamic_cast` 侧向转换。

### ④ FLayerTaskGraph —— 全局调度

```cpp
template <typename TPipeline, typename TContext = FEmptyContext>
class FLayerTaskGraph : public FTaskGraph;
```

一组匿名 `FLayer*` → 编译 → 执行。**契约**：传入的每个层必须实现同一 `TPipeline`。

- **构造** `(FThreadPool&, TContext&)`：只绑定线程池 + 执行上下文（引用，不拷贝）。
- **Init(vector<FLayerBase*>)**：公开、可重复调用（每帧/重配重建节点）。
- **Compile()**：接线 + 环/缺依赖检测。
- **Execute()/Flush()**：异步分派 + barrier 收尾。

每层展开成**每 stage 一个节点**：

1. **自递进**：stage N 依赖同层 stage N-1（保证层内顺序）。
2. **跨对象依赖**：层自己声明的 `AddDependency` 元组。

分派时 `dynamic_cast<TPipeline&>(*Layer).Invoke<TCurrent>(Context)`——运行时 stage type_index 匹配编译期类型。

### ⑤ FTaskGraph —— 依赖图调度器（Core/TaskGraph.h）

节点 = `(对象名, stage)` 对。边来自每个节点的依赖元组。节点在其**所有直接依赖完成后**立即可调度（stage 无关——无 stage barrier，支持跨 stage 流水线）。

生命周期：`Init`（载入拓扑）→ `Compile`（接线 + 校验）→ `Execute`（异步拓扑分派）→ `Flush`（阻塞到排空）。

## 引擎主循环（Engine.h）

### 三阶段 stage

```cpp
class IBeginFrame { virtual void BeginFrame() = 0; };
class ITick       { virtual void Tick()       = 0; };
class IEndFrame   { virtual void EndFrame()   = 0; };
```

### IEnginePipeline —— 固定管线

```cpp
class IEnginePipeline : public IPipeline<IBeginFrame, ITick, IEndFrame>
{
    template <typename TStage, typename TContext>
    void Invoke(TContext& Engine);   // if-constexpr: stage → BeginFrame/Tick/EndFrame
};
```

### FEngineLayer —— 引擎 feature 基类

```cpp
class FEngineLayer : public FLayer<IEnginePipeline> {};
```

业务 feature 继承它，实现三 stage 方法；跨 feature 依赖在构造函数里 `AddDependency<ITick, FOther, IBeginFrame>()`。

### IEngine —— 引擎锚点

```cpp
class IEngine : public IPlugin<IInit, IMain, IShutdown>
{
    int Main() final override;   // 主循环：Flush → 应用挂起变更 → Init → Compile → Execute
protected:
    void Install(FEngineLayer*);    // 下帧生效
    void Uninstall(FEngineLayer*);
};
```

`IEngine` 是入口插件导出的唯一锚点（`MAHO_DECLARE_ENGINE` + `extern "C" CreateEngine()` bridge）。`EntryPoint` 经 `FAssembly` 查 `"CreateEngine"` → `IEngine*` → `Initialize/Main/Shutdown`。

## 插件宏

```cpp
MAHO_DECLARE_LAYER(FWorld)
// 展开：static constexpr std::string_view StaticName() { return "FWorld"; }
//      + std::string_view GetName() const override { return StaticName(); }
// 名字取自类型名字符串化，依赖声明用同一类型推导，拓扑键自洽。

MAHO_DECLARE_ENGINE(FMyApp, "MyApp.dll")
// 展开：static Maho::IEngine* CreateEngine() { return new FMyApp(); }
//      + static std::string_view GetModulePath() { return "MyApp.dll"; }
```

## 依赖 / 链接 / include 规则

**链接方向（`.cplugin` Dependencies / CMake `target_link_libraries`）——分层单向，箭头 = 被链接方**：

```
引擎核心(Maho)  ←── 引擎插件（每个引擎插件 link Maho）
引擎核心 + 引擎插件  ←── 项目核心（入口插件，link Maho + 全部挂载引擎插件）
引擎核心 + 引擎插件 + 项目核心  ←── 项目插件（link Maho，经 .cplugin 传递获得上层 include）
```

- **引擎插件**：`.cplugin Dependencies` 仅声明同层插件依赖；引擎核心由 codegen 自动 link。
- **项目核心（入口插件）**：link 引擎核心 + 全部挂载引擎插件 + 项目插件；是唯一宿主（继承 `IEngine` 并导出 `CreateEngine()`）。
- **项目插件**：`.cplugin Dependencies` 声明父（项目核心）+ 引擎插件依赖；经传递获得引擎核心 include。

**include 方向（编译期）**：

- **引擎核心 ↔ 引擎插件**：**单向**——引擎插件 include 引擎核心（`<Maho.h>`/`<Layer.h>`/`<Engine.h>`），引擎核心零 app 假设、不 include 任何插件。
- **项目核心 ↔ 项目插件**：**双向**——项目核心 include 项目插件头（feature 类型引用），项目插件 include 项目核心头（子→父，经 .cplugin）。include 路径由 codegen 加所有挂载插件 Public/。

**无环保证**：构建依赖（`.cplugin`）分层单向（子→父）；父 include 子只是编译期类型引用，不构成构建环。

## 生命周期

`FLayer`/`FEngineLayer` 不强制 `IInit/IShutdown`——需要生命周期的能力经 `IPlugin<...>` 组合（Core/Interface.h），由用户在驱动循环里显式调用。`IEngine` 是唯一拥有完整生命周期（IInit/IMain/IShutdown）的锚点。析构不静默 teardown。

## 相关文档

- [EngineAPI.html](EngineAPI.html) — API 文档
- [../Core/CoreDoc.md](../Core/CoreDoc.md) — Core 基建概念
- [../PublicDoc.md](../PublicDoc.md) — Public 根
