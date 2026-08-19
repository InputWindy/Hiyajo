# Math

Math library extension (GLM).

## 扩展类

| 字段 | 值 |
|------|-----|
| Class | `Maho::Math::FMath` |
| Header | `Math.h` |
| Stage | 无（纯函数库，header-only） |
| Dependencies | — |

## 说明

数学库插件，基于 GLM（header-only）。提供类型别名（FVector2/3/4、FMatrix4、FQuaternion）和工具函数（Lerp、Clamp、弧度角度换算）。无生命周期。

## 用法

```cpp
#include <Math.h>
using namespace Maho::Math;

FVector3 Pos{1.0f, 2.0f, 3.0f};
Pos = Lerp(Pos, FVector3{0.0f}, 0.5f);
```

## 三方依赖

- GLM（`Math.cmake` FetchContent 拉取，header-only）

## 相关文档

- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典
- [../../AGENTS.md](../../AGENTS.md) — 引擎根 Agent 入口
- [../../Source/Public/Core/CoreDoc.md](../../Source/Public/Core/CoreDoc.md) — 核心基础设施
