# Text

## Code Files

- [Text.h](Text.h) - localized text (`Culture` / `FText` / `FTextManager`)

## Concept - Localized Text

Localized text singleton service - `FText` text handle + `FTextManager` translation catalog. `SetCulture` switches culture; `FText::Resolve()` looks up the current culture's translation and falls back to the source text when missing. Catalog key = `Namespace + US + Key + US + Culture` (US = 0x1f separator, prevents concatenation collisions). `Mutex` protected, thread-safe.

### FText - Text Handle

Stores a `{Namespace, Key, Source}` triple. `Resolve()` looks up a translation via `FTextManager::Get().FindTranslation(Namespace, Key, current culture)` and returns Source when absent. Equality compares Namespace + Key only.

### FTextManager - Catalog Manager (Singleton Service)

`TSingleton<FTextManager>` + `IPlugin<IInit, IShutdown>`:

- `SetCulture / GetCulture`: switch / read the current culture (default `en-US`).
- `AddTranslation(Namespace, Key, Culture, Text)`: register one translation (thread-safe).
- `LoadTranslationsFromJson(JsonText)`: bulk-load a JSON array `[{ "Namespace", "Key", "Culture", "Text" }, ...]` - parsed with **nlohmann/json**.
- `FindTranslation(...)`: lookup; `nullptr` when absent.

```cpp
using namespace Maho::Text;
FTextManager::Get().AddTranslation("MainMenu", "Title", Culture::Chinese, "Main Menu (zh)");
FTextManager::Get().AddTranslation("MainMenu", "Title", Culture::Japanese, "Main Menu (ja)");
const FText Title = FText("MainMenu", "Title", "Main Menu");
FTextManager::Get().SetCulture(std::string(Culture::Chinese));
const std::string Shown = Title.Resolve();   // "Main Menu (zh)"
```

`Culture` provides constants: `English = "en-US"` / `Chinese = "zh-CN"` / `Japanese = "ja-JP"` (strings are UTF-8).

## Third-Party Dependencies

- **nlohmann/json** (`nlohmann/json.hpp`, engine third-party header-only) - used by `LoadTranslationsFromJson`.

## Related Docs

- [API.html](API.html) - API docs
