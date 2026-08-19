<!-- mahogen -->
# Core

## 代码文件

- [Assembly.h](Assembly.h)
- [Core.h](Core.h)
- [Delegate.h](Delegate.h)
- [Export.h](Export.h)
- [Extension.h](Extension.h)
- [Fatal.h](Fatal.h)
- [Scheduler.h](Scheduler.h)
- [Singleton.h](Singleton.h)
- [Topology.h](Topology.h)
- [TypeList.h](TypeList.h)
<!-- mahogen end -->

## 概念——引擎基础设施

Maho 核心是**零 app 假设、零三方依赖、零 stage 预设**的纯积木。不定义 stage 枚举、不定义 app 形态，调度策略全部下放插件。

### 核心抽象

**① `TExtension<TDerived>` —— 单例扩展**

```cpp
template <typename TDerived>
class TExtension : public TSingleton<TDerived>
{
public:
    virtual ~TExtension() = default;
};
```

一切可被驱动的东西都是它。只继承 `TSingleton`（CRTP Meyer's 单例）——**不继承 `IAssembly`**；插件不需要被动态加载，只有应用（宿主）显式继承 `IAssembly`。

**② `TExtensionList<TDerived, TExtensions...>` —— 单例 + 组装**

```cpp
template <typename TDerived, typename... TExtensions>
class TExtensionList
    : public virtual TExtension<TDerived>
    , public TTypeList<TExtensions...>
{
public:
    using FExtensions = TTypeList<TExtensions...>;
};
```

同时是 `TExtension`（单例可驱动）和 `TTypeList`（一组扩展）。因为还是 `TExtension`，能递归嵌套——父 host 作为元素塞进子 host 列表，子 host 是父出边，整体永无环。

### 驱动机制

**`ForEach<TList>(Scheduler, Visitor, Args...)`** —— 编译期遍历：展开 `TTypeList`，每个类型喂 `TTag<T>` 给 Visitor，Scheduler 控制串/并行。

**`IScheduler` 双 Execute**：

```cpp
// ① 按 stage 分发（硬编码 ExecuteStage）
template <auto Stage, typename TExtensions, typename TTopology = FForwardTopology>
void Execute();

// ② 按 lambda 分发（Visitor 自定义，用 FDefaultSlot 排序）
template <typename TExtensions, typename TVisitor>
void Execute(TVisitor&& Visitor);
```

两者都走"外层 level 串行 + 内层同 level 并行/串行"，用 `Topo::TLevels_t` 分层排序。

### 依赖声明（一层）

```cpp
struct FMyExtension
{
    using FDependsPack = TDependsPack<
        TDependsOn<MyStage::Init, TTypeList<FDep1, FDep2>>
    >;
};
```

每个扩展只声明**直接依赖**（一层）。排序递归展开传递依赖。

### 线程池

`FThreadPool`（`Engine/ThreadPool.h`）：构造 0 线程，首次 `Run` 懒启动到 `min(任务数, hardware_concurrency)`，15 任务 5 核自动 5+5+5 分批。`Run` 带 barrier，任务异常保证 barrier 释放、跑完 rethrow。

## 相关文档

- [Core.h](Core.h) — 聚合头
- [CoreAPI.html](CoreAPI.html) — API 文档
- [../../Private/Core/CoreDoc.md](../../Private/Core/CoreDoc.md) — 实现算法字典
- [../../SourceDoc.md](../../SourceDoc.md) — 源码根
