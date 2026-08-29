# Resource

## Code Files

- [Resource.h](Resource.h) - typed async resource system (`FImportConfig` / `FExportConfig` / `FResource` / `TResourceImporter` / `TResourceExporter` / `FResourceSystem`)

## Concept - Async Resource System

Typed async resource system singleton service - dedicated **IO thread** (`FThreadedServer`) performs async import/export, catalog keyed by **FName**, virtual paths resolved through **FPaths**. Importers/exporters are user-specialized templates per resource type, only responsible for encoding/decoding raw bytes (they see `std::span` / `std::vector` of bytes).

### FResourceSystem - Async Transfer Server (Singleton Service)

`TSingleton<FResourceSystem>` + `FThreadedServer` + `IPlugin<IInit, IShutdown>`:

- **Initialize**: starts the IO thread (`FThreadedServer::Initialize`).
- **Tick**: called every frame on the game thread - polls ready transfers and executes decode / `OnDone` callbacks on the game thread (`kMaxAppliesPerTick = 1`).
- **Shutdown**: stops thread + join, clears pending and catalog.
- **Import\<TResource\>(Config, OnDone)**: async import. Virtual source path resolved through FPaths to a physical path, IO thread reads raw bytes -> `Tick` calls `TResourceImporter<TResource>::Import(Config, Bytes, *Resource)` on the game thread to decode -> `RegisterResource` into catalog -> `OnDone(const FResource*)`.
- **Export\<TResource\>(Config, AssetPath, OnDone)**: async export. **Encoding on the caller (game) thread** (synchronously reads catalog resource, no cross-thread sharing), only bytes handed to IO thread `WriteBytes`; `OnDone(bool)` on the game thread (via Tick). Caller must guarantee the resource stays alive and unchanged during export.
- **Find / TryLoad**: look up the catalog by asset path (virtual path with extension stripped).

### Specialized Templates

- `TResourceImporter<TResource>`: must provide `using FConfig = ...;` + `static bool Import(const FConfig&, std::span<const std::uint8_t>, TResource&)`.
- `TResourceExporter<TResource>`: must provide `using FConfig = ...;` + `static bool Export(const FConfig&, const TResource&, std::vector<std::uint8_t>&)`.
- Undefined by default - specialize per resource type.

### Config Bases

- `FImportConfig`: `SourcePath` (virtual path, e.g. `"Raw/mesh.fbx"`) - asset path (catalog key) derived by stripping the extension.
- `FExportConfig`: `DestinationPath` (physical path, e.g. `"C:/Out/mesh.fbx"`).

```cpp
template <>
struct Maho::Resource::TResourceImporter<FMesh>
{
    using FConfig = FMeshImportConfig;
    static bool Import(const FConfig&, std::span<const std::uint8_t>, FMesh&);
};

Resource::FResourceSystem::Get().Import<FMesh>({ "Raw/mesh.fbx" },
    [](const Resource::FResource* R) { /* loaded, game thread */ });
const Resource::FResource* R = Resource::FResourceSystem::Get().TryLoad("Raw/mesh");
```

## Third-Party Dependencies

- None.
- Other plugins: **Name** (catalog key), **Paths** (virtual path resolution) - `.cplugin` Dependencies = `["Name", "Paths"]`.

## Related Docs

- [API.html](API.html) - API docs (public signatures)
- [ImplAPI.html](ImplAPI.html) - implementation algorithm dictionary (cpp function pseudocode)
