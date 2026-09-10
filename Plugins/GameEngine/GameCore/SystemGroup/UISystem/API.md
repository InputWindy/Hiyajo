# UISystem API

## Exported surface

- `FUISystem` (`MAHO_UISYSTEM_API`) -- the world system layer, `MAHO_DECLARE_LAYER(FUISystem)`.
- `UI::FUIWidget`-owning helper types are internal; the component struct itself is public:

```cpp
struct FUIWidget
{
	std::shared_ptr<UI::FUIView> View;   // the persistent declarative tree, registered in the UI registry
};
```

`shared_ptr` (not `unique_ptr`) because the component pool moves elements by value when it grows.

## C export

```cpp
extern "C" MAHO_UISYSTEM_API Maho::FLayerBase* CreateLayer();
```

Looked up by symbol at dynamic install (`FAssembly`), so `FGameWorld` can load the
plugin by name / typed `Install<GameWorld::FUISystem>()`.

## Accessor

```cpp
MAHO_UISYSTEM_API FUISystem* GetUISystem();   // nullptr until installed (mirrors Resource::GetResourceSystem)
```

## Wiring

`FGameWorld` installs it as a peer world system. Render side: `UIFeature` translates
every registered view once per frame (it publishes the game UI context through the UI
plugin, which is how this plugin tags its views without depending on `UIFeature`).
