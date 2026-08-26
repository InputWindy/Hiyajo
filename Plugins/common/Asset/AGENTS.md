# Asset — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 职责边界：资产编目。逻辑路径统一 `/Game/...`（无扩展名）；文件系统访问走 `Resolve/Load`，不要直接拼物理路径。
- 依赖只走 `.cplugin` `Dependencies`（`["Paths"]`），include `<Asset.h>`，不跨目录相对 include。
- 实现要点：
  - `TSingleton<FAssetRegistry>`，`Get()` 定义在 `Private/Asset.cpp`（Asset.dll 内进程唯一）。
  - 内部 `std::map<std::string, FAssetData>` + mutex；`Scan` 递归走目录、按扩展名定类型。
  - `Resolve` 依赖 Paths 插件（MountAlias 即根别名）。
- 遵循根 [AGENTS.md](../../../../AGENTS.md)。
