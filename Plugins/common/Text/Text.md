# Text

本地化文本单例——`FText` 文本句柄 + `FTextManager` 翻译目录；`SetCulture` 切文化，`FText::Resolve()` 查当前文化翻译、缺失回退源文本。

## 提供

- `Culture`：`en-US` / `zh-CN` / `ja-JP` 常量（字符串均为 UTF-8）。
- `FText`：`{Namespace, Key, Source}` 句柄——`Resolve()` / `GetNamespace/GetKey/GetSource` / 相等比较。
- `FTextManager`：`TSingleton<FTextManager>` + `IPlugin<IInit, IShutdown>`。
  - `GetCulture/SetCulture`；`AddTranslation`（线程安全）；`LoadTranslationsFromJson`；`FindTranslation`。

## 示例

```cpp
FTextManager::Get().AddTranslation("MainMenu", "Title", Culture::Chinese, "主菜单");
FTextManager::Get().SetCulture(std::string(Culture::Chinese));
const FText Title = FText("MainMenu", "Title", "Main Menu");
const std::string Shown = Title.Resolve();   // "主菜单"
```

## 依赖

- 三方：nlohmann/json（引擎三方，`LoadTranslationsFromJson` 用）。
- 其他插件：无。
