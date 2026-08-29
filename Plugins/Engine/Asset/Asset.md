# Asset

## Code files

- [Asset.h](Asset.h) - asset registry (`EAssetType` / `FAssetPath` / `FAssetData` / `FAssetRegistry`)

## Concept - asset registry

Asset registry singleton service - **logical path (`/Game/...`) -> asset metadata**. `Scan()` recursively indexes a content directory and registers every asset file, `Find()` queries by logical path, `Resolve()` maps to a physical file via FPaths root aliases, `Load()` reads raw bytes. Logical paths have **no extension and no object part** (this engine has no UObject system).

### FAssetPath - logical asset path

Extension-less logical path (`"/Game/Materials/M_Metal"`), default-constructed empty. `GetPath()` / full comparison operators; usable as a map key.

### FAssetData - asset metadata

`{Path, Type, File(physical file), Dependencies}` - `Dependencies` are filled by deserialization (deferred resolution).

### FAssetRegistry - registry singleton

`TSingleton<FAssetRegistry>` + `IPlugin<IInit, IShutdown>` (`Mutex` protected):

- `Scan(ContentDir, MountAlias = "Game")`: recursive indexing; `Content/Materials/M_Metal.material -> /Game/Materials/M_Metal (Material)`. Type is inferred from the disk extension (`.material` / `.texture`); **MountAlias is also registered as an FPaths root alias**.
- `Find(Path)`: look up a logical path; `nullptr` when absent.
- `Resolve(Path)`: `"/Game/..." -> FPaths root "Game" + sub-path` to build the physical path.
- `Load(Path)`: read the asset file's raw bytes (`std::optional<vector<uint8_t>>`).

```cpp
FAssetRegistry::Get().Scan(ContentDir);   // Content/Materials/M_Metal.material -> /Game/Materials/M_Metal (Material)
const FAssetData* D = FAssetRegistry::Get().Find(FAssetPath("/Game/Materials/M_Metal"));
auto Bytes = FAssetRegistry::Get().Load(FAssetPath("/Game/Materials/M_Metal"));
```

## Third-party dependencies

- None.
- Other plugins: **Paths** (`.cplugin` Dependencies = `["Paths"]`) - `Resolve` uses root aliases to resolve physical paths; MountAlias is the root alias.

## Related docs

- [API.html](API.html) - API documentation
