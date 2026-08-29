# ConsoleVariable - Agent entry

All AI agents read this file before entering this plugin.

## Design constraints (strict)

- Responsibility: CVar registry. Tunable parameters are defined as `TAutoConsoleVariable` static globals (self-registering at static-init); query them at runtime with `FConsoleVariable::Get().Find(name)`.
- Dependencies only go through `.cplugin` `Dependencies`; include `<ConsoleVariable.h>`, no cross-directory relative includes.
- Implementation notes:
  - `TSingleton<FConsoleVariable>`; `Get()` is defined in `Private/ConsoleVariable.cpp` (process-unique inside ConsoleVariable.dll).
  - Values are stored as strings and parsed on typed access; `Set` is ignored for `ReadOnly` variables.
  - The registry is usable after `Initialize`; `Shutdown` clears it.
- Follow root [AGENTS.md](../../../../AGENTS.md).
