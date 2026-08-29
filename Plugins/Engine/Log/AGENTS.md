# Log - Agent Entry

All AI agents must read this file before entering this plugin.

## Design Constraints (strict)

- Responsibility boundary: the single log outlet for the whole engine. Any plugin that needs to log goes through `FLog::Get().Logger` / `FLog::Info/Warn/Error` / `MAHO_LOG_CORE_*` macros. Do not create your own logger.
- Dependencies only go through `.cplugin` `Dependencies`; include `<Log.h>`, no cross-directory relative includes.
- Implementation notes:
  - `TSingleton<FLog>`, `Get()` is defined in `Private/Log.cpp` (process-unique inside Log.dll; dependent plugins link spdlog through it, and do not touch spdlog directly).
  - Static `Info/Warn/Error` just passthrough `Get().Logger`; `MAHO_LOG_CORE_*` macros are synonymous.
  - `Initialize` supports `--log-level=`; `Shutdown` flushes + releases the logger.
- Follow the root [AGENTS.md](../../../../AGENTS.md).
