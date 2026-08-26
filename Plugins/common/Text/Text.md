# Text

## 代码文件

- [Text.h](Text.h) — 本地化文本（`Culture` / `FText` / `FTextManager`）

## 概念——本地化文本

本地化文本单例服务——`FText` 文本句柄 + `FTextManager` 翻译目录。`SetCulture` 切换文化，`FText::Resolve()` 查当前文化的翻译、缺失时回退源文本（Source）。翻译目录 key = `Namespace + US + Key + US + Culture`（US = 0x1f 分隔符，防拼接碰撞），`Mutex` 保护、线程安全。

### FText —— 文本句柄

存 `{Namespace, Key, Source}` 三元组。`Resolve()` 经 `FTextManager::Get().FindTranslation(Namespace, Key, 当前文化)` 查翻译，无则返回 Source。相等比较只看 Namespace + Key。

### FTextManager —— 目录管理器（单例服务）

`TSingleton<FTextManager>` + `IPlugin<IInit, IShutdown>`：

- `SetCulture / GetCulture`：切换/读取当前文化（默认 `en-US`）。
- `AddTranslation(Namespace, Key, Culture, Text)`：注册一条翻译（线程安全）。
- `LoadTranslationsFromJson(JsonText)`：批量加载 JSON 数组 `[{ "Namespace", "Key", "Culture", "Text" }, ...]`——用 **nlohmann/json** 解析。
- `FindTranslation(...)`：查询，无则 `nullptr`。

```cpp
using namespace Maho::Text;
FTextManager::Get().AddTranslation("MainMenu", "Title", Culture::Chinese, "主菜单");
FTextManager::Get().AddTranslation("MainMenu", "Title", Culture::Japanese, "メインメニュー");
const FText Title = FText("MainMenu", "Title", "Main Menu");
FTextManager::Get().SetCulture(std::string(Culture::Chinese));
const std::string Shown = Title.Resolve();   // "主菜单"
```

`Culture` 提供常量：`English = "en-US"` / `Chinese = "zh-CN"` / `Japanese = "ja-JP"`（字符串均为 UTF-8）。

## 三方依赖

- **nlohmann/json**（`nlohmann/json.hpp`，引擎三方 header-only）——`LoadTranslationsFromJson` 用。

## 相关文档

- [API.html](API.html) — API 文档
