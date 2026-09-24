# Maho - Agent Entry (Engine Core)

All AI agents must read this file before entering this engine.

## Design Constraints (strict)

- This engine is **pure scaffolding** - zero app assumptions, zero third-party dependencies, zero stage presets. Every concrete function is an installable plugin.
- **Layers**:
  - **Core** (`Source/Public/Core/`): type-agnostic infrastructure blocks - `TypeList` (TTypeList + operations), `Delegate` (TMulticastEvent), `Singleton` (TSingleton marker base), `Interface` (IPlugin/IPipeline capability composers), `FrameGraph` (`FFrameGraph` scheduler + `FFrameExtension` declaration layer + `FFrameBridge`), `ThreadPool`, `ThreadedServer`, `Assembly` (FAssembly DLL loading), `Fatal`.
  - **Engine** (`Source/Public/Engine/`): the engine side of the frame system - `Frame.h` (`Invoke` dispatch + `TFrameDispatch` + `MAHO_DECLARE_FRAME`) + `FrameBuilder.h` (`FFrameBuilder`: install/uninstall + the frame loop; it is the PRIVATE owner of `FFrameGraph`/`FFrameBridge`/`TFrameDispatch`) + `Engine.h` (10 stage interfaces + `FEngineBase`). **Engine only assembles Core infrastructure, contains no concrete service**.
  - **Plugins** (`Plugins/`): installable plugins - `Common/` (service plugins: TSingleton or pure libraries). The engine has zero app assumptions, all logic lives in plugins.
- **FFrameExtension = the declaration layer**: closes over only itself - `GetName()` (stable identity, a string LITERAL, which is what makes the identity key safe) + the edges it declares (`WaitFor`/`BlockOn` sugar, name-addressed form included). It knows NOTHING about a stage sequence: that belongs to the engine side. **Does not manage dependency lifetimes.**
- **A frame = two bases**: `FFrameExtension` + `IPipeline<TStages...>` (ordered stage list). A frame inherits both (no inheritance relation between them), with `dynamic_cast` side conversion at dispatch time.
- **IPipeline<TStageTypes...>**: ordered stage sequence (`TStages = TTypeList<...>`). Only carries the stage list; the stage-to-method `Invoke` protocol is implemented per concrete scheduling context.
- **FFrameGraph** (`Core/FrameGraph.h`): node identity is the TRIPLE `{Name, Stage, Phase}`, where the phase is a RING index mod `MAHO_FRAMES_IN_FLIGHT`. Two driving verbs: `Submit(batch)` (structural validation + ADMISSION, which blocks the CALLER until the phases the batch names are idle) and `Wait()` (a submission FENCE: block until everything submitted so far has completed). `FFrameBridge::Build(frame set, stage sequence, frame number, dispatch)` turns declarations into that batch and emits the two STRUCTURAL edges (the in-frame stage chain + the per-stage cross-frame self edge); the graph itself emits no edge of its own.
- **FEngineBase**: engine base class - lifecycle capabilities (IInit/IMain/IExit/IShutdown) + main loop + feature ownership (`unique_ptr` container). The entry plugin is the only host, exports `CreateEngine()`.
- **Optional capability composition**: a frame mounts only the stage interfaces it implements; the batch builder asks `TFrameDispatch::Implements` and emits NO node for the rest (there is no no-op node any more). Not every object needs a lifecycle. `TSingleton<T>` is a pure marker base, no forced interface.
- **Singleton process-unique**: `static T& Get()` declared in the Public header, defined in the Private cpp (compiled into that plugin DLL) - the instance is process-unique within its own DLL. Cross-DLL via dllimport symbol, not header inline (avoid one copy per DLL).
- **`.cplugin` `Dependencies`**: build-level (compile target + include), does not fill the `IPipeline` stage list. Runtime dependencies are declared by the frame itself via `WaitFor`/`BlockOn` (each has a typed template form, a name-addressed form, and the `OnLastFrameStage` selector for a cross-frame edge).
- **Lifecycle owned by host**: teardown is `FEngineBase::PostMain` (uninstall sweep) + `~FFrameBuilder`, which drains the graph AND the pool before any instance or module is freed. Feature destructors do not silently teardown; init/shutdown are explicitly driven through the `IInit`/`IShutdown` stages.

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

