# Text

Localized text (culture-aware).

## 扩展类

| 字段 | 值 |
|------|-----|
| Class | `Maho::Text::FTextManager` |
| Header | `Text.h` |
| Stage | `ETextStage`（本插件自定义） |
| Dependencies | Json |

## 说明

本地化文本插件。`FText` 是本地化句柄（Namespace + Key + Source），`FTextManager` 单例管当前 culture 和翻译目录。`Init` 清空目录重置 English，`Shutdown` 清空目录。依赖 Json（从 JSON 数组批量加载翻译）。

## 用法

```cpp
#include <Text.h>
using namespace Maho::Text;

FTextManager::Get().AddTranslation("MainMenu", "Title", "zh-CN", "主菜单");
FTextManager::Get().SetCulture("zh-CN");
const FText T("MainMenu", "Title", "Main Menu");
T.Resolve();  // "主菜单"
```

## 三方依赖

- Json（跨插件依赖，走 `.cplugin` `Dependencies`）

## 相关文档

- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典
- [../../AGENTS.md](../../AGENTS.md) — 引擎根 Agent 入口
- [../../Source/Public/Core/CoreDoc.md](../../Source/Public/Core/CoreDoc.md) — 核心基础设施
