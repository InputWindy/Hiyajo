# Timer — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 职责边界：性能计时 + 游戏时钟。热点函数用 `FScopedTimer`；游戏逻辑时间用 `FGameClock`，不要自己搓 chrono。
- 依赖只走 `.cplugin` `Dependencies`，include `<Timer.h>`，不跨目录相对 include。
- 实现要点：
  - **两个单例**：`FTimer` 与 `FGameClock`，`Get()` 都定义在 `Private/Timer.cpp`（Timer.dll 内进程唯一）。
  - `FScopedTimer` 是栈式作用域计时，必须与 `BeginScope/EndScope` 配平。
  - `FGameClock` 惰性推进——`GetGameSeconds()` 时按距上次推进的 wall-clock delta × TimeScale 累积，无需每帧调用。
- 遵循根 [AGENTS.md](../../../../AGENTS.md)。
