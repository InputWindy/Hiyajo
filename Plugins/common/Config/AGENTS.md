# Config - Agent entry

All AI agents read this file before entering this plugin.

## Design constraints (strict)

- Responsibility: INI config read/write. Static ini config goes through this plugin; startup-parameter style config should use CommandParser.
- Dependencies only go through `.cplugin` `Dependencies`; include `<Config.h>`, no cross-directory relative includes.
- Implementation notes:
  - `TSingleton<FConfig>`; `Get()` is defined in `Private/Config.cpp` (process-unique inside Config.dll).
  - Internal `std::map<Section, std::map<Key, std::string>>`; values are always strings, parsed to int/float/bool on read.
  - Lock-free - `Load` and read/write should complete single-threaded during initialization.
- Follow root [AGENTS.md](../../../../AGENTS.md).
