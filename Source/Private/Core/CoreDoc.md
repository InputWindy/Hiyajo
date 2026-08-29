<!-- mahogen -->
# Core

## Code Files

- [Assembly.cpp](Assembly.cpp)
- [Fatal.cpp](Fatal.cpp)
<!-- mahogen end -->

## Implementation Algorithm Dictionary

The engine core has implementations in only two `.cpp` files (the rest are all header-only templates).

### Assembly.cpp -- Dynamic Loading Primitive

The OS loading implementation of `FAssembly`.

| Function | Description |
|------|------|
| `FAssembly(std::string_view Path)` | `Load` constructor |
| `~FAssembly()` | `Unload` destructor |
| `Load(Path)` | `LoadLibraryA` / `dlopen`, returns success |
| `Unload()` | `FreeLibrary` / `dlclose`, idempotent |
| `GetProcAddress(Name)` | `::GetProcAddress` / `dlsym`, returns `nullptr` on empty handle |

### Fatal.cpp -- Crash Fallback

| Function | Description |
|------|------|
| `ReportFatal(...)` | outputs fatal error + terminates process |
| `InstallFatalHandlers()` | registers structured exception / signal handlers |

## Related Docs

- [../../Public/Core/CoreDoc.md](../../Public/Core/CoreDoc.md) -- root concepts (Public)
- [../../PrivateDoc.md](../../PrivateDoc.md) -- Private layer
