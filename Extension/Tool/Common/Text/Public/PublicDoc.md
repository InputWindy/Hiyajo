# Public

## 代码文件

- [Text.h](Text.h)
- [TextApi.h](TextApi.h)

## 接口字典

| `FText` | 本地化句柄（Namespace/Key/Source + Resolve） |
| `FTextManager : TExtensionList<FTextManager>` | 本地化管理器单例 |
| `ExecuteStage(ETextStage)` | 阶段分发（Init/Shutdown） |
| `AddTranslation / LoadTranslationsFromJson / FindTranslation` | 翻译目录操作 |
| `GetCulture / SetCulture` | 当前 culture |

## 相关文档

- [../Text.md](../Text.md) — 概念 + 用法
- [../Private/PrivateDoc.md](../Private/PrivateDoc.md) — 实现算法字典
