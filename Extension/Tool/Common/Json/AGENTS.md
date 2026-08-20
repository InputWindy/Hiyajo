# Json — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 本插件职责：JSON 序列化（封装 nlohmann/json）。就做这一件事，不越界。
- 插件 = 纯单例：`FJson : TExtensionList<FJson>`，header-only 无生命周期（无 stage、无 ExecuteStage）。
- **暴露 FJsonValue 是有意的**——消费者要用 `nlohmann::json` 类型；除此之外不在 Public 头引入其他符号。
- 依赖只走 `.cplugin` `Dependencies`，include `<Name.h>`，不跨目录相对 include。
- 三方库由 `Json.cmake` 用 FetchContent 拉取；镜像配置在 `settings.json` 的 `mirrors`。
- 遵循根 [AGENTS.md](../../../AGENTS.md)。

## 文档

- [Json.md](Json.md) — 概念 + 用法
- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典
