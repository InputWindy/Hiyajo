# Log

## Code Files

- [Log.h](Log.h) - logging singleton `FLog` (wraps spdlog's stdout-color logger)
- [LogApi.h](LogApi.h) - cross-DLL export macro (`MAHO_LOG_API`)

## Concept - Log Service

Logging singleton service - wraps **spdlog**'s thread-safe stdout-color logger, fmt-style formatting (`{}` placeholders), shared by the whole engine (including other service plugins). `FLog` inherits `TSingleton<FLog>` + `IPlugin<IInit, IShutdown>`: `Initialize(argc, argv)` creates and configures the logger (recognizes `--log-level=trace|debug|info|warn|error`), `Shutdown()` flushes + releases.

### FLog - singleton + static passthrough

- `Logger` (public): shared `std::shared_ptr<spdlog::logger>`, all log output goes through it.
- Static passthrough: `FLog::Info / Warn / Error(fmt, args...)` (fmt style, compile-time format string checking).
- Macro sugar: `MAHO_LOG_CORE_TRACE / DEBUG / INFO / WARN / ERROR / CRITICAL(...)` - directly passthrough to `Logger->xxx`.

```cpp
FLog::Get().Initialize(argc, argv);
FLog::Info("init: {}", name);
FLog::Error("boom: code={}", code);
MAHO_LOG_CORE_INFO("init {}", name);
```

`Get()` is declared in the header, defined in `Log.cpp` (compiled into Log.dll) - process-unique instance. Dependent plugins link spdlog through Log.dll, no need to bundle the third-party dep.

## Third-Party Dependencies

- **spdlog** (header-only, `spdlog/spdlog.h` + stdout_color_sinks) - thread-safe stdout-color logging.

## Related Docs

- [API.html](API.html) - API documentation
