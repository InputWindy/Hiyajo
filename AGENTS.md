# Maho - Agent Entry (Engine Core)

All AI agents must read this file before entering this engine.

## Design Constraints (strict)

- This engine is **pure scaffolding** - zero app assumptions, zero third-party dependencies, zero stage presets. Every concrete function is an installable plugin.
- **Layers**:
  - **Core** (`Source/Public/Core/`): type-agnostic infrastructure blocks - `TypeList` (TTypeList + operations), `Delegate` (TMulticastEvent), `Singleton` (TSingleton marker base), `Interface` (IPlugin/IPipeline capability composers), `TaskGraph` (FTaskGraph dependency-graph scheduling), `ThreadPool`, `ThreadedServer`, `Assembly` (FAssembly DLL loading), `Fatal`.
  - **Engine** (`Source/Public/Engine/`): layer system - `Layer.h` (`FLayerBase`/`FLayer`/`Invoke` dispatch/`MAHO_DECLARE_LAYER`) + `LayerTaskGraph.h` (`FLayerTaskGraph` layer→graph expansion) + `LayerCollector.h` (`FLayerCollector` install/uninstall) + `Engine.h` (10 stage interfaces + `FEngineBase`). **Engine only assembles Core infrastructure, contains no concrete service**.
  - **Plugins** (`Plugins/`): installable plugins - `Common/` (service plugins: TSingleton or pure libraries). The engine has zero app assumptions, all logic lives in plugins.
- **FLayerBase = anonymous layer anchor**: closes over only itself - `GetName()` (stable identity name) + `GetDependencies()` (per-stage dependency table). **Does not manage dependency lifetimes**; execution-context integrity is guaranteed by `FLayerTaskGraph`.
- **FLayer<TPipeline>**: assembly sugar - `FLayerBase` + `IPipeline` (ordered stage list). A layer inherits both (no inheritance relation between them), with `dynamic_cast` side conversion at scheduling time.
- **IPipeline<TStageTypes...>**: ordered stage sequence (`TStages = TTypeList<...>`). Only carries the stage list; the stage-to-method `Invoke` protocol is implemented by the **concrete pipeline class**.
- **FLayerTaskGraph<TPipeline, TContext>**: a set of anonymous `FLayer*` -> compile -> execute. Each layer expands into one node per stage (self-advancing + cross-object dependency), scheduled topologically.
- **FEngineBase**: engine base class - lifecycle capabilities (IInit/IMain/IExit/IShutdown) + main loop + feature ownership (`unique_ptr` container). The entry plugin is the only host, exports `CreateEngine()`.
- **Optional capability composition**: a layer mounts only the stage interfaces it implements (unimplemented stages are silently skipped by `dynamic_cast` in the dispatch specialization). Not every object needs a lifecycle. `TSingleton<T>` is a pure marker base, no forced interface.
- **Singleton process-unique**: `static T& Get()` declared in the Public header, defined in the Private cpp (compiled into that plugin DLL) - the instance is process-unique within its own DLL. Cross-DLL via dllimport symbol, not header inline (avoid one copy per DLL).
- **`.cplugin` `Dependencies`**: build-level (compile target + include), does not fill the FLayer template. Runtime dependencies are declared by the layer itself via `WaitFor`/`BlockOn` (each has a typed template form and an anonymous name form).
- **Lifecycle owned by host**: `FEngineBase::Shutdown` releases all features + DLLs. Feature destructors do not silently teardown; init/shutdown are explicitly driven by the user through the `IInit`/`IShutdown` capabilities.

## Dependency / Link / Include Rules (strict)

**Link direction (`.cplugin` Dependencies / CMake `target_link_libraries`) - layered one-way, arrow = linked target**:

```
engine core (Maho)  <--  engine plugins (each engine plugin links Maho)
engine core + engine plugins  <--  project core (entry layer, links Maho + all mounted engine plugins)
engine core + engine plugins + project core  <--  project plugins (link Maho, get upper include via .cplugin transitivity)
```

