# Timer

## Code files

- [Timer.h](Public/Timer.h) — 分层作用域计时器（`FTimer` + `FScopedTimer`）
- [TimerApi.h](Public/TimerApi.h) — `MAHO_TIMER_API` 导出宏
- [Timer.cpp](Private/Timer.cpp) — 节点树实现 + `CreateLayer` 导出

## Concept - Hierarchical Scope Profiler

Timer 是栈式作用域计时器：`FScopedTimer` 是 RAII 作用域计时，包住 `FTimer` 的分层作用域分析器。计时以**毫秒层级树**输出（根 "Root" → 子作用域，按名字排序分叉）。

### 1. 分层节点树

`FTimer` 维护一个当前栈指针（`Current`）。`BeginScope` 在 `Current` 下查找/创建子节点并下移；`EndScope` 累加该节点耗时（Total/Max/Count）并回退到父节点。同名字的子节点聚合成一个节点（跨调用累计）。

```cpp
void Render()
{
    Timer::FScopedTimer Scope("Render");   // BeginScope("Render")
    // ... work ...
}                                          // EndScope()

Timer::GetTimer()->DumpToString();
// Root:
//   Render: 1.23 ms (42 calls, avg 0.029 ms, max 0.41 ms)
//     Pass1: ...
```

### 2. RAII 配平

`FScopedTimer` 构造调 `BeginScope`、析构调 `EndScope`——作用域退出即配平，异常安全。手动 `BeginScope` / `EndScope` 必须成对；未配平（`Current == &Root` 时再 EndScope）静默忽略。

### 3. 生命周期

引擎层 6 阶段：`Initialize` 重置并发布 `GTimer`，`Shutdown` 重置并清空。

## Third-party dependencies

- 无第三方库（纯 std）。
- 其他插件：无——`.cplugin` Dependencies = `[]`

## Related docs

- [API.md](API.md) - API documentation
