# Log — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 职责边界：全引擎唯一日志出口。任何插件要打日志，走 `FLog::Get().Logger` / `FLog::Info/Warn/Error` / `MAHO_LOG_CORE_*` 宏，不要自建 logger。
- 依赖只走 `.cplugin` `Dependencies`，include `<Log.h>`，不跨目录相对 include。
- 实现要点：
  - `TSingleton<FLog>`，`Get()` 定义在 `Private/Log.cpp`（Log.dll 内进程唯一；依赖插件通过它链接 spdlog，不直接碰 spdlog）。
  - 静态 `Info/Warn/Error` 只是 `Get().Logger` 的透传；`MAHO_LOG_CORE_*` 宏同义。
  - `Initialize` 支持 `--log-level=`；`Shutdown` flush + 释放 logger。
- 遵循根 [AGENTS.md](../../../../AGENTS.md)。