- **Engine core <-> engine plugins**: **one-way** - engine plugins include engine core (`<Maho.h>`/`<Engine/Frame.h>`), **engine core has zero app assumptions and includes no plugin** (pure scaffolding).
- **Project core <-> project plugins**: **bidirectional** - project core includes project plugin headers (`FFrameExtension` template parameter references the subtype), project plugins include project core headers (child->parent, via .cplugin). Include paths are added by codegen for all mounted plugin Public/ dirs (`dep_public_dirs`).

**No-cycle guarantee**: build dependencies (`.cplugin`) stay layered one-way (child->parent); parent includes child only as compile-time type references (entry include path contains all mounted plugins), forming no build cycle.

## Interface Layering

**Read interfaces public, capability/write interfaces public** (no "host-only writable" write protection). Thread safety is guaranteed internally by each object (locks/queues).

- **TSingleton services**: `Get()` process-unique (header declaration + cpp definition); lifecycle optionally composed via `IPlugin<IInit,IShutdown>`.
- **Pure libraries** (Archive/Compress/Unicode): free functions/classes, no singleton, no state, no lifecycle.
- **服务层（FFrameExtension 派生）**: install/uninstall via `FFrameBuilder`/`FEngineBase`'s `Install`/`TryUninstall` (pending set, applied at the next-frame safe point `FlushPendingUpdates`).

## Export / Module-Boundary Rules (strict)

A type that crosses a DLL boundary has exactly one hard question: **which module's vtable and deleting destructor are inside the instance?** Get it wrong and the failure is a silent `0xC0000005` inside `delete`, not a compile error (validated 2026-09-10: editor crashed on close after an OS-dragged PNG import).

- **Rule**: a type whose instances can outlive the module that constructed them, **and** can be destroyed by a different module, **MUST be exported as a DLL interface** - `MAHO_<NAME>_API` from that plugin's `Public/<Name>Api.h` (toggled by codegen's `MAHO_<NAME>_MODULE_EXPORTS`; `MAHO_EXPORT`/`MAHO_IMPORT` live in `Source/Public/Core/Export.h`).
- **Why the tag is required**: a class with no key function (all-inline, e.g. `explicit FTexture2D(std::string P) : FTexture(std::move(P)) {}`) has its vftable + deleting dtor emitted as a COMDAT **per module**; whichever TU instantiates the ctor stamps *its own* module's vptr into the object. `__declspec(dllimport)` on the class forbids local emission, so every consumer must reference the imported symbol (`__imp_??_7...`) - the hazard becomes a compile error. (GCC/Clang equivalent: a key function, i.e. the first out-of-line virtual.)
- **"Put the instantiation in a .cpp" is not sufficient.** It must be *the owning type's* cpp (`TResourceCreator<T>` specializations are defined out-of-line in the type's own module), and it still misses by-value returns, `vector<T>`/`optional<T>`/`make_shared<T>` inside header templates, caller-side local construction, and lambdas capturing `T`.
- **Project-side derived types follow the same rule.** A project plugin's resource/asset type derived from an engine type must be exported too, or keep its vtable in a module that provably outlives every consumer. An unexported derived type silently re-introduces the bug for the base's export tag.
- **Fix the boundary, never the delete site.** Do not "solve" a vptr-into-freed-image crash by leaking, by clearing a catalog earlier, or by moving the delete into another module - the lifetime belongs to the type, so the missing export tag is the bug.
- Precedent in this repo: `RHIResources.h`/`RDG.h`/`Render.h`/`Scene.h`/`UIFeature.h`/`GameWorld.h`/`ExampleEditor.h` export every class; the resource chain is `MAHO_RESOURCE_API FResource` -> `MAHO_ASSET_API FAssetsResource` -> concrete asset types (see `Plugins/GameEngine/EngineCore/Resource/AGENTS.md`).

## Driving Mechanism

