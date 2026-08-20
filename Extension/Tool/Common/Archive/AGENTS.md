# Archive — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 本插件职责：二进制序列化（raw bytes ↔ typed data）。就做这一件事，不越界。
- **纯函数库**：无生命周期、无单例、无 `TExtensionList`、无 stage。全是值类（`FArchive` 基类 + `FMemoryReader`/`FMemoryWriter`）。
- 依赖只走 `.cplugin` `Dependencies`，include `<Name.h>`，不跨目录相对 include。
- 无三方依赖，`Archive.cmake` 保持占位注释。
- 遵循根 [AGENTS.md](../../../AGENTS.md)。

## 文档

- [Archive.md](Archive.md) — 概念 + 用法
- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典

