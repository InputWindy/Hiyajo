<!-- mahogen -->
# Engine

## 代码文件

- [ParallelScheduler.h](ParallelScheduler.h)
- [Layer.h](Layer.h)
- [Tool.h](Tool.h)
- [SerialScheduler.h](SerialScheduler.h)
- [ThreadPool.h](ThreadPool.h)
<!-- mahogen end -->

## 概念——插件模板与调度策略

Engine 层放**具体调度策略**和**两类插件模板**。核心 `Core/Scheduler.h` 只给 `IScheduler` 契约，串/并行在这里。

两个模板各占一个头文件：`Tool.h`（`TTool`，C++14）、`Layer.h`（`TLayer`，C++20）。工具模板不拉任何 concept 头，标准要求最低，方便像 Math（GLM）这类要降标的插件单独使用。

### 两类插件模板

新建插件时，按角色选择继承哪个模板：

| 模板 | 标记 | 身份 | 单例 | 调度器 | 说明 |
|------|------|------|------|--------|------|
| `TTool<TDerived>` | `FToolTag` | 工具 | ✅ | ❌ | 即插即用，全 public，谁用谁 `Get().xxx()` |
| `TLayer<Ts...>` | `FLayerTag` | 应用根/嵌套宿主 | ❌ | ✅ 并行 | Assembly（导出 CreateExtension），可动态安装，并行调度自己 FExtensions |

Engine 与 Layer 已统一为 `TLayer`：应用根就是一个 Layer（导出 CreateExtension → 可多实例），它内部再驱动自己的工具/子层。不再有单独的 Engine 分类。

```cpp
// 工具：单例，全 public，不调度别人
class FLog : public Maho::TTool<FLog> { ... };

// 嵌套层：宿主，调度自己的工具/子层
class FRenderer : public Maho::TLayer<FLog, FRDG> { ... };

// 应用根：也是一个 Layer（Assembly，导出）
class FMyGame : public Maho::TLayer<FLog, FRDG, FRenderer> { ... };
```

### 分类与调度

`TLayer` 内置两个分半别名，Manager 在 `Main` 里分别驱动：

```cpp
using FTools = typename TFilter<FExtensions, FToolTag>::Type;   // 工具
using FLayers = typename TFilter<FExtensions, FLayerTag>::Type;  // 层

enum class EStage { Init, Tick, Shutdown };

int Main(int Argc, char** Argv) override
{
    Execute<EStage::Init,     FTools>();   // 工具先初始化
    Execute<EStage::Init,     FLayers>();  // 层后初始化
    Execute<EStage::Tick,     FLayers>();  // 只有层有帧流程
    Execute<EStage::Shutdown, FLayers>();  // 层先停
    Execute<EStage::Shutdown, FTools>();   // 工具最后关
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
