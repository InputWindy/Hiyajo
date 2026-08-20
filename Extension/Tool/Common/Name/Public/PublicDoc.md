# Public

## 代码文件

- [Name.h](Name.h)
- [NameApi.h](NameApi.h)

## 接口字典

| 声明 | 说明 |
|------|------|
| `FName` | 驻留不可变字符串标识（值类型），O(1) 比较 |
| `FName::FName(string_view)` | 构造时驻留字符串进池 |
| `FName::ToString()` | 返回驻留的字符串 |
| `FName::IsNone()` | 是否为 None（Id==0） |
| `FName::GetId()` | 驻留池索引（完美哈希键） |
| `FNamePool : TExtensionList<FNamePool>` | 全局驻留池单例（纯单例，无 Main/IAssembly） |
| `FNamePool::ExecuteStage(ENameStage)` | 阶段分发（Init / Shutdown） |
| `FNamePool::Intern(string_view)` | 驻留字符串，返回规范 FName（线程安全） |
| `ENameStage` | 本插件自定义 drive stage |
| `std::hash<FName>` | 基于 `GetId()` 的特化 |

## 相关文档

- [../Name.md](../Name.md) — 概念 + 用法
- [../Private/PrivateDoc.md](../Private/PrivateDoc.md) — 实现算法字典

