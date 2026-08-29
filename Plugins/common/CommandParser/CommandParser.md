# CommandParser

## Code files

- [CommandParser.h](CommandParser.h) - command-line parsing singleton `FCommandParser`

## Concept - command-line parsing

Command-line parsing singleton service - parses `argc/argv` into a **key -> value store** for startup configuration queries. Both `--key value` and `--key=value` forms are supported; bare flags (no value) are recorded as `true`; non-hyphen arguments (positional) are ignored. The backend is **CLI11** (engine third-party, header-only), which does the real tokenization / quoted-value handling.

### FCommandParser - key-value store

`TSingleton<FCommandParser>` + `IPlugin<IInit, IShutdown>`. `Initialize(argc, argv)` is `Parse` (idempotent; later writes win); `Shutdown` clears. Query API: `Has / Get / GetBool / GetInt` (bool accepts `true/1/yes/on`; int parse failure falls back to 0), `GetAll()` returns all key-value pairs.

```cpp
FCommandParser::Get().Initialize(argc, argv);
const std::string Name = FCommandParser::Get().Get("name");
const bool Verbose = FCommandParser::Get().GetBool("verbose");
```

Internally each `-key` is normalized to a `--key` long option and handed to CLI11 for per-option declaration and parsing; bad input does not abort (best effort to read back what parsed).

## Third-party dependencies

- **CLI11** (`CLI/CLI.hpp`, engine third-party header-only) - option tokenization and quoted-value handling.

## Related docs

- [API.html](API.html) - API documentation
