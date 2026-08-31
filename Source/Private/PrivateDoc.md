# Private

## Sub Layers

- [Core](Core/CoreDoc.md) — Core 实现（3 个 cpp）
- [Engine](Engine/EngineDoc.md) — Engine 实现（2 个 cpp）

## Concept -- Implementation Layer

`Source/Private/` 存放引擎核心的编译单元实现，与 `Source/Public/` 的头文件一一对应：

| 模块 | cpp | 实现内容 |
|------|-----|----------|
| Core | `Core/Assembly.cpp` | FAssembly 占位（现为 header-only） |
| Core | `Core/Fatal.cpp` | 致命路径 / 崩溃兜底 |
| Core | `Core/TaskGraph.cpp` | 依赖图调度器 |
| Engine | `Engine/Engine.cpp` | 主循环 / 命令行解析 / 退出标志 |
| Engine | `Engine/Layer.cpp` | FLayerBase 析构 / 运行时依赖声明 |

其余 Core/Engine 基础设施（TypeList/ThreadPool/ThreadedServer/Layer 模板等）均为 header-only，无实现单元。

## Related Docs

- [Public/PublicDoc.md](../Public/PublicDoc.md) — 公开层
- [SourceDoc.md](../SourceDoc.md) — 源码根
