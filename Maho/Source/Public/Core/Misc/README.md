# Core Misc

Infrastructure primitives for the Maho engine core.

## Modules

| Header | Purpose |
|--------|---------|
| `Log.h` | spdlog facade + `MAHO_*` logging macros |
| `Json.h` | JSON value/document wrapper |
| `Paths.h` | Project/engine roots, mount points, virtual package paths |
| `Delegate.h` | Unicast/multicast delegates + handle |
| `TypeList.h` | Compile-time type list utilities |
| `Console.h` | CVar registry facade |
| `ConsoleVariable.h` | `IConsoleVariable` interface + flags/set-by |
| `ConfigFile.h` | Minimal `.ini` reader |
| `Archive.h` | Bidirectional binary serialization stream |
| `AsyncTask.h` | One-shot temporary-thread job |
| `Compression.h` | zlib wrappers |
| `Fatal.h` | Unified fatal error path |
| `Timer.h` | Multi-category scope timer |
| `Utf8Path.h` | UTF-8 ↔ wide / filesystem path helpers |
| `RefCounted.h` | `TRefCounted` + `TRef` intrusive smart pointer |
| `DependsPack.h` | Compile-time extension dependency graph |
| `Export.h` | DLL import/export macros |

Use `Misc.h` to include all Misc headers at once.

## Use

```cpp
#include <Core/Misc/Misc.h>

Maho::FJsonValue Root = Maho::FJsonValue::Object();
Root.SetField("name", Maho::FJsonValue::String("Hero"));
```

## Related docs

- [Core Engine](../Engine/README.md)
- [Server primitives](../Server/README.md)
- [Core aggregate](../Core.h)
