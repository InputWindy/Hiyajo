# UISystem - Agent Entry

Read this before editing UISystem.

## Role

A **GameWorld system** (world-system peer layer), installed into `FGameWorld` by
the sub-landlord collector (`Install<GameWorld::FUISystem>()`). It owns the
ECS-side UI widget model (`FUIWidget` + `FTransform` on spawned entities) and
animates it each frame. It does **not** draw -- the render layer
(`FUIFeature`/ImGui) consumes the model upstream.

## Rules

- **Namespace**: `Maho::GameWorld` (matches `GameWorld.h`).
- **Stages**: `IOnInstalled` / `IProcessInput` / `IUpdate` / `IPreUnInstall`.
  Unimplemented stages (e.g. `IFixedUpdate`/`ILateUpdate`) are skipped by the
  dispatch `dynamic_cast` -- do not force them.
- **World access**: never touch the graph directly; use `FGameWorld&`'s accessors
  (`CreateEntity`, `AddComponent`, `GetComponent`, `IsAlive`, `DestroyEntity`,
  `GetDeltaSeconds`).
- **Dependency direction**: `Dependencies:["GameWorld"]` (PUBLIC include + link).
  GameWorld is the sub-landlord and installs this via the header; UISystem never
  links GameWorld back into itself beyond the one-way dep.
- **Teardown**: drain everything in `PreUnInstall` (destroy handles). Do not rely
  on the collector ordering; close your own state.
