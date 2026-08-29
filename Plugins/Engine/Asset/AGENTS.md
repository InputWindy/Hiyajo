# Asset - Agent entry

All AI agents read this file before entering this plugin.

## Design constraints (strict)

- Responsibility: asset catalog. Logical paths are always `/Game/...` (no extension); filesystem access goes through `Resolve/Load`, never hand-built physical paths.
- Dependencies only go through `.cplugin` `Dependencies` (`["Paths"]`); include `<Asset.h>`, no cross-directory relative includes.
- Implementation notes:
  - `TSingleton<FAssetRegistry>`; `Get()` is defined in `Private/Asset.cpp` (process-unique inside Asset.dll).
  - Internal `std::map<std::string, FAssetData>` + mutex; `Scan` walks directories recursively and determines the type by extension.
  - `Resolve` depends on the Paths plugin (MountAlias is the root alias).
- Follow root [AGENTS.md](../../../../AGENTS.md).