- **Engine plugins**: `.cplugin Dependencies` only declare same-layer plugin dependencies (e.g. Asset->Paths, Resource->Name+Paths, World->AI); the engine core is auto-linked by codegen (`target_link_libraries({name} PUBLIC Maho)`).
- **Project core (entry layer)**: links engine core + all mounted engine plugins + project plugins. It is the only host (inherits FEngineBase and exports `CreateEngine()`).
- **Project plugins**: `.cplugin Dependencies` declare the parent (project core) + engine plugin dependencies; get engine-core include via transitivity.

**Include direction (compile-time) - bidirectional rule**:

- **Engine core <-> engine plugins**: **one-way** - engine plugins include engine core (`<Maho.h>`/`<Layer.h>`), **engine core has zero app assumptions and includes no plugin** (pure scaffolding).
- **Project core <-> project plugins**: **bidirectional** - project core includes project plugin headers (`FLayer<FTestProjectPlugin>` template parameter references the subtype), project plugins include project core headers (child->parent, via .cplugin). Include paths are added by codegen for all mounted plugin Public/ dirs (`dep_public_dirs`).

**No-cycle guarantee**: build dependencies (`.cplugin`) stay layered one-way (child->parent); parent includes child only as compile-time type references (entry include path contains all mounted plugins), forming no build cycle.

## Interface Layering

**Read interfaces public, capability/write interfaces public** (no "host-only writable" write protection). Thread safety is guaranteed internally by each object (locks/queues).

- **TSingleton services**: `Get()` process-unique (header declaration + cpp definition); lifecycle optionally composed via `IPlugin<IInit,IShutdown>`.
- **Pure libraries** (Archive/Compress/Unicode): free functions/classes, no singleton, no state, no lifecycle.
- **服务层（FLayer 派生）**: install/uninstall via `FLayerCollector`/`FEngineBase`'s `Install`/`TryUninstall` (pending set, applied at the next-frame safe point `FlushPendingUpdatePipelines`).

## Driving Mechanism

- **Dependency-graph scheduling** (FTaskGraph): node = (object name, stage) pair, edges from dependency tuples. A node is immediately schedulable after all its direct dependencies complete (no stage barrier, cross-stage pipeline). `Init` -> `Compile` (wiring + validation) -> `Execute` (async topological dispatch) -> `Flush` (barrier).
- **Thread pool** (FThreadPool): `Submit` enqueues and returns immediately, `Flush` blocks until all submitted tasks complete.
- **Compile-time filtering** (TQuery): `Query<TList>().Select<...>().With<...>().Not<...>().FResult` produces the filtered type list.
- **Layer expansion** (FLayerTaskGraph::ExpandLayer): each layer expands along the `IPipeline` stage sequence into one node per stage; self-advancing (stage N depends on stage N-1 of the same layer) + cross-object dependency (the layer's declared `WaitFor` edges) + reverse dependency (the layer's declared `BlockOn` edges, applied by the graph at Init).

## Synchronization & Shutdown Principles (module self-consistency)

The engine's design intent: **each module must be self-consistent (自洽) and provide reliable services upward**. The plugin architecture exists so every module owns its behavior. A bug -- especially a race -- is the owning module's responsibility. **Never fix a module's bug by silently modifying another module** to accommodate it. These are the hard-won debugging heuristics (validated 2026-09-01 on the render pipeline + shutdown):

