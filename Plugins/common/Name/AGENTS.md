# Name — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 职责边界：全局字符串驻留池。需要稳定 O(1) 比较的字符串标识（如资源目录 key）用 `FName`；一次性字符串直接用 `std::string`，不要滥用驻留。
- 依赖只走 `.cplugin` `Dependencies`，include `<Name.h>`，不跨目录相对 include。
- 实现要点：
  - `FNamePool` 是 `TSingleton`，`Get()` 定义在 `Private/Name.cpp`（Name.dll 内进程唯一）。
  - `Intern` 线程安全（内部 mutex）；`FName` 是值类型，默认构造 Id==0 即 None。
- 遵循根 [AGENTS.md](../../../../AGENTS.md)。
