# Private

## 代码文件

- [Name.cpp](Name.cpp)

## 实现算法字典

Name 插件的实现集中在 `Name.cpp`——驻留池 + 线程安全。

| 函数 | 说明 |
|------|------|
| `FName::FName(string_view)` | 委托 `FNamePool::Get().Intern(Str)` 取得 Id |
| `FName::ToString()` | 从 `GPool[Id]` 取回字符串 |
| `FNamePool::ExecuteStage(ENameStage Stage)` | 加锁清池：`Init` 清空后预留 index 0（None）；`Shutdown` 清空 |
| `FNamePool::Intern(string_view Str)` | 空串 → `FName(0)`；加锁：池空则懒预留 index 0；已存在返回缓存 Id；否则追加池 + 建立 string→index 映射 |

**线程安全**——池用 `std::mutex GPoolMutex` 保护；`GPool`（index→string）与 `GLookup`（string→index）在匿名命名空间，仅 `Name.cpp` 可见。

## 相关文档

- [../Name.md](../Name.md) — 概念 + 用法
- [../Public/PublicDoc.md](../Public/PublicDoc.md) — 接口层