- **Cross-module synchronization IS the dependency graph.** A dependency edge orders "writer before reader". If two modules access the same memory concurrently WITHOUT a dependency, they must both be READING (read-read is safe). Any WRITE to shared memory between modules implies a data-flow relationship, so they must be ordered -- **a cross-module write race means a missing dependency edge (`WaitFor`/`BlockOn`)**, nothing else.
- **Dependency direction is declared by the CONSUMER.** `WaitFor<MyStage, Other, OtherStage>` = "my MyStage waits for Other" (I depend on a service). `BlockOn<Other, OtherStage, MyStage>` = "Other@OtherStage is blocked on my MyStage" (Other must run AFTER me -- e.g. "the Log layer must outlive my teardown"). Both are the same graph edge declared from the side that knows it. A producer (e.g. FLog) does NOT enumerate its consumers; each consumer declares its own need. Both forms exist as typed templates (consumer knows the producer's type) and anonymous name forms (dynamically-loaded plugins referencing each other without a header include).
- **The graph honors only declared edges -- there is no stage barrier.** Cross-stage ordering must be explicit. Example: every render feature's `IBeginRender` must depend on the Frame feature's `IFrameBegin`, or `ReleaseFrameLists` races the features' list acquisition (validation: `vkFreeCommandBuffers is in use`).
- **A layer's private async workers (own pool, threaded servers) are NOT graph-visible.** The graph schedules stages, not a layer's internal tasks. The layer must **self-close**: drain ALL its async work at the START of its own `Shutdown` (let the queues drain naturally, block until empty). This is the layer's contract with the scheduler -- failing it is the layer's bug, not a missing edge.
- **Shutdown is just the next frame.** A layer's `Shutdown` is the next scheduling unit that drains the previous frame's work -- structurally identical to the next `Tick`'s leading Flush. If you can Flush at the start of the next Tick, you can Flush at the start of Shutdown.
- **The shutdown transition's environment must stay alive -- express it as dependencies.** Just as the engine keeps Log/surface alive across normal frame transitions, the shutdown transition must too. This is a dependency, not a patch:
  - `FPlatform.IShutdown → FRender.IShutdown`: the RHI's surface is created from Platform's window, so the surface must outlive FRender's render teardown (symmetric with init's `FRender.IInit → FPlatform.IPostInit`). Without it, leftover present/recreate races a dying surface → garbage-capabilities validation errors.
  - `FLog.IShutdown → FRender.IShutdown`: the log outlives every layer that may log during teardown (so teardown logging never hits `GetLog()==null`).
  - Init-side: any layer that logs during `IInit` depends on `FLog.IInit` (the init graph is concurrent too).
- **The RHI is a stateless async processor.** It receives tasks and processes them; it never refuses work. Gating/refusing is application-layer logic -- do not add "exiting"-style flags to the RHI; drain the tasks instead.
- **`FThreadPool::Flush` is a quiescence barrier** (waits `PendingCount==0 && Queue.empty()`), tolerating concurrent `Submit` from nested graphs. The old FIFO no-op barrier leaks tasks dispatched by nested graphs (e.g. the render graph's dynamic downstreams) and must not be reintroduced.

## Project-Side Development Constraints (strict)

When extending project-side code, follow these three rules:

### 1 Interface Definition and Implementation Separation

- **Define interfaces**: write all of them under the project entry plugin's `Public/` directory, organized into folders by function.
- **Implement interfaces**: create a new plugin **outside** the entry plugin, implement in its own `Private/`.
- The entry plugin **does not care how any interface is implemented**, it only schedules.

### 2 Create Code with Tools

- For new plugins, call `CreatePlugin.bat` / `Tools/create_plugin_ui.py` to auto-create, **do not hand-write directories/`.cplugin`**.
- The tool generates `Public/` + `Private/` + `.cplugin`, and automatically adds the parent plugin (the project anchor) to `Dependencies`; **other dependency plugins are hand-filled in `Dependencies`** — the UI no longer offers a selection tree.

### 3 Install Third-Party Plugins as a Whole into Extension

- Install external third-party plugin packages **as a whole into project-side `Extension/`**; include + DLL target are auto-added at build time.

**Hourglass dependency**: engine -> project entry plugin -> feature sub-plugins. The entry plugin is the only host (inherits FEngineBase and exports `CreateEngine()`).

## Docs

- [Docs.md](Docs.md) - full repository documentation index
- [Source/SourceDoc.md](Source/SourceDoc.md) - source root (layers)
- [Source/Public/Core/CoreDoc.md](Source/Public/Core/CoreDoc.md) - Core infrastructure concepts
- [Source/Public/Engine/EngineDoc.md](Source/Public/Engine/EngineDoc.md) - layer architecture (FLayerBase/FLayer/FLayerTaskGraph/main loop)
- [Source/Public/PublicDoc.md](Source/Public/PublicDoc.md) - Public root
