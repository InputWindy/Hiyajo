# CommandParser — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 本插件职责：命令行参数解析（键值存储）。就做这一件事，不越界。
- 插件 = 纯单例：`FCommandParser : TExtensionList<FCommandParser>`，无 Main、无 IAssembly（应用才继承）。
- 旧引擎的 `ExecuteStage` 和 `ParseCommandLine` 是 TODO 空实现，迁移时补齐真实逻辑（幂等解析）。
- 依赖只走 `.cplugin` `Dependencies`，include `<Name.h>`，不跨目录相对 include。
- 遵循根 [AGENTS.md](../../../AGENTS.md)。

## 文档

- [CommandParser.md](CommandParser.md) — 概念 + 用法
- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典
