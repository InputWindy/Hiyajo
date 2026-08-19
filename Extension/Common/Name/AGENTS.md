# Name — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 本插件职责：驻留字符串标识符池。就做这一件事，不越界。
- 插件 = 纯单例：`FNamePool : TExtensionList<FNamePool>`，无 Main、无 IAssembly（应用才继承）。
- `FName` 是普通值类型（非单例），驻留池由 `FNamePool` 单例管理；`std::hash<FName>` 特化基于 `GetId()`。
- 池存储（`GPool`/`GLookup`）是 `Name.cpp` 里的匿名命名空间全局，线程安全由文件内互斥锁保证。
- 依赖只走 `.cplugin` `Dependencies`，include `<Name.h>`，不跨目录相对 include。
- 无三方库：`Name.cmake` 保持占位注释，`settings.json` 的 `mirrors` 为空。
- 遵循根 [AGENTS.md](../../../AGENTS.md)。

## 文档

- [Name.md](Name.md) — 概念 + 用法
- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典

