# Engine（Private）

## Code Files

- [Engine.cpp](Engine.cpp) — FEngineBase 主循环 / 命令行解析 / 退出标志
- [Layer.cpp](Layer.cpp) — FLayerBase 析构 / 运行时依赖声明

## Concept -- Implementation

Engine 层主体（`FLayerBase` / `FLayer` / `FLayerTaskGraph` / `LayerCollector`）是模板 + 内联实现，全在 `Source/Public/Engine/` 头文件里。Private 侧只有两个 cpp：

- **Engine.cpp**：`FEngineBase` 的非模板成员——命令行解析（CLI11）、`Main()` 主循环（Init 图 → Tick 循环 → Shutdown 图）、`RequestExit()`、KV 读取。
- **Layer.cpp**：`FLayerBase` 的虚析构、`GetDependencies()`、运行时字符串寻址的 `AddDependency` 重载。

逐函数伪代码见 [EngineAPI.md](EngineAPI.md)。

## Related Docs

- [EngineAPI.md](EngineAPI.md) — 实现算法字典
- [公开 API](../../Public/Engine/EngineAPI.md) — 签名入口
- [EngineDoc.md](../../Public/Engine/EngineDoc.md) — 层架构
