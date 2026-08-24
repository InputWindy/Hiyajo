# Resoure — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- TODO: 插件职责边界
- 依赖只走 `.cplugin` `Dependencies`，include `<Name.h>`，不跨目录相对 include。
- 遵循根 [AGENTS.md](../../../../AGENTS.md)。
