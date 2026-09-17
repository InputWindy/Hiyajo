# Engine（Private）

## Code Files

- [Engine.cpp](Engine.cpp) — FEngineBase 主循环 / 命令行解析 / PreMain·PostMain 生命周期 / 退出闸门
- [Layer.cpp](Layer.cpp) — FFrameExtension 虚析构 / `GetDependencies()` / 按名字寻址的依赖落点

## Concept -- Implementation

Engine 层主体（`FFrameExtension` / `FLayer` / `FLayerTaskGraph` / `LayerCollector`）是模板 + 内联实现，全在 `Source/Public/Engine/` 头文件里。Private 侧只有两个 cpp：

- **Engine.cpp**：`FEngineBase` 的非模板成员——命令行解析（CLI11）、`PreMain`（装载 + 初始化）/ `PostMain`（卸载 + 排干）、`Main()`（纯调度 Tick 环：帧边界应用挂起更新、拓扑变更时重展开、`SubmitFrame`、读 `ShouldExit()`）、`RequestExit()`、KV 读取。
- **Layer.cpp**：`FFrameExtension` 的虚析构、`GetDependencies()`、以及按名字寻址的 `WaitFor` / `BlockOn` 落点（`private`，只由依赖 DSL 的 builder 调用）。

逐函数伪代码见 [EngineAPI.md](EngineAPI.md)。

## Related Docs

- [EngineAPI.md](EngineAPI.md) — 实现算法字典
- [公开 API](../../Public/Engine/EngineAPI.md) — 签名入口
- [EngineDoc.md](../../Public/Engine/EngineDoc.md) — 层架构
