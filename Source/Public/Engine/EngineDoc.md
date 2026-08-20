<!-- mahogen -->
# Engine

## 代码文件

- [ParallelScheduler.h](ParallelScheduler.h)
- [PluginTemplates.h](PluginTemplates.h)
- [SerialScheduler.h](SerialScheduler.h)
- [ThreadPool.h](ThreadPool.h)
<!-- mahogen end -->

## 概念——插件模板与调度策略

Engine 层放**具体调度策略**和**三类插件模板**。核心 `Core/Scheduler.h` 只给 `IScheduler` 契约，串/并行在这里。

### 三类插件模板

新建插件时，按角色选择继承哪个模板：

| 模板 | 标记 | 身份 | 单例 | 调度器 | 说明 |
|------|------|------|------|--------|------|
| `TTool<TDerived, Ts...>` | `FToolTag` | 工具 | ✅ | ❌ | 被 Driver 调度的神秘妙妙工具 |
| `TLayer<TDerived, Ts...>` | `FLayerTag` | 层 | ✅ | ✅ 并行 | 单例 + Main，调度自己的工具 |
| `TEngine<Ts...>` | — | 应用根 | ❌ | ✅ 并行 | Assembly（导出），可动态安装 |

```cpp
// 工具：单例，不调度别人
class FLog : public Maho::TTool<FLog> { ... };

// 层：单例 + Main + 并行调度 + 工具列表
class FRenderer : public Maho::TLayer<FRenderer, FLog, FRDG> { ... };

// 应用根：Assembly + 并行调度
class FMyGame : public Maho::TEngine<FLog, FRDG, FRenderer> { ... };
```

### 分类与调度

`TLayer` / `TEngine` 内置两个分半别名，Manager 在 `Main` 里分别驱动：

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
