# ConsoleVariable

## Code files

- [ConsoleVariable.h](Public/ConsoleVariable.h) — CVar 注册表（`ECVarFlags` / `ECVarType` / `IConsoleVariable` / `FConsoleVariable` / `TCVarType` / `TAutoConsoleVariable`）
- [ConsoleVariable.cpp](Private/ConsoleVariable.cpp) — 单例 `Get` + 注册/查询/清空实现

## Concept - 控制台变量

CVar 注册表（UE `IConsoleManager` 风格）：静态 `TAutoConsoleVariable` 全局对象在 static-init 自注册，`Find` 按名查询。值以字符串存储、typed 访问时解析。**单例形态**：`FConsoleVariable : TSingleton<FConsoleVariable>, IPlugin<IInit, IShutdown>`——`Get()` 进程唯一，生命周期经 Engine 层；`Shutdown` 清空注册表。

### FConsoleVariable — 注册表单例

`TSingleton<FConsoleVariable>` + `IPlugin<IInit, IShutdown>`。`Register(Name, Type, DefaultValue, Description, Flags)` 注册并返回接口（`TAutoConsoleVariable` 构造调用）；`Find(Name)` 查询，未注册返回 `nullptr`。`Initialize` 是 no-op——static-init 全局已注册，显式不清空；`Shutdown` 清空注册表。

### TAutoConsoleVariable\<T\> — 静态自注册

模板；`T` = int/float/bool/`std::string`（经 `TCVarType<T>` 特化映射到 `ECVarType`）。构造即注册；`GetValue()` typed 读，`Set(v)` typed 写。

### IConsoleVariable — 变量接口

`GetName / GetDescription / GetFlags` + typed 读写。`Set` 从字符串解析；`ECVarFlags::ReadOnly` 变量运行时不可改（`Set` 静默忽略）。

```cpp
static TAutoConsoleVariable<int> CVarMaxFPS("r.MaxFPS", 60, "Max FPS");
const int MaxFPS = CVarMaxFPS.GetValue();
CVarMaxFPS.Set(120);

// query from the registry
if (auto* CVar = Maho::ConsoleVariable::FConsoleVariable::Get().Find("r.MaxFPS")) {
    const int V = CVar->GetInt();
}
```

## Third-party dependencies

- None（pure std）。

## Related docs

- [API.md](API.md) - API documentation
