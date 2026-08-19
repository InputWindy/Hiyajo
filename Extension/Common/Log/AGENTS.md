# Log — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 本插件职责：日志（封装 spdlog）。就做这一件事，不越界。
- 插件 = 纯单例：`FLog : TExtensionList<FLog>`，无 Main、无 IAssembly（应用才继承）。
- **Public 头不泄露三方依赖**：`Log.h` 不含任何 spdlog 头，spdlog 只在 `Private/Log.cpp`。日志 API 用简单字符串（`const char*` + 函数），不把 fmt 模板暴露给宿主。
- 依赖只走 `.cplugin` `Dependencies`，include `<Name.h>`，不跨目录相对 include。
- 三方库由 `Log.cmake` 用 FetchContent 拉取；镜像配置在 `settings.json` 的 `mirrors`。
- 遵循根 [AGENTS.md](../../../AGENTS.md)。

## 文档

- [Log.md](Log.md) — 概念 + 用法
- [API.html](API.html) — API 文档
