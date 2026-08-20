# Json

JSON serialization extension (nlohmann/json)。

## 扩展类

| 字段 | 值 |
|------|-----|
| Class | `Maho::Json::FJson` |
| Header | `Json.h` |
| Stage | 无（header-only，无生命周期） |
| Dependencies | — |

## 说明

JSON 插件，封装 nlohmann/json（header-only）。提供 `FJsonValue` 类型别名——消费者 include `<Json.h>` 后直接用它做序列化/反序列化。

插件本身无生命周期：单例 `FJson` 只是个类型提供者，不需要 Init / Shutdown，也没有 stage。

## 用法

```cpp
#include <Json.h>
using namespace Maho::Json;

FJsonValue Doc;
Doc["name"] = "player";
Doc["hp"] = 100;

std::string Serialized = Doc.dump();
FJsonValue Parsed = FJsonValue::parse(Serialized);
```

## 三方依赖

- nlohmann/json（`Json.cmake` FetchContent 拉取，header-only）

## 相关文档

- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典
- [../../AGENTS.md](../../AGENTS.md) — 引擎根 Agent 入口
- [../../Source/Public/Core/CoreDoc.md](../../Source/Public/Core/CoreDoc.md) — 核心基础设施
