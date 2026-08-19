<!-- mahogen -->
# Engine

## 代码文件

- [ParallelScheduler.h](ParallelScheduler.h)
- [SerialScheduler.h](SerialScheduler.h)
- [ThreadPool.h](ThreadPool.h)
<!-- mahogen end -->

## 概念——调度策略

Engine 层放**可选的调度策略**（引擎核心不自带，插件按需引用）。核心 `Core/Scheduler.h` 只给 `IScheduler` 空基契约，具体串/并行在这里。

### FSerialScheduler —— 串行调度

`Run` = fold 顺序执行；`Execute` = 外层 level 串行 + 内层同 level 串行。

### FParallelScheduler —— 并行调度

持有一个 `FThreadPool`。`Run` 任务池并行；`Execute` = 外层 level 串行 + 内层同 level 线程池并行（barrier 同步跨层）。

### FThreadPool —— 线程池

- 构造 0 线程；首次 `Run` 懒启动到 `min(任务数, hardware_concurrency)`
- 15 任务 5 核 → 5+5+5 分批，对外透明
- `Run` 带 barrier（atomic + cv）；任务异常 `try/catch` 记录 `exception_ptr`、保证 barrier 释放、跑完 `rethrow`

### 双 Execute

```cpp
// ① stage 版：硬编码 T::Get().ExecuteStage(Stage)
template <auto Stage, typename TExtensions, typename TTopology = FForwardTopology>
void Execute();

// ② lambda 版：Visitor 自由决定，FDefaultSlot 排序
template <typename TExtensions, typename TVisitor>
void Execute(TVisitor&& Visitor);
```

## 相关文档

- [../Core/CoreDoc.md](../Core/CoreDoc.md) — 核心基础设施
- [EngineAPI.html](EngineAPI.html) — API 文档
