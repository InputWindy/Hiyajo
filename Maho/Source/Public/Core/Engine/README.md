# Core Engine

Engine shell + extension lifecycle for Maho applications.

## What lives here

| Type | Purpose |
|------|---------|
| `FEngineBase` | Application shell: config, log, console, timer, extension registry, game loop |
| `FGameClientEngine` | Client shell: game world + render world framing |
| `FGameServerEngine` | Headless server shell: game world only |
| `FNullEngine` | Minimal shell with no extensions |
| `IEngineExtension` | Extension interface driven through lifecycle stages |
| `EEngineStage` | Unified lifecycle stages (`PreInit` → `Shutdown`) |
| `EExtensionPriority` | Ordering band (`System` / `Layer` / `Overlay`) |
| `FConfig` | App/module configuration owned by `FEngineBase` |

`Engine.h` declares `FConfig` (not an aggregate header). Use `Core.h` for the
full Core aggregate.

## Use

```cpp
#include <Core/EngineBase.h>

class FMyClient : public Maho::FGameClientEngine
{
protected:
    Maho::FSystemGroup* CreateWorld() override;
};
```

Extensions implement `IEngineExtension` and register through
`FEngineBase::RegisterExtension` (protected, called from `PreInitialize`).

## Related docs

- [Misc infrastructure](../Misc/README.md)
- [Server primitives](../Server/README.md)
- [Core aggregate](../Core.h)
