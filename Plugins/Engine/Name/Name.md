# Name

## Code files

- [Name.h](Public/Name.h) — 名称池头：`FName` + `FNamePool` + `std::hash` 特化
- [NameApi.h](Public/NameApi.h) — DLL 导出宏 `MAHO_NAME_API`
- [Name.cpp](Private/Name.cpp) — 驻留算法 + 跨 DLL 访问器 `GetNamePool`

## Concept - Interned String Identifiers

Name 提供 UE FName 式的**字符串驻留**：把字符串存进全局池一次，后续以 `uint32_t` id 引用。相同字符串共享同一池条目 → 比较 / 散列是 O(1) 整数运算，比 `strcmp` 快得多。适合"反复出现、需快速相等比较"的标识（骨骼名、资源名、消息名）。

### 1. 驻留（Intern）

`FNamePool::Intern(Str)` 线程安全：查 `Lookup`（`unordered_map<string, uint32_t>`）；命中返回既有 id，未命中追加进 `Pool` 并记新 id。**空串不驻留**——直接返回 None（`Id == 0`），None 同时是"空"与"未设置"的标记。

```cpp
#include <Name.h>

using namespace Maho;

const Name::FName Bone = "head";
const Name::FName Also = "head";   // 同一池条目
Bone == Also;                       // true，O(1)

Bone.ToString();                    // "head"
Bone.IsNone();                      // false
Name::FName{}.IsNone();             // true（默认构造）
```

### 2. 生命周期与前提

`FNamePool` 须先经引擎层系统初始化（`GetNamePool()` 非空）——`FName(Str)` 构造和 `ToString()` 都依赖它。`Initialize` 先清空池，`Shutdown` 清空池并撤回。

## Third-party dependencies

- None (pure std).

## Related docs

- [API.md](API.md) - API documentation
- [ImplAPI.md](ImplAPI.md) - 实现算法字典
- [EngineDoc.md](../../../Source/Public/Engine/EngineDoc.md) - 层架构
