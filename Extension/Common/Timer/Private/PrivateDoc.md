# Private

## 代码文件

- [Timer.cpp](Timer.cpp)

## 实现算法字典

Timer 插件的实现集中在 `Timer.cpp`——纯 `<chrono>` 封装。

| 函数 | 说明 |
|------|------|
| `FTimer::ExecuteStage(ETimerStage Stage)` | 阶段分发：`Init` / `Shutdown` 都调用 `Reset()` |
| `FTimer::BeginScope(string_view)` | 在 `Current->Children` 建/取子节点，记录 `Start`，`Current` 下移 |
| `FTimer::EndScope()` | 若已是根则忽略（不平衡）；算耗时，累加 Total/Max/Count，`Current` 回到父节点 |
| `FTimer::Reset()` | `Root` 重置为默认，`Current` 指回根 |
| `FTimer::DumpToString()` | 递归遍历计时树，输出缩进文本（毫秒，含调用次数/均值/最大值） |
| `FScopedTimer::FScopedTimer(string_view)` | 构造 → `FTimer::Get().BeginScope(Name)` |
| `FScopedTimer::~FScopedTimer()` | 析构 → `FTimer::Get().EndScope()` |
| `FGameClock::GetRealSeconds()` | 墙钟自构造起经过的秒数 |
| `FGameClock::GetGameSeconds()` | 非暂停时累加真实增量 × TimeScale 到 GameSeconds，更新 DeltaSeconds；返回 GameSeconds |
| `FGameClock::GetDeltaSeconds()` | 先 `GetGameSeconds()` 再返回 DeltaSeconds |
| `FGameClock::SetTimeScale(double)` | 设置时间缩放 |
| `FGameClock::SetPaused(bool)` | 若转暂停且之前非暂停，先推进一次再冻结 |
| `FGameClock::GetTimeScale()` | 返回当前缩放 |
| `FGameClock::IsPaused()` | 返回暂停状态 |

**纯标准库实现**——无任何三方依赖，`<chrono>` 的 `steady_clock` 提供单调时钟。

## 相关文档

- [../Timer.md](../Timer.md) — 概念 + 用法
- [../Public/PublicDoc.md](../Public/PublicDoc.md) — 接口层

