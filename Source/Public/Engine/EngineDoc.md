# Engine

## 代码文件

- [Layer.h](Layer.h) — 层体系（header-only：FLayerBase / FLayer / 命令 / DispatchInstance / 插件宏）

## 概念——层架构

Engine 层只有 `Layer.h`，全 header-only（模板 + 内联，无 .cpp）。它定义了层体系：一个"层"是**可安装、可被驱动的运行时节点**。Core 的基建（FQueue/FParallelScheduler/TQuery）在这里被组装成层。

### FLayerBase —— 三个基建能力的组装基座

`FLayerBase` 是所有层实例的多态锚点（跨 DLL 单 vtable/RTTI，`MAHO_API`）。它继承三个类型无关的 Core 基建：

```cpp
class MAHO_API FLayerBase
    : public FQueue                          // ① 命令队列（catalog 分 lane 的 FIFO）
    , public Parallel::FParallelScheduler    // ② 并行执行（两个泛型 ForEach）
{
    template <typename TList> constexpr auto Query() const;  // ③ 类型无关编译期查询
    template <typename T> bool Is() const;                   // 运行时类型判断
    virtual void FlushCommands() {}                          // 默认空（FLayer override）
};
```

- **FQueue**：`Enqueue(unique_ptr<ICommand>)`（任意线程，按 `GetCatalogId()` 路由到 lane）+ `Dequeue(catalogId)`（FIFO 取出）。命令是纯数据载体（`ICommand` 仅 `GetCatalogId()`），**执行是消费方的职责**——`FlushCommands()` 在安全点 Drain 队列并应用。
- **FParallelScheduler**：`ForEach(FCallables...)`（编译期变参并行）+ `ForEach(Container, MakeTask)`（运行时容器并行），barrier 收尾。
- **Query\<TList\>()**：返回 `TQuery<TList>`（Core 的 Select/With/Not 编译期筛选），不依赖层状态。

### DispatchInstance —— 运行时分发

`DispatchInstance(TTypeList<Ts...>, FLayerBase* Instance, Visitor)` 对单个实例按候选类型列表 `dynamic_cast`，**第一个匹配**的类型交给 Visitor。用于"层内类型筛选"（实例属于哪层就驱动哪层）。

### FLayer —— 装配节点

`FLayer<TChildren...>` 继承 `FLayerBase`，持有已安装子实例（`Layers`）+ 已加载模块（`LoadedModules`）。它定义层命令类别 + 命令类型：

```cpp
enum class ELayerCommand : std::uint64_t { Install = 1, Uninstall = 2 };
struct FInstallCommand : ICommand { FLayerBase* Child; GetCatalogId() → Install; };
struct FUninstallCommand : ICommand { FLayerBase* Child; GetCatalogId() → Uninstall; };
```

生命周期与装配：

- **ctor**：遍历 `FLayers`（TChildren 类型表），对每个 `FModuleInstance<T>`（`T::GetModulePath()`）Load 其 DLL → Enqueue `FInstallCommand` → 末尾 `FlushCommands()` 立即装。
- **Install\<T\>()**：Load 子类型 → Enqueue Install 命令（延迟，待 FlushCommands）。
- **Uninstall(Child)**：Enqueue Uninstall 命令（延迟）。
- **FlushCommands()**（virtual，FLayer 层级的顶层虚）：从命令队列按 lane 取出命令——Install → `Layers.push_back(Child)`；Uninstall → erase + delete。**生命周期钩子（IInit/IShutdown）归用户显式驱动**，析构只释放内存。
- **Load(DLLPath)**：`FAssembly` Load DLL → `GetProcAs<CreateFunction>("CreateLayer")` 拿工厂 → `Create()` 构造子实例。

### ForEach —— 分层序遍历

`ForEach<FLevels>(Visitor)` 编译期分层序遍历，`if constexpr (LayerDetail::TAllSingleton<FLevels>::value)` 分流：

- **单例分支**（FLevels 全 `TSingleton` 类型表）：每层类型 `Visitor(T::Get())`，编译期遍历 + 层内并行。
- **实例分支**（FLevels 是实例类型表）：每层遍历 `Layers` 数组，`DispatchInstance` 匹配层类型，层内并行。
- 层间串行（barrier），层内并行（继承的 FParallelScheduler）。

`LayerDetail::TAllSingleton<FLevels>` 是编译期检测（`is_base_of<TSingleton<T>, T>` 递归），判断分层表是单例还是实例类型。

### 插件宏

```cpp
MAHO_DECLARE_LAYER(FCustomLayer, "MyLayer.dll")
// 展开：static FLayerBase* CreateLayer() { return new FCustomLayer(); }
//      + static std::string_view GetModulePath() { return "MyLayer.dll"; }
```

`Load` 经 `GetProcAs<CreateFunction>("CreateLayer")` 从 DLL 拿工厂（DLL 的 extern "C" bridge 返回 `FCustomLayer::CreateLayer()`）。

### 分层序（依赖）

子层依赖经 `MAHO_EXTEND_DEPS`（声明锚点）+ codegen 生成的 `FDepends` 表达。分层用 `Topo::TLevels_t<FTypeList, FDefaultSlot>` 计算（读每类 `FDepends`），传给 `ForEach<FLevels>` 驱动。参见 `Core/Topology.h` + `Core/Query.h`。

### 生命周期

`FLayer` 本身不强制 `IInit/IShutdown`——需要生命周期的层经 `IPlugin<IInit, IShutdown, ...>` 组合（Core/Interface.h），由用户在驱动循环里显式调用（可经 `ForEach<FLevels>` 统一驱动子层）。析构不静默 teardown。

## 相关文档

- [EngineAPI.html](EngineAPI.html) — API 文档
- [../Core/CoreDoc.md](../Core/CoreDoc.md) — Core 基建概念
- [../PublicDoc.md](../PublicDoc.md) — Public 根
