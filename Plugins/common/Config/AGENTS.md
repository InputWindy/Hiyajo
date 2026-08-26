# Config — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 职责边界：INI 配置读写。静态 ini 配置走本插件；启动参数类配置建议走 CommandParser。
- 依赖只走 `.cplugin` `Dependencies`，include `<Config.h>`，不跨目录相对 include。
- 实现要点：
  - `TSingleton<FConfig>`，`Get()` 定义在 `Private/Config.cpp`（Config.dll 内进程唯一）。
  - 内部 `std::map<Section, std::map<Key, std::string>>`，值恒为字符串，读取时解析为 int/float/bool。
  - 无锁——`Load` 与读写应在初始化期单线程完成。
- 遵循根 [AGENTS.md](../../../../AGENTS.md)。
