# Paths

## Code files

- [Paths.h](Paths.h) - path resolution singleton `FPaths`

## Concept - path resolution

Path resolution singleton service - resolves **virtual paths** to **physical paths**. After registering root aliases (e.g. `Engine` -> engine directory), `Resolve("Alias/Sub/Path")` (or `"Alias:Sub/Path"` colon-separated) joins the alias root with the sub-path. Every cross-platform path abstraction in the engine (Asset's MountAlias, Resource's virtual source path) lands here.

### FPaths - root alias -> physical path

`TSingleton<FPaths>` + `IPlugin<IInit, IShutdown>` (Initialize/Shutdown clear `Roots`). Internal `std::map<std::string, std::filesystem::path>`:

- `SetRoot(Alias, Path)`: register/override a root alias.
- `Resolve(VirtualPath)`: take the alias segment before the first `/` or `:` and look it up; join the remainder after the root; unregistered aliases are returned as-is (fault tolerance).
- `HasRoot(Alias)`: whether the alias is registered.

```cpp
FPaths::Get().SetRoot("Engine", engineDir);
const auto Full = FPaths::Get().Resolve("Engine/Config/Base.ini");
const bool bOk   = FPaths::Get().HasRoot("Engine");
```

## Third-party dependencies

- None (pure std, `std::filesystem`).

## Related docs

- [API.html](API.html) - API documentation
