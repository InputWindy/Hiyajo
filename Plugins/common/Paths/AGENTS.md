# Paths - Agent entry

All AI agents read this file before entering this plugin.

## Design constraints (strict)

- Responsibility: path resolution infrastructure. Do not hard-code physical paths when reading/writing files; first `SetRoot` to register an alias, then `Resolve` the virtual path.
- Dependencies only go through `.cplugin` `Dependencies`; include `<Paths.h>`, no cross-directory relative includes.
- Implementation notes:
  - `TSingleton<FPaths>`; `Get()` is defined in `Private/Paths.cpp` (process-unique inside Paths.dll).
  - Internal `std::map<std::string, std::filesystem::path> Roots`, lock-free - `SetRoot/Resolve` should complete single-threaded during initialization.
  - `Resolve` accepts both `Alias/Sub/Path` and `Alias:Sub/Path` forms.
- Follow root [AGENTS.md](../../../../AGENTS.md).
