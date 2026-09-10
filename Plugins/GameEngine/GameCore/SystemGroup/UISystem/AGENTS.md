# UISystem - Agent Entry

Read this before editing UISystem.

## Role

A **GameWorld system** (world-system peer layer), installed into `FGameWorld` by
the sub-landlord collector (`Install<GameWorld::FUISystem>()`). It owns the
game-side UI: every `FUIWidget` entity owns a **persistent declarative UI tree**
(`UI::FUIView`) registered in the UI plugin's view registry. Each `Update` it
re-declares the widget's tree -- the render side translates the tree once per
frame. It does **not** draw, and it does not know ImGui exists.

## Rules

- **Namespace**: `Maho::GameWorld` (matches `GameWorld.h`).
- **Stages**: `IOnInstalled` / `IProcessInput` / `IUpdate` / `IPreUnInstall`.
  Unimplemented stages (e.g. `IFixedUpdate`/`ILateUpdate`) are skipped by the
  dispatch `dynamic_cast` -- do not force them.
- **World access**: never touch the graph directly; use `FGameWorld&`'s accessors
  (`CreateEntity`, `AddComponent`, `GetComponent`, `IsAlive`, `DestroyEntity`,
  `GetDeltaSeconds`).
- **Dependency direction**: `Dependencies:["GameWorld","UI"]`. GameWorld is the
  sub-landlord and installs this via the header (which is why GameWorld carries
  `PrivateIncludes:["UISystem","UI"]` -- compile-only, no link back).
- **UI contract**: declare the tree inside `UI::FUIEditScope Scope = View->Edit()`
  (exclusive write, `std::shared_mutex`), then `View->DrainEvents()` on this
  (owner) thread. Never call ImGui, never link `maho_imgui`: the game context is
  fetched through `UI::GetUIGameRenderContext()` (published by the render side),
  so this plugin does not depend on `UIFeature` either.
- **Teardown**: `PreUnInstall` unregisters the view (`Registry->UnregisterView`)
  BEFORE the tree is destroyed -- the registry holds raw pointers. The ordering
  "unregister, then close the registry" is declared by `FGameWorld` (the layer
  that drives this teardown) as a name-form `BlockOn("FUIViewRegistry", IShutdown)`;
  an edge declared here would be silently skipped (this layer is a sub-plugin of
  the world's own collector graph).
