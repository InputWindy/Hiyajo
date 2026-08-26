# Resource — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 职责边界：异步资源导入/导出。新资源类型 = 特化 `TResourceImporter/Exporter<T>`（纯编解码，见字节不见线程）；宿主负责生命周期：`Initialize` 启 IO 线程、每帧 `Tick`、`Shutdown` 停线程清目录。
- 依赖只走 `.cplugin` `Dependencies`（`["Name", "Paths"]`），include `<Resource.h>`，不跨目录相对 include。
- 实现要点：
  - `TSingleton` + `FThreadedServer`，`Get()` 定义在 `Private/Resource.cpp`（Resource.dll 内进程唯一）。
  - **Import**：IO 线程读文件 → 游戏线程 `Tick` 里解码（Importer 在 game thread 被调，可安全碰目录/状态）。
  - **Export**：调用方线程编码（Exporter 同步读目录资源，勿跨线程共享）→ IO 线程写盘 → `OnDone` 经 `Tick` 回游戏线程；调用方须保持资源存活且不变直到完成。
  - 传输细节（`FTransferState` / `FBulkData` / `FTransferHandle`）全在 cpp 内部，头文件只前向声明。
- 遵循根 [AGENTS.md](../../../../AGENTS.md)。
