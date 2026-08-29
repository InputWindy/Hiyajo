<!-- mahogen -->
# Source

## Sub Layers

- [Private](Private/PrivateDoc.md)
- [Public](Public/PublicDoc.md)
<!-- mahogen end -->

## Concept -- Source Root

Engine source root: `Public/` holds interfaces and headers, `Private/` holds implementations (.cpp).

- [Public/PublicDoc.md](Public/PublicDoc.md) -- interfaces + aggregate headers
- [Private/PrivateDoc.md](Private/PrivateDoc.md) -- implementations

The engine is a **pure scaffold** -- only type-agnostic infrastructure (Core) + layer system (Engine/). All concrete services are installable plugins (`Plugins/Common/`). The engine .cpp files are only `Assembly.cpp` / `Fatal.cpp` / `TaskGraph.cpp`; the rest of the infrastructure is header-only (templates + inline).

### Layering

- **Core** (`Public/Core/`): type-agnostic infrastructure building blocks -- TypeList/Query/Singleton/Interface/TaskGraph/Assembly/Fatal/ThreadPool.
- **Engine** (`Public/Engine/`): layer system -- `Layer.h` (FLayerBase/FLayer/FLayerTaskGraph/plugin macro) + `Engine.h` (three-stage main loop + IEnginePipeline/FEngineLayer/FEngineBase).
- **Plugins** (`Plugins/`): installable plugins -- `Common/` (service plugins, pure libraries or singletons). The engine makes zero app assumptions; all logic lives in plugins.
