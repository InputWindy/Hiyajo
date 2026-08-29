# ConsoleVariable

## Code files

- [ConsoleVariable.h](ConsoleVariable.h) - console variable registry (`ECVarFlags` / `ECVarType` / `IConsoleVariable` / `FConsoleVariable` / `TAutoConsoleVariable`)

## Concept - console variables

Console variable registry (UE `IConsoleManager` style) - static `TAutoConsoleVariable` globals self-register at static-init; `Find` queries by name. Values are stored as strings and parsed on typed access (`GetInt/GetFloat/GetBool/GetString`). `Shutdown` clears the registry (without destroying the static-init registered globals).

### FConsoleVariable - registry singleton

`TSingleton<FConsoleVariable>` + `IPlugin<IInit, IShutdown>`. `Register(Name, Type, DefaultValue, Description, Flags)` registers and returns the interface (used by `TAutoConsoleVariable`); `Find(Name)` queries, returns `nullptr` when unregistered.

### TAutoConsoleVariable\<T\> - static self-registration

Template; `T` = int/float/bool/`std::string` (mapped to `ECVarType` via `TCVarType<T>` specializations). Constructing registers (`FConsoleVariable::Get().Register`); `GetValue()` reads typed, `Set(v)` writes back.

```cpp
static TAutoConsoleVariable<int> CVarMaxFPS("r.MaxFPS", 60, "Max FPS");
const int MaxFPS = CVarMaxFPS.GetValue();
CVarMaxFPS.Set(120);
```

### IConsoleVariable - variable interface

`GetName / GetDescription / GetFlags` + typed read/write. `Set` parses from a string; variables with `ECVarFlags::ReadOnly` cannot be changed at runtime (`Set` silently ignored).

## Third-party dependencies

- None (pure std).

## Related docs

- [API.html](API.html) - API documentation
