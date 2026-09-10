# Resource - Agent Entry

All AI agents must read this file before entering this plugin.

## Design Constraints (strict)

- Responsibility boundary: async resource import/export. New resource type = specialize `TResourceImporter/Exporter<T>` (pure codec, sees bytes not threads) + `TResourceCreator<T>` (instance factory, defined out-of-line in the type's own module); host owns the lifecycle: `Initialize` starts the IO thread, `Tick` every frame, `Shutdown` stops the thread and clears the catalog.
- **Never construct a resource in a header template.** `Import<T>` / `CreateResource<T>` go through `TResourceCreator<T>` so the instance's vtable + deleting dtor live in the resource type's module, never in the caller's: a caller may be a sub-plugin that is unloaded while the resource is still cataloged, and the engine's Shutdown then deletes it through a vptr into a freed image (validated crash: `FContentBrowser.dll` unloaded before `FResourceSystem::Shutdown`).
- Dependencies go only through `.cplugin` `Dependencies` (`["Name", "Paths"]`), include `<Resource.h>`, no cross-directory relative includes.
- Implementation notes:
  - `TSingleton` + `FThreadedServer`, `Get()` defined in `Private/Resource.cpp` (process-unique inside Resource.dll).
  - **Import**: IO thread reads file -> game thread decodes in `Tick` (Importer called on game thread, safe to touch catalog/state).
  - **Export**: caller thread encodes (Exporter reads catalog resources synchronously, no cross-thread sharing) -> IO thread writes to disk -> `OnDone` returns to game thread via `Tick`; caller must keep the resource alive and unchanged until completion.
  - Transfer details (`FTransferState` / `FBulkData` / `FTransferHandle`) all inside the cpp, header only forward-declares.
- Follow root [AGENTS.md](../../../../AGENTS.md).
