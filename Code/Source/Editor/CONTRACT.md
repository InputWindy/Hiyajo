# Editor UI Registry — module contract

## Role

`FEditorUIRegistry` lets the editor shell (`FEditorLayer`) and game/plugin code contribute UI into fixed **regions**, grouped by **Catalog**. The shell owns chrome geometry (menu row, toolbars, DockSpace, central viewport window). Registry only injects contributions.

**Scope:** editor only. Runtime game HUD / screen-space UI is a separate future `FGameUI` (or similar) — do not merge with this registry.

## Regions

| Region | Purpose |
|--------|---------|
| `MainMenu` | Top-level menus and popup items |
| `ToolbarPrimary` | First toolbar (beside brand) |
| `ToolbarSecondary` | Second toolbar strip |
| `DockPanel` | Closable / dockable panels (including temporary Details) |
| `CentralViewport` | Overlays inside the locked central viewport |
| `Modal` | **Blocking** popups only (`BeginPopupModal`) |

## Catalog (required)

Every contribution must carry:

```cpp
struct FEditorUICatalog
{
	std::string Name;       // e.g. "Transform", "Play", "Browser", "Debug"
	std::int32_t Order = 0; // catalog sort key
};
```

Plus per-item `Id`, `Order` (within catalog), and `Draw`.

**Sort:** `Catalog.Order` → `Catalog.Name` → item `Order` → `Id`.

**UI separators:** adjacent contributions with different `Catalog.Name` must show a separator (toolbar: vertical; menus / Window dock list: horizontal). Empty `Catalog.Name` is refused.

### Built-in catalog conventions

| Area | Catalogs |
|------|----------|
| ToolbarSecondary | `Transform`, `Play` |
| DockPanel | `Browser`, `Log`, `Tools`, `Details` (transient), `Dummy` (debug) |
| Menu | `File` / `Window` / `Help` / `Debug` / `Dummy` |
| Modal | `System` (busy / blocking) — **never** Details |

## Temporary Details vs Modal

- Temporary Details (asset inspector, material editor secondary, etc.) → **`DockPanel`** with `bTransient = true`, open via `OpenDockPanel(Id)`.
- **Modal** = blocking only (loading / busy). Do not put editable Details in a modal.

## Central viewport dock rules

- Central window: `NoTabBar` + `NoUndocking`.
- DockSpace: `ImGuiDockNodeFlags_NoDockingOverCentralNode` so other panels cannot dock **over** the central node as siblings/tabs.
- Side splits that squeeze the central viewport remain allowed (`NoDockingSplit` must **not** be set on the central node for that reason).

## Access

```cpp
FEditorUIRegistry* Reg = Maho::TryGetEditorUIRegistry(App);
// or
App.GetExtension<FEditorLayer>()->GetUIRegistry();
```

Register from Mount / plugin Attach after the editor layer exists.

**Location:** game project `Source/Editor/` (not Maho engine). Enable via `GAME_WITH_EDITOR`.

## Status

- Registry + Catalog separators landed.
- Built-ins migrated (toolbars, docks, Window toggles, Debug/Dummy samples).
- ImGui still uses official backends (not FRHICommandList).

## Pitfalls

- Do not use Modal for Details.
- Do not register with empty Catalog.Name.
- Dummy UI is toggled from **Debug → Show Dummy UI**; docks via **Open Dummy Docks**.
- Central viewport is not yet a real engine render-target `Image` (still ImGuizmo placeholder).

## Related

- Public: `Core/Editor/EditorUIRegistry.h`, `EditorUITypes.h`
- Shell: `Core/Extension/Editor/EditorLayer.h`
- Journal: `Doc/Engine/DESIGN_JOURNAL.md` → Editor UI