- **Dependency-graph scheduling** (FFrameGraph): node = the triple (name, stage, PHASE), where the phase is a ring index mod `MAHO_FRAMES_IN_FLIGHT`. A node is immediately schedulable once every edge it carries is satisfied (no stage barrier, cross-stage pipeline). Lifecycle: `Submit(batch)` (validation + admission) -> the scheduler dispatches ready nodes -> `Wait()` (a submission fence). There is no `Init`, no `Compile` and no rebuild: a batch merges into the live graph.
- **Thread pool** (FThreadPool): `Submit` enqueues and returns immediately, `Flush` blocks until all submitted tasks complete (`Wait()` = the graph fence + this pool barrier).
- **Compile-time filtering** (TQuery): `Query<TList>().Select<...>().With<...>().Not<...>().FResult` produces the filtered type list.
- **Batch expansion** (FFrameBridge::Build): one node per stage the frame IMPLEMENTS (an unimplemented stage emits nothing), plus the two STRUCTURAL edges -- a frame's own stages in order (the in-frame chain) and each stage against its own previous frame (the cross-frame self edge). Declared edges are resolved here too: a relative frame offset becomes an absolute `{name, stage, phase}`, reverse (`BlockOn`) declarations land on the DECLARER's node naming the target, and a name that cannot bind is REPORTED (the graph silently makes a dangling edge disappear, by design).
- **Install into the collector whose stages you mount** (FFrameBuilder): the install tree (`PluginManager.json` -> `GetChildren` + `InstallChildrenOf(GetName())`) puts each frame into the collector that drives it -- the host's, or a collector frame's own (FRender owns its features, FExampleWorld its systems). Only the frames selected for a batch's stage sequence get nodes; a frame installed into a collector whose stage set it does not mount contributes no node at all, so it is never driven and its declared dependencies are never even seen. The mirror-image mistake is a frame type installed into two collectors: the DLL is mapped once (LoadLibrary shares one copy and only bumps a refcount), but there are two frame INSTANCES -- two ImGui contexts, two of whatever the frame owns -- which is legal-but-rarely-intended; the per-collector name check cannot see it, so it is caught by reading the teardown trace, not by the loader.

## Synchronization & Shutdown Principles (module self-consistency)

The engine's design intent: **each module must be self-consistent (自洽) and provide reliable services upward**. The plugin architecture exists so every module owns its behavior. A bug -- especially a race -- is the owning module's responsibility. **Never fix a module's bug by silently modifying another module** to accommodate it. These are the hard-won debugging heuristics (validated 2026-09-01 on the render pipeline + shutdown):

- **Cross-module synchronization IS the dependency graph.** A dependency edge orders "writer before reader". If two modules access the same memory concurrently WITHOUT a dependency, they must both be READING (read-read is safe). Any WRITE to shared memory between modules implies a data-flow relationship, so they must be ordered -- **a cross-module write race means a missing dependency edge (`WaitFor`/`BlockOn`)**, nothing else.
- **Dependency direction is declared by the CONSUMER.** `MyStage<S>().WaitFor<Other>().OnStage<OtherStage>()` = "my S waits for Other@OtherStage" (I depend on a service). `MyStage<S>().BlockOn<Other>().OnStage<OtherStage>()` = "Other@OtherStage is blocked by my S" (Other must run AFTER me -- e.g. "the Log frame must outlive my teardown"). Both are the same graph edge declared from the side that knows it. A producer (e.g. FLog) does NOT enumerate its consumers; each consumer declares its own need. Both directions exist as typed templates (the declarer knows the producer's type) and as name-addressed forms (dynamically-loaded plugins referencing each other without a header include). Add `OnLastFrameStage<...>()` instead of `OnStage<...>()` for a CROSS-FRAME edge: the only cross-frame ordering the scheduler gives for free is the per-stage self edge, so "stage X of frame N+1 after stage Y of frame N" is always an explicit declaration.
- **The graph honors only declared edges -- there is no stage barrier.** Cross-stage ordering must be explicit. Example: every render feature's `IBeginRender` must depend on the Frame feature's `IFrameBegin`, or `ReleaseFrameLists` races the features' list acquisition (validation: `vkFreeCommandBuffers is in use`).
- **A layer's private async workers (own pool, threaded servers) are NOT graph-visible.** The graph schedules stages, not a layer's internal tasks. The layer must **self-close**: drain ALL its async work at the START of its own `Shutdown` (let the queues drain naturally, block until empty). This is the layer's contract with the scheduler -- failing it is the layer's bug, not a missing edge.
- **Shutdown is just the next frame.** A layer's `Shutdown` is the next scheduling unit that drains the previous frame's work -- structurally identical to the next `Tick`'s leading Flush. If you can Flush at the start of the next Tick, you can Flush at the start of Shutdown.
- **The shutdown transition's environment must stay alive -- express it as dependencies.** Just as the engine keeps Log/surface alive across normal frame transitions, the shutdown transition must too. This is a dependency, not a patch:
  - `FPlatform.IShutdown → FRender.IShutdown`: the RHI's surface is created from Platform's window, so the surface must outlive FRender's render teardown (symmetric with init's `FRender.IInit → FPlatform.IPostInit`). Without it, leftover present/recreate races a dying surface → garbage-capabilities validation errors.
  - `FLog.IShutdown → FRender.IShutdown`: the log outlives every layer that may log during teardown (so teardown logging never hits `GetLog()==null`).
  - Init-side: any layer that logs during `IInit` depends on `FLog.IInit` (the init graph is concurrent too).
