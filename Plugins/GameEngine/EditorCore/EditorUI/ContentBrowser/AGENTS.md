# ContentBrowser — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- Editor content browser panel - virtual-filesystem view of the project/engine Content roots (.casset only) with OS file-drop import.
- 依赖只走 `.cplugin` `Dependencies`，include `<Name.h>`，不跨目录相对 include。
- 遵循根 [AGENTS.md](../../../../AGENTS.md)。
