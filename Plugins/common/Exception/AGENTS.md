# Exception - Agent entry

All AI agents read this file before entering this plugin.

## Design constraints (strict)

- Responsibility: central dispatch for non-fatal exceptions. Business code calls `ReportException` when it catches a recoverable error; for fatal errors use Core/Fatal, do not abort in this plugin.
- Dependencies only go through `.cplugin` `Dependencies`; include `<Exception.h>`, no cross-directory relative includes.
- Implementation notes:
  - `TSingleton<FException>`; `Get()` is defined in `Private/Exception.cpp` (process-unique inside Exception.dll).
  - Bundles a minimal `TMulticastEvent` (the engine has no standalone Delegate building block yet; keep it internal, do not share).
  - `ReportException` broadcasts synchronously to subscribers.
- Follow root [AGENTS.md](../../../../AGENTS.md).
