# Engine

## Code Files

- [Layer.h](Layer.h) -- anonymous layer + stage pipeline + task graph (header-only: `FLayerBase` / `FLayer` / `FLayerTaskGraph` / `MAHO_DECLARE_LAYER`)
- [Engine.h](Engine.h) -- engine main loop (`IBeginFrame` / `ITick` / `IEndFrame` / `IEnginePipeline` / `FEngineLayer` / `FEngineBase`)

## Concept -- Anonymous Layer + Ordered Stage Pipeline

The Engine layer is header-only (templates + inline, no .cpp). It defines the layer system: a "layer" is an **installable runtime node schedulable by the task graph**, with anonymous identity (only a name), whose behavior is determined by the stage sequence (pipeline) it implements.

Unlike the old "layer nests layer recursive startup" approach, this architecture **does not self-close its lifecycle**: a layer only declares who it is and who it depends on at each stage; global scheduling (order, parallelism, missing-dependency detection) is uniformly handled by `FLayerTaskGraph`.

### 1. FLayerBase -- Anonymous Anchor

```cpp
class FLayerBase
{
    virtual std::string_view GetName() const = 0;   // stable identity name (topology key of the task graph)
    const FDependencyTable& GetDependencies() const; // dependency table for each stage
protected:
    template <typename TMyStage, typename TDepObj, typename TDepStage>
    void AddDependency();   // declares: this depends on TDepObj at TDepStage while at TMyStage
};
```

- A layer **closes over only itself**: identity (`GetName`) + per-stage dependencies (`AddDependency`).
- **Does not manage dependency lifetimes** -- whether the execution context is complete is guaranteed by `FLayerTaskGraph`.
- Dependency table structure: `map<my stage interface type_index, vector<{dep name, dep stage type_index}>>`.

### 2. IPipeline -- Ordered Stage Sequence (Core/Interface.h)

```cpp
template <typename... TStageTypes>
class IPipeline : public virtual TStageTypes...
{
    using TStages = TTypeList<TStageTypes...>;   // ordered stage list
};
```

- Template parameter order = the layer's own node order: `IPipeline<IInit, IMain, IShutdown>` means nodes expand as Init -> Main -> Shutdown (automatic self-progression edges).
- `IPipeline` **only carries the stage list**; the stage -> method call `Invoke` protocol is implemented by the **concrete pipeline class**.

### 3. FLayer -- Assembly Sugar

```cpp
template <typename TPipeline>
class FLayer : public FLayerBase, public TPipeline {};
```

`FLayer<IPipeline<IMain, IShutdown>>` == `FLayerBase` + `IPipeline<IMain, IShutdown>`. A layer inherits both at once (no inheritance relation between them); scheduling performs a lateral conversion via `dynamic_cast`.

### 4. FLayerTaskGraph -- Global Scheduling

```cpp
template <typename TPipeline, typename TContext = FEmptyContext>
class FLayerTaskGraph : public FTaskGraph;
```

A set of anonymous `FLayer*` -> compile -> execute. **Contract**: every layer passed in must implement the same `TPipeline`.

- **Construct** `(FThreadPool&, TContext&)`: only binds the thread pool + execution context (references, not copied).
- **Init(vector<FLayerBase*>)**: public, repeatable (rebuilds nodes each frame / on reconfiguration).
- **Compile()**: wiring + cycle / missing-dependency detection.
- **Execute()/Flush()**: asynchronous dispatch + barrier finish.

Each layer expands into **one node per stage**:

1. **Self-progression**: stage N depends on stage N-1 of the same layer (guarantees intra-layer order).
2. **Cross-object dependencies**: the dependency tuples the layer declares via `AddDependency`.

Dispatch uses `dynamic_cast<TPipeline&>(*Layer).Invoke<TCurrent>(Context)` -- runtime stage type_index matches compile-time type.

### 5. FTaskGraph -- Dependency Graph Scheduler (Core/TaskGraph.h)

Node = `(object name, stage)` pair. Edges come from each node's dependency tuples. A node becomes schedulable immediately after **all its direct dependencies complete** (stage-independent -- no stage barrier, supports cross-stage pipelining).

Lifecycle: `Init` (load topology) -> `Compile` (wiring + validation) -> `Execute` (async topology dispatch) -> `Flush` (block until drained).

## Engine Main Loop (Engine.h)

