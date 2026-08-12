# Maho Engine Plugins

Optional runtime plugins live here (`.cplugin` + `Source/`). They are **not** part of
`Maho.dll`.

## Built-in engine systems (Mahi.dll, always loaded)

These are **engine-scaffold systems** that ship inside the engine core — every game gets them:

| Extension class | `GetName()` | Location |
|-----------------|-------------|----------|
| `FPlatformSystem` | `Platform` | `Maho/Source/Public/Core/Extension/Platform/Platform.h` |
| `FRenderSystem` | `Render` | `Maho/Source/Public/Core/Extension/Render/Render.h` |

## Project-side systems (`Source/Game/System/` in game project)

These are **application-layer systems** — the project owns them, not the engine:

| System | Location in project |
|--------|-------------------|
| `FGCSystem` | `Source/Game/System/GC/GCSystem.h` |
| `FResourceSystem` | `Source/Game/System/Resource/ResourceSystem.h` |
| `FScriptSystem` | `Source/Game/System/Script/ScriptSystem.h` |

They are auto-discovered by codegen from `Source/Game/System/` and registered in the generated `<Game>App.cpp`.

## Minimal plugin example: `Sample`

```text
Maho/Plugins/Sample/
  Sample.cplugin
  Source/Sample/
    Public/SampleApi.h
    Public/SampleModule.h
    Private/SampleModule.cpp
```

`.cplugin` Module entry for auto-register:

```json
"Extension": {
  "Class": "Maho::FSampleModule",
  "Header": "SampleModule.h",
  "Priority": "Overlay"
}
```

1. Enable in the game `.cproject`: `{ "Name": "Sample", "Enabled": true }`
2. Double-click `.cproject` (or run `generateProject.bat`) — codegen injects Register + `#include`
3. Plugin `Source/<Module>/Public` is on the game include path via `Maho::Modules`

Omit `Extension` to build/link the DLL only (no App Register).

## Optional plugin naming

| Layer | Rule | Example |
|-------|------|---------|
| Plugin folder / `.cplugin` / CMake target / DLL / `GetName()` | short PascalCase name | `Sample` |
| Module class | `F` + role + `Module` | `class FSampleModule` |
| Module headers | Public path → `<>` | `#include <SampleModule.h>` |
| Private headers | Private path → `""` | `#include "SamplePrivate.h"` |
| Export macros | per-plugin `Public/<Name>Api.h` | `#include <SampleApi.h>` |

## Game `.cproject` Plugins list

```json
"Plugins": [
  { "Name": "Sample", "Enabled": true }
]
```

Empty `"Plugins": []` means no optional plugin DLLs (unless a plugin sets `EnabledByDefault: true`).

Extension stage order uses `TDependsPack` / `TDependsOn` in code — **not** a `.cplugin` field.
Do not put `Dependencies` on Modules.

## Layout

```text
Maho/Plugins/<Name>/
  <Name>.cplugin
  Source/<ModuleName>/
    Public/<X>Module.h
    Private/<X>Module.cpp
```

## Scan tool

```text
Tools\scan_plugins.bat --cproject path\to\Game.cproject
```
