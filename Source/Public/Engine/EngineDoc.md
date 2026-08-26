<!-- mahogen -->
# Engine

## 代码文件

- [Layer.h](Layer.h)
<!-- mahogen end -->

## 层架构（FLayerBase / FLayer）

Engine 层只有 `Layer.h` —— 层体系：一个"层"是**可安装/可被驱动的运行时节点**（`FLayerBase`），加上带编译期子层表的装配节点（`FLayer<TChildren...>`）。Core 的 `Schedulers.h` / `ThreadPool.h` / `ThreadedServer.h` 已迁入 Core（类型无关基建）。

### FLayerBase — 三个基建能力的组装基座

`FLayerBase` 继承三个类型无关的 Core 基建，是所有层实例的多态锚点（跨 DLL 单 vtable/RTTI，`MAHO_API`）：

```cpp
class MAHO_API FLayerBase
    : public FQueue                          // ① 命令队列（catalog 分 lane 的 FIFO，多线程 Enqueue）
    , public Parallel::FParallelScheduler    // ② 并行执行（两个泛型 ForEach）
{
    template <typename TList> constexpr auto Query() const;  // ③ 类型无关编译期查询（Core::TQuery）
    template <typename T> bool Is() const;                   // 运行时 dynamic_cast 类型判断
};
```

- **FQueue**：`Enqueue(unique_ptr<ICommand>)`（任意线程，按 `GetCatalogId()` 路由到 lane）+ `Dequeue(catalogId)`（FIFO 取出）。命令是纯数据载体（`ICommand` 仅 `GetCatalogId()`），**执行是消费方的职责**。
- **FParallelScheduler**：`ForEach(FCallables...)`（编译期变参并行）+ `ForEach(Container, MakeTask)`（运行时容器并行），barrier 收尾。
- **Query\<TList\>()**：返回 `TQuery<TList>`（Core 的 Select/With/Not 编译期筛选），不依赖层状态。

### DispatchInstance — 运行时分发

`DispatchInstance(TTypeList<Ts...>, FLayerBase* Instance, Visitor)` 对单个实例按候选类型列表 `dynamic_cast`，**第一个匹配**的类型交给 Visitor。用于"层内类型筛选"（实例属于哪层就驱动哪层）。

### FLayer — 装配节点

`FLayer<TChildren...>` 继承 `FLayerBase`，持有已安装子实例（`Layers`）+ 已加载模块（`LoadedModules`）：

- **ctor**：遍历 `FLayers`（TChildren 类型表），对每个 `FModuleInstance<T>`（`T::GetModulePath()`）Load 其 DLL → Enqueue `FInstallCommand` → 末尾 `FlushCommands()` 立即装。
- **Install\<T\>()**：Load 子类型 → Enqueue Install 命令（延迟，待 FlushCommands）。
- **Uninstall(Child)**：Enqueue Uninstall 命令（延迟）。
- **FlushCommands()**（virtual，FLayer 层级的顶层虚）：从命令队列按 lane 取出 Install/Uninstall 命令；Install → `Layers.push_back(Child)`；Uninstall → erase + delete。**生命周期钩子（IInit/IShutdown）归用户显式驱动**，析构只释放内存。
- **ForEach\<FLevels\>(Visitor)**：编译期分层序遍历。`if constexpr (LayerDetail::TAllSingleton<FLevels>::value)` 分流：
  - **单例分支**（FLevels 全 `TSingleton` 类型表）：每层类型 `Visitor(T::Get())`，编译期遍历 + 并行。
  - **实例分支**（FLevels 是实例类型表）：每层遍历 `Layers` 数组，`DispatchInstance` 匹配层类型，并行执行层内实例。
  - 层间串行（barrier），层内并行（继承的 FParallelScheduler）。

### 命令类型

```cpp
enum class ELayerCommand : std::uint64_t { Install = 1, Uninstall = 2 };
struct FInstallCommand : ICommand { FLayerBase* Child; GetCatalogId() → Install; };
struct FUninstallCommand : ICommand { FLayerBase* Child; GetCatalogId() → Uninstall; };
```

### 插件宏

```cpp
MAHO_DECLARE_LAYER(FCustomLayer, "MyLayer.dll")
// 展开：static FLayerBase* CreateLayer() { return new FCustomLayer(); }
//      + static std::string_view GetModulePath() { return "MyLayer.dll"; }
```

`Load` 经 `FAssembly::GetProcAs<CreateFunction>("CreateLayer")` 从 DLL 拿工厂（DLL 的 extern "C" bridge 返回 `FCustomLayer::CreateLayer()`）。

### 生命周期

`FLayer` 本身不强制 IInit/IShutdown —— 需要生命周期的层经 `IPlugin<IInit, IShutdown, ...>` 组合（Core/Interface.h），由用户在驱动循环里显式调用（可经 `ForEach<FLevels>` 统一驱动子层）。

### 分层序（依赖）

子层之间的依赖经 `MAHO_EXTEND_DEPS`（声明锚点）+ codegen 生成的 `FDepends`（`TTypeList<Key, TTypeList<deps...>>`）表达。分层用 `Topo::TLevels_t<FTypeList, FDefaultSlot>` 计算（读每类的 `FDepends`），传给 `ForEach<FLevels>` 驱动。参见 `Core/Topology.h` + `Core/Query.h`。

## 相关文档

- [../Core/CoreDoc.md](../Core/CoreDoc.md) — 核心基础设施
- [EngineAPI.html](EngineAPI.html) — API 文档
- [../PublicDoc.md](../PublicDoc.md) — Public 根
