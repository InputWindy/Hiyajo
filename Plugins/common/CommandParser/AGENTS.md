# CommandParser — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 职责边界：命令行解析。宿主在初始化阶段解析一次；各插件启动参数统一从这里查，不要重复解析 argc/argv。
- 依赖只走 `.cplugin` `Dependencies`，include `<CommandParser.h>`，不跨目录相对 include。
- 实现要点：
  - `TSingleton<FCommandParser>`，`Get()` 定义在 `Private/CommandParser.cpp`（CommandParser.dll 内进程唯一）。
  - 底层用 CLI11（引擎三方）；值以字符串存储，`GetBool/GetInt` 读取时解析，缺省回退 0/false/空串。
  - 注意：静态访问器 `Get()` 与查询接口 `Get(string_view)` 同名但参数不同，可合法共存。
- 遵循根 [AGENTS.md](../../../../AGENTS.md)。
