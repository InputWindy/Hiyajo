# Paths

Path resolution extension (project/engine roots).

## 扩展类

| 字段 | 值 |
|------|-----|
| Class | `Maho::Paths::FPaths` |
| Header | `Paths.h` |
| Stage | `EPathsStage`（本插件自定义） |
| Dependencies | — |

## 说明

路径解析插件。用别名（alias）注册根路径（如 `"Engine"` → 引擎目录），然后解析虚拟路径 `"Alias/Sub/Path"`（或 `"Alias:Sub/Path"`）到物理路径。别名未注册或虚拟路径无分隔符时，原样返回。

### 驱动

`Init` / `Shutdown` 都清空根路径表。

## 用法

```cpp
#include <Paths.h>
using namespace Maho::Paths;

FPaths::Get().SetRoot("Engine", "C:/Engine");
FPaths::Get().HasRoot("Engine");                 // true
FPaths::Get().Resolve("Engine/Source/Main.cpp"); // "C:/Engine/Source/Main.cpp"
```

## 相关文档

- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典（cpp 函数表）
- [../../../AGENTS.md](../../../AGENTS.md) — 引擎根 Agent 入口
- [../../../Source/Public/Core/CoreDoc.md](../../../Source/Public/Core/CoreDoc.md) — 核心基础设施

