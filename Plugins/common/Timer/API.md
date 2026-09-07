# Timer — API 文档

Timer 插件 = 分析作用域计时：`FTimer`（服务层）+ `FScopedTimer`（RAII）。毫秒层级树。`TimerApi.h` 提供 `MAHO_TIMER_API` 导出宏。

## Timer.h

### GetTimer <自由函数>

全局计时器访问器（跨 DLL）。

#### 接口

| 签名 | 说明 |
|------|------|
| `MAHO_TIMER_API FTimer* GetTimer()` | 返回全局 `FTimer*` |

### FTimer <class>

栈式作用域分析器——分层计时插桩。`BeginScope` 入栈，`EndScope` 出栈并累加耗时；节点树按名字分叉。**非线程安全**——在拥有线程上使用。

#### 接口

| 签名 | 说明 |
|------|------|
| `void BeginScope(std::string_view Name)` | 推入一个作用域（必须与 EndScope / FScopedTimer 配平） |
| `void EndScope()` | 弹出当前作用域并累加其耗时 |
| `void Reset()` | 清空全部累计计时 |
| `[[nodiscard]] std::string DumpToString() const` | 把计时树格式化成文本（毫秒） |

#### 生命周期阶段（6 个 stage）

| 阶段 | 方法 | 行为 |
|------|------|------|
| `IInit` | `Initialize(FEngineBase&)` | `Reset()` + 发布 `GTimer` |
| `IShutdown` | `Shutdown(FEngineBase&)` | `Reset()` + 清 `GTimer` |
| 其余 4 个 | — | no-op |

#### 内部结构

`FNode`：`Name` / `TotalSeconds` / `MaxSeconds` / `Count` / `Start`（`steady_clock`）/ `Parent` / `Children`（`std::map<std::string, FNode>`，按名字排序分叉）。

### FScopedTimer <class>

RAII 作用域计时器——构造 `BeginScope`，析构 `EndScope`。**不可拷贝**。

```cpp
void Render()
{
    Timer::FScopedTimer Scope("Render");   // 构造即 BeginScope
    // ... work ...
}                                          // 析构即 EndScope

Timer::GetTimer()->DumpToString();   // "Render: 1.23 ms (n calls, avg, max)"
```

#### 接口

| 签名 | 说明 |
|------|------|
| `explicit FScopedTimer(std::string_view Name)` | 构造：`GetTimer()->BeginScope(Name)` |
| `~FScopedTimer()` | 析构：`GetTimer()->EndScope()` |
| 拷贝构造 / 拷贝赋值 `delete` | RAII 不可拷贝 |

- [Timer.md](Timer.md) — 概念 · [实现字典](ImplAPI.md) — 算法
