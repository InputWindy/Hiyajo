# Private

## 代码文件

- [Text.cpp](Text.cpp)

## 实现算法字典

| `FText::Resolve` | 查目录，缺省回退 Source |
| `FTextManager::ExecuteStage` | Init: 清目录+重置 en-US；Shutdown: 清目录 |
| `MakeKey`（内部） | Namespace + US + Key + US + Culture，防碰撞 |
| `LoadTranslationsFromJson` | 用 Json 插件解析数组逐条 AddTranslation |

## 相关文档

- [../Text.md](../Text.md) — 概念 + 用法
- [../Public/PublicDoc.md](../Public/PublicDoc.md) — 接口字典
