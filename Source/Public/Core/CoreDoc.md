<!-- mahogen -->
# Core

## Code Files

- [Assembly.h](Assembly.h)
- [Core.h](Core.h)
- [Export.h](Export.h)
- [Extension.h](Extension.h)
- [Fatal.h](Fatal.h)
- [Interface.h](Interface.h)
- [Query.h](Query.h)
- [Queue.h](Queue.h)
- [Schedulers.h](Schedulers.h)
- [Singleton.h](Singleton.h)
- [ThreadedServer.h](ThreadedServer.h)
- [ThreadPool.h](ThreadPool.h)
- [Topology.h](Topology.h)
- [TypeList.h](TypeList.h)
<!-- mahogen end -->

## Concept -- Type-Agnostic Infrastructure Building Blocks

Core is a pure set of building blocks with **zero app assumptions, zero third-party dependencies, zero stage presets**. Every component is type-agnostic (does not reference FLayer/app concepts) and can be used independently.

### 1. Type List (TypeList)

`TTypeList<T...>` is a compile-time ordered type array. Operations: `TCons` (prepend) / `TAppend` (append) / `TContains` (membership) / `TCatch` (concatenate) / `TUnionList_t` (deduplicated union).

### 2. Dependency Topology (Topology)

`MAHO_EXTEND_DEPS` (Extension.h) is a **generic dependency declaration anchor** -- any class can mark dependencies (codegen scans it and generates a `.gen.h` macro that fills `FDepends`). `TResolveDependsPack`/`TNodeLevel`/`TLevels_t` compute the layered order at compile time:

```cpp
class FWorld : public FLayer<> {
    MAHO_EXTEND_DEPS(FWorld, FDefaultSlot, (FNoParent, FAI));  // declaration anchor
    // codegen -> World.gen.h: #define MAHO_DEPS_FWorld_FDefaultSlot FAI
    // macro fills FDepends = TTypeList<FDefaultSlot, TTypeList<FAI>>
};
using FLevels = Topo::TLevels_t<FChildrenList, FDefaultSlot>;  // layered order
```

### 3. Type Query (Query)

`TQuery<FList>` is a type-agnostic compile-time LINQ -- Select (OR) / With (AND) / Not (NOR) chain filtering, outputting `FResult` (TTypeList):

```cpp
using FTickable = TQuery<FTable>::Select<ITick>::With<IShared>::Not<ITest>::FResult;
```

### 4. Command Queue (Queue)

`FQueue` is **type-agnostic**: stores `unique_ptr<ICommand>`, commands carry their own `GetCatalogId()` (uint64) to route to a catalog lane. FIFO, multi-threaded Enqueue, consumer Dequeues then executes:

```cpp
FQueue Q;
Q.Enqueue(std::make_unique<FInstallCmd>(...));          // any thread
while (auto Cmd = Q.Dequeue(kInstallLane)) { /* apply */ }  // consumer executes
```

`ICommand` is a pure data carrier (only `GetCatalogId()`), no execution protocol.

### 5. Parallel Execution (Schedulers + ThreadPool)

`FParallelScheduler` (non-template) -- two generic ForEach: variadic callable pack + runtime container (MakeTask projection). Internal `FThreadPool` parallel execution + barrier finish. The "layered semantics" of serial between layers / parallel within a layer is the caller's concern (held by FLayer).

`FThreadedServer` -- a resident single thread + FIFO task queue (dedicated roles like IO thread / render thread).

### 6. Singleton

`TSingleton<T>` is a **marker base class** (CRTP) with no mandatory lifecycle. Subclasses declare their own `static T& Get();` (defined in .cpp, process-unique). Lifecycle, when needed, is composed via `IPlugin<IInit,IShutdown>` (Interface.h).

### 7. Capability Interfaces (Interface)

`IInit` (Initialize(int,char**)) / `IShutdown` (Shutdown()) / `IMain` (Main()) / `IExit` (Exit()) / `IPlugin<Caps...>` (virtual-base combinator). Capabilities are **optionally composable** -- not every object needs a lifecycle.

### 8. Loading and Fatal Errors (Assembly / Fatal)

`FAssembly` -- DLL loading RAII. `Fatal::ReportFatal` -- fatal error (non-recoverable).

## Related Docs

- [Core.h](Core.h) -- aggregate header
- [CoreAPI.html](CoreAPI.html) -- API docs
- [../Engine/EngineDoc.md](../Engine/EngineDoc.md) -- layer architecture (FLayerBase assembles the above infrastructure)
- [../../SourceDoc.md](../../SourceDoc.md) -- source root
