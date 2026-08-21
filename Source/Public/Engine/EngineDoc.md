<!-- mahogen -->
# Engine

## 代码文件

- [Schedulers.h](Schedulers.h) — 串行 / 并行调度器
- [ThreadPool.h](ThreadPool.h)
<!-- mahogen end -->

## 概念——插件模板与调度策略

Engine 层放**具体调度策略**和**两类插件模板**。核心 `Core/Scheduler.h` 只给 `IScheduler` 契约，串/并行在这里。

两个模板各占一个头文件：`Tool.h`（`TTool`，C++14）、`Layer.h`（`TLayer`，C++20）。工具模板不拉任何 concept 头，标准要求最低，方便像 Math（GLM）这类要降标的插件单独使用。

### 两类插件模板

新建插件时，按角色选择继承哪个模板：

| 模板 | 身份 | 单例 | 调度器 | 说明 |
|------|------|------|--------|------|
| `TTool<TDerived>` | 工具 | ✅ | ❌ | 即插即用，全 public，谁用谁 `Get().xxx()` |
| `TLayer<Ts...>` | 应用根/嵌套宿主 | ❌ | ✅ 并行 | Assembly（导出 CreateExtension），可动态安装，并行调度自己 FExtensions |

Engine 与 Layer 已统一为 `TLayer`：应用根就是一个 Layer（导出 CreateExtension → 可多实例），它内部再驱动自己的工具/子层。不再有单独的 Engine 分类。

```cpp
// 工具：单例，全 public，不调度别人
class FLog : public Maho::TTool<FLog> { ... };

// 嵌套层：宿主，调度自己的工具/子层
class FRenderer : public Maho::TLayer<FLog, FRDG> { ... };

// 应用根：也是一个 Layer（Assembly，导出）
class FMyGame : public Maho::TLayer<FLog, FRDG, FRenderer> { ... };
```

### 分类与驱动

`TLayer` 内置两个分半：`FTools`（编译期单例类型）与 `FLayerTypes`（装配类型，运行时实例化进 `this->Layers`）。`Execute` 是并行遍历基座，宿主按生命周期分阶段调用：

```cpp
using FTools = typename TFilterWhere<FExtensions, TIsSingleton>::Type;   // 工具（单例）
using FLayerTypes = typename TFilter<FExtensions, ILayer>::Type;       // 层（Assembly）

int Main(int Argc, char** Argv) override
{
    CreateLayers();   // 实例化每个子 Layer（CreateExtension）进 this->Layers

    Execute<FTools>([](T& Tool) {   // 工具：编译期单例，遍历器传实例
        Tool.Initialize();
    });
    Execute(Layers, [](Maho::ILayer* L) { ... });   // 层：运行时实例
    Execute<FTools>([](T& Tool) { Tool.Shutdown(); });
    return 0;
}
```

### 调度策略

**`FSerialScheduler`** —— 串行：`Run` = fold 顺序执行；`Execute` = 外层 level 串行 + 内层串行。

**`FParallelScheduler`** —— 并行：持有 `FThreadPool`。`Run` 投线程池；`Execute` = 外层 level 串行 + 内层线程池并行（barrier 跨层同步）。

**`FThreadPool`** —— 线程池：

- 构造 0 线程；首次 `Run` 懒启动到 `min(任务数, hardware_concurrency)`
- 15 任务 5 核 → 5+5+5 分批，对外透明
- `Run` 带 barrier（atomic + cv）；任务异常 `try/catch` 记录 `exception_ptr`、保证 barrier 释放、跑完 `rethrow`

## 相关文档

- [../Core/CoreDoc.md](../Core/CoreDoc.md) — 核心基础设施
- [EngineAPI.html](EngineAPI.html) — API 文档
