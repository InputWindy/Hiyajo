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
- **`.cplugin` `Dependencies`**: build-level (compile target + include), does not fill the FLayer template. Runtime dependencies are declared by the layer itself via `AddDependency` (compile-time template or string addressing).
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
- **Layer expansion** (FLayerTaskGraph::ExpandLayer): each layer expands along the `IPipeline` stage sequence into one node per stage; self-advancing (stage N depends on stage N-1 of the same layer) + cross-object dependency (the layer's declared AddDependency tuples).

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
