# Text — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 职责边界：本地化。面向用户展示的字符串做成 `FText`，不要直接散落硬编码；UI 文案统一经 `FTextManager` 目录管理。
- 依赖只走 `.cplugin` `Dependencies`，include `<Text.h>`，不跨目录相对 include。
- 实现要点：
  - `TSingleton<FTextManager>`，`Get()` 定义在 `Private/Text.cpp`（Text.dll 内进程唯一）。
  - 目录线程安全（mutex）；`FText::Resolve` 按当前 culture 查目录，缺失回退 Source。
  - JSON 翻译格式：`[{ "Namespace", "Key", "Culture", "Text" }, ...]`。
- 遵循根 [AGENTS.md](../../../../AGENTS.md)。
