# Timer（Private）— 实现算法字典

cpp 侧每个函数的算法伪代码解释。Public 侧 API 文档通过 `#fn-...` 锚点跳转落到这里。

## Timer.cpp

<a id="fn-timer-get"></a>
### GetTimer()

← [公开 API](API.md) · `FTimer*`

```text
GetTimer():
1. return GTimer
```

<a id="fn-timer-init"></a>
### FTimer::Initialize(FEngineBase&) / Shutdown(FEngineBase&)

← [公开 API](API.md) · `void`

```text
Initialize(Engine):
1. Reset(); GTimer = this

Shutdown(Engine):
1. Reset(); GTimer = nullptr
```

<a id="fn-timer-begin"></a>
### FTimer::BeginScope(std::string_view Name)

← [公开 API](API.md) · `void`

在 `Current` 下查找/创建子节点，记录起点并下移。

```text
BeginScope(Name):
1. Child = Current->Children[Name]
2. Child.Name = Name
3. Child.Start = steady_clock::now()
4. Current = &Child
```

<a id="fn-timer-end"></a>
### FTimer::EndScope()

← [公开 API](API.md) · `void`

弹出当前作用域并累加耗时（Total/Max/Count）。在 Root 上调用（未配平）静默忽略。

```text
EndScope():
1. if Current == &Root: return      // 未配平 - 忽略
2. Elapsed = now() - Current->Start
3. Current->TotalSeconds += Elapsed
4. Current->MaxSeconds = max(Current->MaxSeconds, Elapsed)
5. Current->Count += 1
6. Current = Current->Parent
```

<a id="fn-timer-reset"></a>
### FTimer::Reset()

← [公开 API](API.md) · `void`

```text
Reset():
1. Root = FNode{"Root"}; Current = &Root
```

<a id="fn-timer-dump"></a>
### FTimer::DumpToString() const

← [公开 API](API.md) · `std::string`

递归格式化节点树为毫秒文本：`<Name>: <total> ms (n calls, avg <avg> ms, max <max> ms)`，每层缩进 2 空格。

```text
DumpToString():
1. Out = ostringstream()
2. Format(Node, Depth):   // 递归
       Out << 2*Depth 空格 << Node.Name << ": "
       if Node.Count > 0:
           Out << Node.TotalSeconds*1000 << " ms ("
               << Node.Count << " calls, avg "
               << Node.TotalSeconds/Node.Count*1000 << " ms, max "
               << Node.MaxSeconds*1000 << " ms)"
       Out << '\n'
       for Child in Node.Children: Format(Child, Depth+1)
3. Format(Root, 0)
4. return Out.str()
```

<a id="fn-timer-scoped"></a>
### FScopedTimer(std::string_view Name) / ~FScopedTimer()

← [公开 API](API.md) · RAII

```text
FScopedTimer(Name):
1. GetTimer()->BeginScope(Name)

~FScopedTimer():
1. GetTimer()->EndScope()
```

<a id="fn-timer-export"></a>
### CreateLayer()（C 导出）

```text
extern "C" CreateLayer():
1. return Maho::Timer::FTimer::CreateLayer()
```

- [Timer.md](Timer.md) — 概念 · [公开 API](API.md) — 签名入口
