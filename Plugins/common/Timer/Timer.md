# Timer

计时分析 + 游戏时钟——两个单例：`FTimer`（分层作用域计时）+ `FGameClock`（带时间缩放/暂停的实时/游戏时钟，惰性推进、无需每帧 Tick）。

## 提供

- `FTimer`：`BeginScope(Name)` / `EndScope()` / `Reset()` / `DumpToString()`（毫秒文本树）。
- `FScopedTimer`：RAII 作用域计时（构造 BeginScope、析构 EndScope）。
- `FGameClock`：`GetRealSeconds()` / `GetGameSeconds()`（缩放 × 暂停冻结）/ `GetDeltaSeconds()` / `SetTimeScale` / `SetPaused` / `GetTimeScale` / `IsPaused`。

## 示例

```cpp
Timer::FScopedTimer Scope("Render");   // 构造即 BeginScope
// ... work ...                        // 析构即 EndScope
Timer::FTimer::Get().DumpToString();   // "Render: 1.23 ms (n calls, avg, max)"
const double S = FGameClock::Get().GetGameSeconds();
```

## 依赖

- 三方：无。
- 其他插件：无。
