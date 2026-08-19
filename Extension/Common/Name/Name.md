# Name

Interned string identifier pool.

## 扩展类

| 字段 | 值 |
|------|-----|
| Class | `Maho::Name::FNamePool` |
| Header | `Name.h` |
| Stage | `ENameStage`（本插件自定义） |
| Dependencies | — |

## 说明

内部字符串标识符池插件。`FName` 是驻留的不可变字符串标识——构造时把字符串驻留（intern）进全局池，相同字符串共享同一池条目，比较为 O(1)。默认构造的 `FName` 是 `None`（空，Id=0）。`FNamePool` 单例管理全局驻留池（线程安全）。

### 驱动

`Init` 清空池并预留 index 0（None），`Shutdown` 清空池。

## 用法

```cpp
#include <Name.h>
using namespace Maho::Name;

const FName Bone = "head";
const FName Also = "head";   // 同一池条目
Bone == Also;                // true, O(1)
Bone.ToString();             // "head"
Bone.GetId();                // 驻留池索引（完美哈希键）
```

## 相关文档

- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典（cpp 函数表）
- [../../../AGENTS.md](../../../AGENTS.md) — 引擎根 Agent 入口
- [../../../Source/Public/Core/CoreDoc.md](../../../Source/Public/Core/CoreDoc.md) — 核心基础设施

