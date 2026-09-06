# UISystem API

## Exported surface

- `FUISystem` (`MAHO_UISYSTEM_API`) -- the world system layer.

## C export

```cpp
extern "C" MAHO_UISYSTEM_API Maho::FLayerBase* CreateLayer();
```

Looked up by symbol at dynamic install (`FAssembly`), so `FGameWorld` can load the
plugin by name / typed `Install<GameWorld::FUISystem>()`.

## Types

- `FUIWidget` -- ECS-side widget state (screen anchor by `X/Y` + size). Pure game
  data, written through the world accessor.

## Wiring

GameWorld installs it as a peer layer. The render layer (`FUIFeature`) reads the
model (not yet wired -- UI is model-only until a consumer exists).
