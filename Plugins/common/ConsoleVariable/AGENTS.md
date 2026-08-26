# ConsoleVariable — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 职责边界：CVar 注册表。可调参数定义成 `TAutoConsoleVariable` 静态全局（static-init 自注册），运行时用 `FConsoleVariable::Get().Find(name)` 查询。
- 依赖只走 `.cplugin` `Dependencies`，include `<ConsoleVariable.h>`，不跨目录相对 include。
- 实现要点：
  - `TSingleton<FConsoleVariable>`，`Get()` 定义在 `Private/ConsoleVariable.cpp`（ConsoleVariable.dll 内进程唯一）。
  - 值存字符串、类型化访问时解析；`ReadOnly` 变量 `Set` 被忽略。
  - `Initialize` 后注册表可用，`Shutdown` 清空。
- 遵循根 [AGENTS.md](../../../../AGENTS.md)。
