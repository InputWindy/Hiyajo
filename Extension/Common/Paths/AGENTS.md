# Paths — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 本插件职责：路径解析（别名 → 根路径）。就做这一件事，不越界。
- 插件 = 纯单例：`FPaths : TExtensionList<FPaths>`，无 Main、无 IAssembly（应用才继承）。
- 纯标准库（`<filesystem>` / `<map>` / `<string>`），无三方依赖。
- 依赖只走 `.cplugin` `Dependencies`，include `<Name.h>`，不跨目录相对 include。
- 无三方库：`Paths.cmake` 保持占位注释，`settings.json` 的 `mirrors` 为空。
- 遵循根 [AGENTS.md](../../../AGENTS.md)。

## 文档

- [Paths.md](Paths.md) — 概念 + 用法
- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典

