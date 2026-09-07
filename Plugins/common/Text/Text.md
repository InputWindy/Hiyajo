# Text

## Code files

- [Text.h](Public/Text.h) — 本地化文本（`FText` 句柄 + `FTextManager` 目录 + `Culture` 常量）
- [TextApi.h](Public/TextApi.h) — `MAHO_TEXT_API` 导出宏
- [Text.cpp](Private/Text.cpp) — 目录实现 + `CreateLayer` 导出

## Concept - Localized Text

Text 是本地化文本层：`FText` 句柄 + `FTextManager` 目录。`SetCulture` 选文化，`FText::Resolve()` 查当前文化的翻译并回退到源文本。依赖引擎固定的 JSON（nlohmann）做 `LoadTranslationsFromJson`。

### 1. FText - 本地化句柄（数据类）

`FText` 存 `{Namespace, Key, Source}`，不持有状态，`Resolve()` 经全局 `GetTextManager()` 查翻译。可复制、按 `{Namespace, Key}` 判等。

```cpp
using namespace Maho::Text;

// 启动时注册翻译（例如从配置文件）。
GetTextManager()->AddTranslation("MainMenu", "Title", Culture::Chinese, "主菜单");
GetTextManager()->AddTranslation("MainMenu", "Title", Culture::Japanese, "メインメニュー");

const FText Title = FText("MainMenu", "Title", "Main Menu");
GetTextManager()->SetCulture(std::string(Culture::Chinese));
const std::string Shown = Title.Resolve();   // "主菜单"
```

### 2. FTextManager - 目录（服务层）

`FTextManager : FLayer<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>`，6 个生命周期阶段。目录键 = `Namespace + 0x1f + Key + 0x1f + Culture`（单位分隔符避免 `"a"+"bc" == "ab"+"c"` 冲突）。

- **SetCulture**：切换当前文化，后续 Resolve 生效。
- **AddTranslation**：注册单条翻译（线程安全）。
- **LoadTranslationsFromJson**：批量加载 JSON 对象数组。
- **FindTranslation**：按 (Namespace, Key, Culture) 精确查；无则 nullptr。

### 3. 线程安全

目录读写在同一个 `Mutex` 下串行；`FText::Resolve` 内部走 `FindTranslation`，从任意线程调用安全。

## Third-party dependencies

- **nlohmann/json**（`Text.cmake` FetchContent，`LoadTranslationsFromJson` 解析用）
- 其他插件：无——`.cplugin` Dependencies = `[]`

## Related docs

- [API.md](API.md) - API documentation
