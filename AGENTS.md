# Maho — Agent 入口（引擎核心）

所有 AI Agent 进本引擎前先读本文件。

## 设计约束（强约束）

- 本引擎是**纯脚手架**——零 app 假设、零三方依赖、零 stage 预设。一切具体功能都是可安装插件。
- **分层**：
  - **Core**（`Source/Public/Core/`）：类型无关基建积木——`TypeList`（TTypeList + 操作）、`Topology`（TLevels_t 分层/环检测）、`Query`（TQuery Select/With/Not）、`Queue`（FQueue 类型无关命令队列）、`Singleton`（TSingleton 标识基类）、`Interface`（IInit/IShutdown/IMain/IExit/IPlugin 能力接口）、`Schedulers`（FParallelScheduler 泛型并行）、`ThreadPool`、`ThreadedServer`、`Extension`（MAHO_EXTEND_DEPS 依赖锚点）、`Assembly`（FAssembly DLL 加载）、`Fatal`。
  - **Engine**（`Source/Public/Engine/`）：层体系——`Layer.h`（`FLayerBase`/`FLayer`/命令/`DispatchInstance`/`MAHO_DECLARE_LAYER`）。**Engine 只组装 Core 基建，不含具体服务**。
  - **Plugins**（`Plugins/`）：可安装插件——`Common/`（服务插件：TSingleton 或纯库）+ `Gameplay/`（Layer 插件）。引擎零 app 假设，逻辑全在插件。
- **FLayerBase = 三基建组装**：继承 `FQueue`（命令队列）+ `FParallelScheduler`（并行）+ 提供 `Query<TList>()`（编译期筛选）。类型无关，无模板负担。
- **FLayer<TChildren...>**：装配节点——ctor 遍历 FLayers Load 子 DLL，Install/Uninstall 经 **Enqueue 命令**（延迟到 `FlushCommands` 安全点执行），`ForEach<FLevels>` 分层序遍历（编译期单例/实例双分支）。
- **能力可选组合**：`IInit`/`IShutdown`/`IMain`/`IExit` 经 `IPlugin<Caps...>` 显式组合——不是所有对象都需要生命周期。`TSingleton<T>` 是纯标识基类，无强制接口。
- **单例进程唯一**：`static T& Get()` 声明在 Public 头、定义在 Private cpp（编进该插件 DLL）——实例归自己 DLL 进程唯一。跨 DLL 经 dllimport 符号，非 header inline（避免每 DLL 一份）。
- **依赖声明**：`MAHO_EXTEND_DEPS(Class, Key, (Parent, deps...))` 是通用声明锚点（codegen 扫描 → `.gen.h` 宏填 `FDepends`），分层用 `Topo::TLevels_t<FTypeList, Key>`。`.cplugin` `Dependencies` 是构建级（编译 target + include），不填 FLayer 模板。
- **生命周期归宿主**：FLayer 析构只释放内存，不静默 teardown。init/shutdown 由用户经 `ForEach<FLevels>` 或显式驱动 `IInit`/`IShutdown` 能力。

## 接口分层

**读接口 public，能力/写接口 public**（不做"仅宿主可写"写保护）。多线程安全由各对象内部保证（锁/队列）。

- **TSingleton 服务**：`Get()` 进程唯一（头声明 + cpp 定义）；生命周期经 `IPlugin<IInit,IShutdown>` 可选组合。
- **纯库**（Archive/Compress/Unicode）：自由函数/类，无单例、无状态，不加生命周期。
- **FLayer**：安装/卸载经命令队列（Enqueue → FlushCommands 延迟），不直接改 Layers。ForEach<FLevels> 驱动子层（分层并行）。

## 驱动机制

- **命令队列**（FQueue）：生产者任意线程 `Enqueue(unique_ptr<ICommand>)`（按 `GetCatalogId()` 路由 lane），消费者安全点 `Dequeue(catalogId)` 后自行执行。命令是纯数据载体。
- **并行**（FParallelScheduler）：`ForEach(FCallables...)` / `ForEach(Container, MakeTask)`，barrier 收尾。分层语义是调用方（FLayer）的事。
- **编译期筛选**（TQuery）：`Query<TList>().Select<...>().With<...>().Not<...>().FResult` 产筛后类型表。
- **分层遍历**（FLayer::ForEach<FLevels>）：`if constexpr` 检测 FLevels 是否全单例——单例走 `T::Get()`，实例走 `Layers` + DispatchInstance。

## 项目侧开发约束（强约束）

拓展项目侧代码时，遵守以下三条：

### ① 接口定义与实现分离

- **定义接口**：全部写到项目入口插件的 `Public/` 目录下，按功能分好文件夹层级。
- **实现接口**：新建一个插件写到入口插件的**外面**，在它自己的 `Private/` 里实现。
- 入口插件**不关心任何接口的实现原理**，只负责调度。

### ② 代码实现用工具创建

- 新建插件请调用 `CreatePlugin.bat` / `Tools/create_plugin_ui.py` 自动创建，**不要手写目录/`.cplugin`**。
- 工具会生成 `Public/` + `Private/` + `.cplugin`，并自动把父插件加进 `Dependencies`。

### ③ 三方插件整体安装到 Extension

- 外部第三方插件包**整个安装到项目侧 `Extension/`**，构建时自动补 include + DLL target。

**沙漏依赖**：引擎 → 项目入口插件 → 功能子插件。入口插件是唯一宿主（继承 FLayerBase 并导出 `CreateLayer()`）。

## 文档

- [Source/SourceDoc.md](Source/SourceDoc.md) — 源码根（分层）
- [Source/Public/Core/CoreDoc.md](Source/Public/Core/CoreDoc.md) — Core 基建概念
- [Source/Public/Engine/EngineDoc.md](Source/Public/Engine/EngineDoc.md) — 层架构（FLayerBase/FLayer/命令/ForEach）
- [Source/Public/PublicDoc.md](Source/Public/PublicDoc.md) — Public 根
