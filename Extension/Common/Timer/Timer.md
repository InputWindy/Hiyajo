# Timer

Time source extension (chrono)。

## 扩展类

| 字段 | 值 |
|------|-----|
| Class | `Maho::Timer::FTimer` |
| Header | `Timer.h` |
| Stage | `ETimerStage`（本插件自定义） |
| Dependencies | — |

## 说明

计时插件，纯 C++ 标准库（`<chrono>`）实现，无三方依赖。提供三种能力：

- `FTimer` — 栈式作用域性能剖析器，层次化计时（构造/析构自动 Begin/End）。
- `FScopedTimer` — RAII 作用域计时器，构造时 `BeginScope`、析构时 `EndScope`。
- `FGameClock` — 游戏时钟，真实时间/游戏时间 + 时间缩放 + 暂停。惰性推进：`GetGameSeconds()` 累加自上次推进以来的墙钟增量 × TimeScale，无需每帧 Tick。

`FTimer` 是插件单例（`TExtensionList`），`FGameClock` 是独立单例（`TSingleton`）。

### 驱动

`Init`/`Shutdown` 都调用 `Reset()` 清空计时树；无需其他生命周期逻辑。

## 用法

```cpp
#include <Timer.h>
using namespace Maho::Timer;

void Render()
{
    FScopedTimer Scope("Render");
    // ... work ...
}   // 自动 EndScope

FTimer::Get().DumpToString();  // "Render: 1.23 ms (n calls, avg, max)"

FGameClock::Get().SetTimeScale(0.5);
FGameClock::Get().GetGameSeconds();
```

## 相关文档

- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典（cpp 函数表）
- [../../../AGENTS.md](../../../AGENTS.md) — 引擎根 Agent 入口
- [../../../Source/Public/Core/CoreDoc.md](../../../Source/Public/Core/CoreDoc.md) — 核心基础设施

