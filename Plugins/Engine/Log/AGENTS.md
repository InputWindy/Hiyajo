# Log - Agent Entry

All AI agents must read this file before entering this plugin.

## Design Constraints (strict)

- Responsibility boundary: the single log outlet for the whole engine. Any plugin that needs to log goes through `::Maho::GetLog()` / `MAHO_LOG_CORE_*` macros. Do not create your own logger.
- Dependencies only go through `.cplugin` `Dependencies`; include `<Log.h>`, no cross-directory relative includes.
- Implementation notes:
  - FLog is a **service layer** (`FLayer<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>`), not a singleton. `GetLog()` is defined in `Private/Log.cpp` (published at Initialize, cleared at Shutdown).
  - Instance `Trace/Debug/Info/Warn/Error/Critical` methods route to the internal spdlog logger; `MAHO_LOG_CORE_*` macros are synonymous (null-safe via `MAHO_ENSURE_NOT_NULL`).
  - `Initialize` supports `--log-level=`; `Shutdown` flushes + releases the logger.
- Follow the root [AGENTS.md](../../../../AGENTS.md).
