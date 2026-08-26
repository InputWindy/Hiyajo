# Paths — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 职责边界：路径解析基础设施。读写文件不要硬编码物理路径，先 `SetRoot` 注册别名，再 `Resolve` 虚拟路径。
- 依赖只走 `.cplugin` `Dependencies`，include `<Paths.h>`，不跨目录相对 include。
- 实现要点：
  - `TSingleton<FPaths>`，`Get()` 定义在 `Private/Paths.cpp`（Paths.dll 内进程唯一）。
  - 内部 `std::map<std::string, std::filesystem::path> Roots`，无锁——`SetRoot/Resolve` 应在初始化期单线程完成。
  - `Resolve` 同时接受 `Alias/Sub/Path` 与 `Alias:Sub/Path` 两种写法。
- 遵循根 [AGENTS.md](../../../../AGENTS.md)。
