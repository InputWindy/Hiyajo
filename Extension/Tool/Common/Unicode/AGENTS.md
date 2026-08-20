# Unicode — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 本插件职责：Text encoding extension (UTF-8/16/32).
- 插件 = 纯单例：`FUnicode : TExtensionList<FUnicode>`，纯函数库（无 stage）
- 依赖只走 `.cplugin` `Dependencies`，include `<Name.h>`，不跨目录相对 include。
- 三方库由 `Unicode.cmake` 用 FetchContent 拉取；镜像配置在 `settings.json` 的 `mirrors`。
- 遵循根 [AGENTS.md](../../../AGENTS.md)。

## 文档

- [Unicode.md](Unicode.md) — 概念 + 用法
- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典
