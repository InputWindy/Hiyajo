# Config

## Code files

- [Config.h](Config.h) - INI config singleton `FConfig`

## Concept - INI config

INI-style config singleton service (UE `DefaultEngine.ini` format) - `Load` parses an INI file into a two-level Section -> Key -> Value table, `GetString / GetInt / GetFloat / GetBool` read by Section/Key (with defaults), `SetString` overrides at runtime. Internal layered `std::map<std::string, FSection>`.

### FConfig - Section/Key reads

`TSingleton<FConfig>` + `IPlugin<IInit, IShutdown>` (Initialize/Shutdown clear `Sections`). Parsing rules:

- `[Section]` lines switch the current section; `Key = Value` lines are stored (trimmed).
- Lines starting with `;` / `#` are comments; blank lines are skipped.
- `GetBool` accepts `true / 1 / yes / on` (case-insensitive).

```cpp
FConfig::Get().Load("Engine/Config/DefaultEngine.ini");
const auto GameName = FConfig::Get().GetString("/Script/Engine.Engine", "GameName");
const std::int64_t MaxFPS = FConfig::Get().GetInt("SystemSettings", "MaxFPS", 60);
FConfig::Get().SetString("SystemSettings", "MaxFPS", "120");
```

## Third-party dependencies

- None (pure std).

## Related docs

- [API.html](API.html) - API documentation
