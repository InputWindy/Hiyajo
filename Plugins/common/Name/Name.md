# Name

## 代码文件

- [Name.h](Name.h) — 字符串驻留标识（`FName` + `FNamePool` + `std::hash` 特化）

## 概念——字符串驻留

Name 插件提供**不可变字符串标识符**——构造 `FName` 时把字符串 intern 进全局池，相同字符串共享同一条目，比较 O(1)（按内部 Id，无逐字节比较）。引擎里高频重复的字符串（资源目录 key、CVar 名等）用 FName 免去重复存储与比较开销。

### FName —— interned 标识符

默认构造 = None（空，`Id == 0`）。显式构造 `FName("head")` 走 `FNamePool::Get().Intern`；`ToString()` 反查池内字符串。

```cpp
const FName Bone = "head";
const FName Also = "head";       // 同一池条目
Bone == Also;                    // true，O(1)
```

配套 `std::hash<FName>` 特化（按 `GetId()`）——可直接作 `unordered_map` 的 key。

### FNamePool —— 全局驻留池（单例服务）

`TSingleton<FNamePool>` + `IPlugin<IInit, IShutdown>`，`Mutex` 保护，线程安全。`Intern` 幂等（已存在返回规范条目），`StringForId` 反查。Initialize/Shutdown 清池（`free()`）。

## 三方依赖

- 无（纯 std）。

## 相关文档

- [API.html](API.html) — API 文档
