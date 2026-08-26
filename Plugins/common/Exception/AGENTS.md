# Exception — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 职责边界：非致命异常的中枢分发。业务代码捕获到可恢复错误就 `ReportException`；致命错误请用 Core/Fatal，不要在本插件 abort。
- 依赖只走 `.cplugin` `Dependencies`，include `<Exception.h>`，不跨目录相对 include。
- 实现要点：
  - `TSingleton<FException>`，`Get()` 定义在 `Private/Exception.cpp`（Exception.dll 内进程唯一）。
  - 自带最小 `TMulticastEvent`（引擎尚无独立 Delegate 积木，先内置，勿外借）。
  - `ReportException` 同步广播给订阅者。
- 遵循根 [AGENTS.md](../../../../AGENTS.md)。