### Three-Stage Stages

```cpp
class IBeginFrame { virtual void BeginFrame() = 0; };
class ITick       { virtual void Tick()       = 0; };
class IEndFrame   { virtual void EndFrame()   = 0; };
```

### IEnginePipeline -- Fixed Pipeline

```cpp
class IEnginePipeline : public IPipeline<IBeginFrame, ITick, IEndFrame>
{
    template <typename TStage, typename TContext>
    void Invoke(TContext& Engine);   // if-constexpr: stage -> BeginFrame/Tick/EndFrame
};
```

### FEngineLayer -- Engine Feature Base Class

```cpp
class FEngineLayer : public FLayer<IEnginePipeline> {};
```

Business features inherit it and implement the three stage methods; cross-feature dependencies call `AddDependency<ITick, FOther, IBeginFrame>()` in the constructor.

### FEngineBase -- Engine Base Class

```cpp
class FEngineBase : public IPlugin<IInit, IMain, IExit, IShutdown>
{
    int Main() final override;   // main loop: Flush -> apply pending changes -> Init -> Compile -> Execute
protected:
    void Install(FEngineLayer*);          // takes effect next frame
    void TryUninstall(std::string_view);  // anonymous uninstall (by layer name)
};
```

`FEngineBase` is the sole anchor exported by the entry plugin (`MAHO_DECLARE_ENGINE` + `extern "C" CreateEngine()` bridge). `EntryPoint` looks up `"CreateEngine"` via `FAssembly` -> `FEngineBase*` -> `Initialize/Main/Shutdown`. It owns the feature instances + DLLs (`unique_ptr` containers), not a pure interface.

## Plugin Macros

```cpp
MAHO_DECLARE_LAYER(FWorld)
// expands to: static constexpr std::string_view StaticName() { return "FWorld"; }
//      + std::string_view GetName() const override { return StaticName(); }
// The name comes from stringifying the type name; dependency declarations use the same type deduction, keeping topology keys self-consistent.

MAHO_DECLARE_ENGINE(FMyApp, "MyApp.dll")
// expands to: static Maho::FEngineBase* CreateEngine() { return new FMyApp(); }
//      + static std::string_view GetModulePath() { return "MyApp.dll"; }
```

## Dependency / Linking / Include Rules

**Link direction (`.cplugin` Dependencies / CMake `target_link_libraries`) -- layered one-way, arrow = linked target**:

```
engine core (Maho)  <--- engine plugins (each engine plugin links Maho)
engine core + engine plugins  <--- project core (entry plugin, links Maho + all mounted engine plugins)
engine core + engine plugins + project core  <--- project plugins (link Maho, get upper-layer includes transitively via .cplugin)
```

- **Engine plugins**: `.cplugin Dependencies` only declares same-layer plugin dependencies; engine core is auto-linked by codegen.
- **Project core (entry plugin)**: links engine core + all mounted engine plugins + project plugins; it is the sole host (inherits `FEngineBase` and exports `CreateEngine()`).
- **Project plugins**: `.cplugin Dependencies` declares parent (project core) + engine plugin dependencies; get engine core includes transitively.

**Include direction (compile time)**:

- **Engine core <-> engine plugins**: **one-way** -- engine plugins include engine core (`<Maho.h>`/`<Layer.h>`/`<Engine.h>`); engine core makes zero app assumptions and includes no plugins.
- **Project core <-> project plugins**: **two-way** -- project core includes project plugin headers (feature type references), project plugins include project core headers (child -> parent, via .cplugin). Include paths are added by codegen for all mounted plugin Public/.

**Acyclic guarantee**: build dependencies (`.cplugin`) stay layered one-way (child -> parent); parent including child is only a compile-time type reference and does not form a build cycle.

## Lifecycle

`FLayer`/`FEngineLayer` do not force `IInit/IShutdown` -- lifecycle capabilities are composed via `IPlugin<...>` (Core/Interface.h) and invoked explicitly by the user in the driver loop. `FEngineBase` is the only base class with a complete lifecycle (IInit/IMain/IExit/IShutdown). Destructors do not silently teardown.

## Related Docs

- [EngineAPI.html](EngineAPI.html) -- API docs
- [../Core/CoreDoc.md](../Core/CoreDoc.md) -- Core infrastructure concepts
- [../PublicDoc.md](../PublicDoc.md) -- Public root