- **The RHI is a stateless async processor.** It receives tasks and processes them; it never refuses work. Gating/refusing is application-layer logic -- do not add "exiting"-style flags to the RHI; drain the tasks instead.
- **`FThreadPool::Flush` is a quiescence barrier** (waits `PendingCount==0 && Queue.empty()`), tolerating concurrent `Submit` from nested graphs. The old FIFO no-op barrier leaks tasks dispatched by nested graphs (e.g. the render graph's dynamic downstreams) and must not be reintroduced.

## Thread Affinity (strict)

Some state does not live in memory the engine may schedule freely -- it lives in a **thread**. The
scheduling model ("a frame = its stages, dispatched to whichever pool worker is free") assumes every
stage is thread-agnostic. A module that owns an OS handle breaks that assumption, and the failure is
silent, intermittent, and easy to misdiagnose for days.

- **`FPlatform` is the canonical case.** A Win32 window's message queue belongs to the thread that
  **created** it, and `PeekMessage` only ever sees the **calling** thread's queue. Keys / characters /
  wheel exist *only* as posted messages, so a pump running on any other thread asks the wrong queue and
  finds nothing there.
- **The symptom, worth recognizing instantly: the mouse is fine while the keyboard is not.** Mouse
  position and buttons also survive through **global queries** (`GetCursorPos` / `GetAsyncKeyState`), so
  they stay fresh even when the pump is on the wrong thread -- posted messages just pile up and then
  arrive in a burst. With N pool workers the pump lands on the window's own thread about 1/N of the
  time (measured: ~2 keys per second, 0.2-2 s of queue delay, "a tap does nothing", "a run of
  characters appears at once", "one Backspace erases the whole line" -- and, because a press and its
  release can then land inside one ImGui frame, ImGui concludes the key was never pressed).
- **Rule**: a module that owns an OS handle, a message queue, or thread-scoped state must keep that
  state on ONE fixed thread. Either the module owns that thread itself (`FThreadedServer` -- which is
  why `FPlatform` derives from it and creates its window *and* pumps there), or the engine pins its
  stages to one. Every other thread may only read what that thread **published**.
- **Corollaries**: `ImmDisableIME`, `SetCapture`, `GetMessageTime`, `GetFocus`, `AttachThreadInput` are
  all thread-scoped -- they must be called ON that thread (and a thread-wide IME disable is meaningless
  if the pumping happens on other threads too). Window state read from anywhere else goes through
  published caches (atomics / a small mutex), never through the OS: `CachedSurface`,
  `CachedWidth/Height`, `bCachedShouldClose`, `CachedToolkitWindow`, refreshed by `PublishWindowState`
  -- and published **immediately after the window is created**, not only after the first pump, or the
  RHI finds no handle to build its swapchain from.
- **Corollary for anything published per frame**: tag it with the frame it belongs to (see
  `FPlatform::FInputFrame`: index + snapshot + that pump's events, one ring slot per frame, one cursor
  per consumer). A consumer lagging by a few frames then applies every frame exactly once, in order --
  which is what lets two ImGui contexts share one input stream instead of stealing it from each other.

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

### 4 UI Is Declared as a Component Tree, Never Drawn (strict)

- **Every UI is declared as a component tree** through the UI plugin (`Maho::UI`, `Plugins/GameEngine/EngineCore/UI`): a persistent `UI::FUIView` (the tree IS the widget -- runtime state survives across frames) is re-declared each frame inside `FUIView::Edit()`, and re-declaring a node with the SAME id AND type reuses it. Panels and game code call `UI::FUIBuilder` / the `Widgets/` types only.
- **No `ImGui::*` call and no `<imgui.h>` include outside the translator.** The one translation boundary is the UI plugin's `Private/UIImGuiTranslator.*` + `Private/UIImGuiEntry.cpp` (the `maho_imgui` link lives only on the layers that own an ImGui context: `UIFeature` for the game, `ExampleEditor` for the editor). A panel that hand-writes an ImGui call bypasses the ownership/teardown/flush contract and will be rejected in review -- the same rule holds for `Public/*.h` (see the include-direction rules above).
- **Cross-thread contract is exclusive-write / shared-read**: a view with `EUIOwnership::CrossThread` is mutated only inside `Edit()` (exclusive write, `std::shared_mutex`) and read by the translation thread under a shared read; interaction events are queued by the translator and replayed on the OWNER thread by `DrainEvents()`. A view with `EUIOwnership::SameThread` is touched by one thread only.
- **A view owner declares its teardown ordering against the registry at the layer that drives the teardown**: the registry holds raw pointers, so every view must be unregistered BEFORE the registry's `IShutdown`. A sub-plugin's own `BlockOn` is silently skipped (a collector sub-graph only contains its own pending set), so the edge belongs on the TopLevel layer that drives the uninstall (`FGameWorld` for world systems, `FRender`/`FExampleEditor` for editor panels) and is expressed by NAME to avoid forcing a build dependency on the optional UI plugin.

## Build Configuration (two axes, strict)

`.cproject` declares exactly TWO axes and nothing else: `BuildType` (`Runtime | Editor`) and `Configuration` (`Debug | Release | Shipping`). Six cells; the matrix and what each cell isolates are the spec (`openspec/changes/add-build-configuration/`, `design.md` D2/D3).

- **Every build-shape macro is DERIVED by the generator** (`Tools/maho_tools.py`, `_build_config_block`) into one `add_compile_definitions` block, per configuration (`$<CONFIG:...>`) -- so ONE generated project builds all three. `Source/Public/Core/BuildConfig.h` is the only reader: axis macros (`MAHO_BUILD_*`, `MAHO_EDITOR_BUILD`), behavior macros (`MAHO_DO_*`), capability macros (`MAHO_WITH_*`), plus the `#error` guards (unknown cell, `Shipping x Editor`).
- **A plugin `.cmake` must never define one of those macros.** A second definition site is how the matrix rots -- the define stops matching the axes and nobody notices until a Shipping build ships with its checks in -- so the generator rejects it with file:line. A CMake *variable* (e.g. `MAHO_BUILD_DIR`) is unaffected; a capability *override* goes through `.cproject`/`.cplugin`, not `.cmake`.
- **Adding a switch means answering "which cells?":** a check goes behind `MAHO_DO_*`, a facility behind `MAHO_WITH_*`, and the generator derives the values. Never read `NDEBUG` / `_DEBUG` to mean a configuration -- that is the toolchain's axis, not ours.
- **Shipping carries no diagnostics** (`MAHO_CHECK`/`MAHO_CHECKF`/`MAHO_ENSURE` compile out, trace and logging bodies vanish, stats and the RHI validation layer are not built) and registers only the CVars flagged `ECVarFlags::Shipping` (player-facing settings). It must still RUN: `ReportFatal`/`ReportError` stay, `GetLog()` still answers (it is the engine's "this layer was driven" observable), and an unregistered CVar reads as its own default instead of breaking the build. **A build that needs the diagnostics ships as `Release`** -- optimized, with logging/trace/checks all in; that is the cell for a release handed to artists or QA.

## Docs

- [Source/Docs.html](Source/Docs.html) - 引擎源码文档：左侧 Source 树（叶子 = `.h`），右侧逐类展示字段 / 接口签名 / 功能描述。**内容在 `Tools/docs_content.py` 里一条一条手工声明**（`Tools/docs_builder.py` 提供原子接口：`Header` / `Class` / `Interface` / `Field` / `Nested` …，**不扫描源码**）；渲染：`Tools\maho_python.bat Tools\docs_content.py`
