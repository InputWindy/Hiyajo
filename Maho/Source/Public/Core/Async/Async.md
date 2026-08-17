# Async — 并行模型

引擎的两种并行模型 + 可运行契约。选型原则：**瞬态任务进池，常驻角色用专用线程**。

## 两种模型

| | `FThreadPool` | `FThreadedServer` |
|---|---|---|
| 线程 | N 个常驻 worker | 1 个专用线程 |
| 模型 | 任务池（并行抢） | FIFO 串行队列 |
| barrier | `Run`（原子计数） | `Flush`（哨兵任务） |
| 用途 | 并行 for、短计算、job | 渲染线程、IO、音频 |

## 可运行契约

```cpp
class ICommandLine
{
	virtual ~ICommandLine() = default;
	virtual void ParseCommandLine(int Argc, char** Argv) = 0;
};

class IRunable : public ICommandLine
{
	virtual ~IRunable() = default;
	virtual void MainLoop() = 0;
	virtual void RequestShutdown() = 0;   // 只有可运行的东西才有"停止运行"
};

inline IRunable* GApp = nullptr;          // 运行中的 app（FEngineBase 构造时置位）
```

- **`IRunable`**：凡有主循环的类型都实现它。`MainLoop` 跑循环，`RequestShutdown` 请求退出。
- **`GApp`**：全局运行实例指针，扩展（如 Platform 的窗口关闭）通过 `GApp->RequestShutdown()` 请求退出。
- **语义**：停机请求属于"可运行"的固有属性——`FToolkitBase`（无循环）不是 `IRunable`，天然没有 `RequestShutdown`。

## 用法

### FThreadPool — 瞬态并行任务

```cpp
FThreadPool Pool(4);                    // 0 = 硬件核数
Pool.Run(f1, f2, f3);                   // 并行执行 + barrier 等全部完成
Pool.Submit(task);                      // 提交 fire-and-forget 任务
```

### FThreadedServer — 专用常驻线程

```cpp
class FRenderThread : public FThreadedServer
{
	bool OnInitialize() override { /* 建 RHI 等 */ return true; }
	void OnShutdown() override { /* 清理 */ }
	const char* GetThreadName() const override { return "Render"; }
};

FRenderThread Render;
Render.Initialize();
Render.Submit(task);    // FIFO 入队（非阻塞）
Render.Flush();         // FIFO barrier
Render.Shutdown();      // 排空队列后 join
```

## 关键语义

- **`Flush` 是 FIFO barrier**：哨兵任务排尾，之前提交的任务全部跑完才返回。
- **`Shutdown` 排空队列**：已提交的任务执行完才退出，不丢任务。
- **线程安全**：`Submit` 可从任意线程调用；任务内共享数据需自带同步。
- **`Run` 的优化**：0 个直接返回，1 个内联跑（不起线程）。

## 相关文档

- [Async.h](Async.h) — 聚合头
- [Runable.h](Runable.h) — `ICommandLine` / `IRunable` / `GApp`
- [ThreadPool.h](ThreadPool.h) — `FThreadPool`
- [ThreadedServer.h](ThreadedServer.h) — `FThreadedServer`
- [../Extension.h](../Extension.h) — 串行 / 并行调度器
- [Async.html](Async.html) — API 文档
