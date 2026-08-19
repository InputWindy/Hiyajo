# Maho — Agent 入口（核心插件）

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 本插件是**引擎核心**——纯通用基础设施：零 app 假设、零三方依赖、零 stage 预设。
- 只提供积木：`TypeList`（`TTypeList` + `ForEach`）/ `Topology`（`TDependsOn`/`TDependsPack` + 排序/分层/环检测）/ `Delegate` / `Singleton` / `Scheduler`（`IScheduler` 空基 + 双 `Execute`）/ `Extension`（`TExtension` 单例 + `TExtensionList` 组装）/ `Assembly`（`IAssembly` + `FAssembly`）/ `Fatal`。
- **插件 = 纯单例**：`TExtension<TDerived>` 只继承 `TSingleton`，**不继承 `IAssembly`**；只有应用（宿主）显式继承 `IAssembly` 并导出 `CreateExtension()`。
- **驱动机制**：编译期 `ForEach`（`TTag<T>` + Scheduler 串/并行）+ `T::Get()` 单例直调；`IScheduler` 双 `Execute`——stage 版（硬编码 `ExecuteStage`）+ lambda 版（`FDefaultSlot` 排序 + Visitor 自定义）。
- **依赖声明两层**：编译期 `TDependsOn`/`TDependsPack`（插件内，level 排序）；项目装配 `.cplugin` `Dependencies`（CMake + codegen）。核心与核心之间走 `TDependsOn`，插件与宿主走 `.cplugin`。
- stage 枚举、app 形态、线程池策略**全部下放插件层**，不写回 core（`Engine/` 只放可选的 `FSerialScheduler`/`FParallelScheduler` 示例）。
- 遵循根 [AGENTS.md](../AGENTS.md)。

## 项目侧开发约束（强约束）

拓展项目侧代码时，遵守以下三条：

### ① 接口定义与实现分离

- **定义接口**：全部写到项目入口插件的 `Public/` 目录下，按功能分好文件夹层级。
- **实现接口**：新建一个插件写到入口插件的**外面**（`Source/<新插件名>/`），在它自己的 `Private/` 里实现。
- 入口插件**不关心任何接口的实现原理**，只负责在 `Main` 里调度。

```
Source/
  Main.cpp
  <入口插件>/            ← 只放接口（Public/）+ 调度（Private/Main）
    Public/FeatureA/IXxx.h
    Public/FeatureB/IYyy.h
    Private/<入口插件>.cpp   ← 只调度，不实现
  <功能插件>/            ← 接口实现（新插件，写入口外面）
    Public/  Private/
```

### ② 代码实现用工具创建

- 新建插件请调用项目侧的 `CreatePlugin.bat` 自动创建，**不要手写目录/`.cplugin`**。
- 工具会：生成 `Public/` + `Private/` + `.cplugin`，并自动把项目入口插件加进 `Dependencies`（锚定父插件）。

### ③ 三方插件整体安装到 Extension

- 要用外部的第三方插件包，**整个安装到项目侧的 `Extension/` 目录下**。
- 构建项目时（双击 `.cproject`）会自动补全代码依赖（include 路径 + DLL target），无需手动配。

**沙漏依赖**（背景）：引擎插件 → 项目入口插件 → 功能子插件。入口插件是唯一宿主——只有它继承 `IAssembly` 并导出 `CreateExtension()`，其余插件是纯 `TExtension` 单例（无 Main）。

## 文档

- [Source/SourceDoc.md](Source/SourceDoc.md) — 源码根（Public/Private 分工）
- [Source/Public/Core/CoreDoc.md](Source/Public/Core/CoreDoc.md) — 基础设施概念
- [Source/Public/Core/CoreAPI.html](Source/Public/Core/CoreAPI.html) — API 文档
- [Source/Public/Engine/EngineDoc.md](Source/Public/Engine/EngineDoc.md) — 调度策略（Serial/Parallel/ThreadPool）
