# ConsoleVariable（Private）— 实现算法字典

注册表单例实现。公开 API 签名入口见 API.md。

## ConsoleVariable.cpp

<a id="fn-cvar-get"></a>
### FConsoleVariable::Get()

← [公开 API](API.md) · `FConsoleVariable&`

进程唯一实例：函数内 static 局部变量（编进 ConsoleVariable DLL → 跨 DLL 进程唯一）。

```text
Get():
1. static FConsoleVariable Instance
2. return Instance
```

<a id="fn-cvar-entry"></a>
### FCVarEntry（匿名命名空间，内部）

`IConsoleVariable` 的字符串存储实现：全部值存 `std::string Value`，typed 访问时解析。`Set` 遇 `ReadOnly` 直接返回（静默忽略）。`GetBool` 经 `ToLower` 后与 `"true"` / `"1"` / `"yes"` / `"on"` 比较。`GetInt` / `GetFloat` 用 `std::stoi` / `std::stof`，解析失败返回 0 / 0.0f（catch-all，不抛异常）。

```text
FCVarEntry::Set(InValue):
1. if HasFlag(Flags, ReadOnly): return
2. Value = string(InValue)

FCVarEntry::GetBool():
1. Lower = ToLower(Value)
2. return Lower == "true" || Lower == "1" || Lower == "yes" || Lower == "on"
```

<a id="fn-cvar-initialize"></a>
### FConsoleVariable::Initialize(FEngineBase& Engine)

← [公开 API](API.md) · `void`

no-op——static-init 已把全局 `TAutoConsoleVariable` 注册进表；显式不清空（清空会破坏它们）。

```text
Initialize(Engine):
1. (void)Engine          // 无操作
```

<a id="fn-cvar-shutdown"></a>
### FConsoleVariable::Shutdown(FEngineBase&)

← [公开 API](API.md) · `void`

清空注册表。

```text
Shutdown():
1. lock(GMutex)
2. Registry.clear()
```

<a id="fn-cvar-find"></a>
### FConsoleVariable::Find(string_view Name)

← [公开 API](API.md) · `IConsoleVariable*`

按名查表。`Name` 转 `std::string` 做 key；未命中 → `nullptr`。

```text
Find(Name):
1. lock(GMutex)
2. It = Registry.find(string(Name))
3. return It != end ? It->second.get() : nullptr
```

<a id="fn-cvar-register"></a>
### FConsoleVariable::Register(Name, Type, DefaultValue, Description, Flags)

← [公开 API](API.md) · `IConsoleVariable*`

新建 `FCVarEntry`，以名字为 key 入表（同名覆盖），返回裸指针。

```text
Register(Name, Type, DefaultValue, Description, Flags):
1. lock(GMutex)
2. Entry = make_unique<FCVarEntry>(Name, Type, DefaultValue, Description, Flags)
3. Result = Entry.get()
4. Registry[string(Name)] = move(Entry)
5. return Result
```

- [ConsoleVariable.md](ConsoleVariable.md) — 概念 · [公开 API](API.md) — 签名入口
