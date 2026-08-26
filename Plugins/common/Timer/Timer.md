# Timer

## 代码文件

- [Timer.h](Timer.h) — 计时与时钟（`FTimer` / `FScopedTimer` / `FGameClock`）

## 概念——计时分析 + 游戏时钟

Timer 插件提供**两个独立单例**（`FTimer` + `FGameClock`）：`FTimer` 是栈式作用域计时器（分层计时插桩），`FGameClock` 是带时间缩放/暂停的实时/游戏时钟（惰性推进、无需每帧 Tick）。

### FTimer —— 分层作用域计时（单例服务）

`TSingleton<FTimer>` + `IPlugin<IInit, IShutdown>`。内部 `FNode` 树（Root → Children map）：`BeginScope(Name)` 下推（同名节点累加），`EndScope()` 上弹并累计 elapsed（Total/Max/Count），`DumpToString()` 输出毫秒缩进文本树。Initialize/Shutdown/Reset 清树。

```cpp
void Render()
{
    Timer::FScopedTimer Scope("Render");   // BeginScope on construction
    // ... work ...
}                                          // EndScope on destruction
Timer::FTimer::Get().DumpToString();       // "Render: 1.23 ms (n calls, avg, max)"
```

`FScopedTimer` 是 RAII 封装——构造 `BeginScope`、析构 `EndScope`（禁拷贝）。

### FGameClock —— 游戏时钟（单例服务）

`TSingleton<FGameClock>` + `IPlugin<IInit, IShutdown>`。**惰性推进**：`GetGameSeconds()` 在调用时累计「距上次调用的墙钟差 × TimeScale」，无需每帧 Tick；暂停时冻结（`SetPaused(true)` 前先推进一次）。`GetDeltaSeconds()` 返回上一次推进的游戏时间差。

```cpp
const double Real = FGameClock::Get().GetRealSeconds();
const double Game = FGameClock::Get().GetGameSeconds();
FGameClock::Get().SetTimeScale(0.5);   // 慢放
FGameClock::Get().SetPaused(true);     // 冻结
```

## 三方依赖

- 无（纯 std，`std::chrono`）。

## 相关文档

- [API.html](API.html) — API 文档
