# Exception

## Code files

- [Exception.h](Exception.h) - non-fatal exception broadcast (`TMulticastEvent` + `FException`)

## Concept - exception broadcast

Non-fatal exception broadcast singleton service - `ReportException` fans messages out to every `OnException` subscriber (logging, telemetry, crash reporting). **This plugin never aborts**: fatal errors go through `Core/Fatal`; here we only "report to interested parties". The engine has no standalone delegate building block yet, so a minimal multicast event is bundled (`TMulticastEvent`, only needing `void(const std::string&)` broadcast).

### FException - report entry point (singleton service)

`TSingleton<FException>` + `IPlugin<IInit, IShutdown>` (Initialize/Shutdown clear subscriptions). `ReportException(std::string_view)` broadcasts directly; `ReportException(const std::exception&)` forwards `what()`.

### TMulticastEvent - minimal multicast event

Template `TMulticastEvent<void(Args...)>`: `Bind(FHandler)` appends a subscription (`std::function`), `Broadcast(Args...)` calls non-empty handlers in order, `RemoveAll()` clears. Lock-free - single-threaded broadcast semantics.

```cpp
Exception::FException::Get().OnException.Bind([](const std::string& M) {
    Log::Error("exception: {}", M);
});
Exception::FException::Get().ReportException("failed to load texture");
```

## Third-party dependencies

- None (pure std).

## Related docs

- [API.html](API.html) - API documentation
