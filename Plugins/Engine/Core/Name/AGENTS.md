# Name - Agent entry

All AI agents read this file before entering this plugin.

## Design constraints (strict)

- Responsibility: global string interning pool. Use `FName` for string identifiers that need stable O(1) comparison (e.g. resource directory keys); use `std::string` directly for one-shot strings, do not overuse interning.
- Dependencies only go through `.cplugin` `Dependencies`; include `<Name.h>`, no cross-directory relative includes.
- Implementation notes:
  - `FNamePool` is a `TSingleton`; `Get()` is defined in `Private/Name.cpp` (process-unique inside Name.dll).
  - `Intern` is thread-safe (internal mutex); `FName` is a value type, default-constructed Id==0 means None.
- Follow root [AGENTS.md](../../../../AGENTS.md).
