# Maho — 引擎核心

引擎核心现在是**纯通用基础设施**，零 app 假设。聚合头 [Maho.h](Maho.h) 只转发到 [Core](Core/Core.md)。

## 核心不包含什么

曾经的 app 形态全部拆成了插件：

| 曾经的 core 内容 | 现在在哪 |
|-----------------|---------|
| `FEngineBase` / `EEngineStage` / `GApp` | `Engine` 插件 |
| `FToolkitBase` / `EToolStage` | `Toolkit` 插件 |
| `EntryPoint*`（`MahoMain`/`MahoCLIMain`） | 已删除（薄 launcher 取代） |

core 不知道"app 是什么"、"stage 有哪几个"、"怎么关机"——这些是插件层的职责。

## 核心包含什么

| 头 | 职责 |
|----|------|
| [Core/TypeList.h](Core/TypeList.h) | 编译期类型列表 `TTypeList` + 遍历 `ForEach` |
| [Core/Topology.h](Core/Topology.h) | 依赖声明（`TDependsPack`/`TPackConcat`）+ 编译期拓扑排序 / 环检测 |
| [Core/Delegate.h](Core/Delegate.h) | 单播 / 多播委托 |
| [Core/Extension.h](Core/Extension.h) | `IExtension<TStage>`（运行时接口）+ `TExtension` + 调度器 + `FExtensions` 装配 |
| [Core/Assembly.h](Core/Assembly.h) | `FAssembly` 动态加载原语（句柄 + 符号探测） |
| [Core/Fatal.h](Core/Fatal.h) | 致命路径 + 崩溃兜底 |
| [Core/Async/](Core/Async/Async.md) | `IRunable`（含 `RequestShutdown` + `GApp`）+ `FThreadPool` + `FThreadedServer` |

## 架构总览

```
core（纯通用）
  TExtension / IExtension / 调度器 / FAssembly / IRunable / 委托 / 拓扑
  ↑
插件（一切皆 assembly）
  Toolkit 插件（EToolStage + FToolkitBase）
  Engine 插件（EEngineStage + FEngineBase）
  AssemblyImporter 插件（统一 IExtension<TStage> 驱动）
  Log / Paths / Resource / ...（功能插件）
  ↑
项目（聚合 assembly + 薄 launcher）
  MyGameToolkit.dll + MyGameEngine.dll + MyGame.exe
```

## 相关文档

- [Core/Core.md](Core/Core.md) — 基础设施
- [Core/Async/Async.md](Core/Async/Async.md) — 并行模型 + 可运行契约
- [Maho.html](Maho.html) — API 文档
