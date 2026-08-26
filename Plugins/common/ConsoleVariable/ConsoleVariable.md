# ConsoleVariable

## 代码文件

- [ConsoleVariable.h](ConsoleVariable.h) — 控制台变量注册表（`ECVarFlags` / `ECVarType` / `IConsoleVariable` / `FConsoleVariable` / `TAutoConsoleVariable`）

## 概念——控制台变量

控制台变量注册表（UE `IConsoleManager` 风格）——静态 `TAutoConsoleVariable` 全局变量在 static-init 自注册，`Find` 按名查询。值以字符串存储、类型化访问时解析（`GetInt/GetFloat/GetBool/GetString`）。`Shutdown` 清空注册表（不破坏 static-init 已注册的全局）。

### FConsoleVariable —— 注册表单例

`TSingleton<FConsoleVariable>` + `IPlugin<IInit, IShutdown>`。`Register(Name, Type, DefaultValue, Description, Flags)` 注册并返回接口（`TAutoConsoleVariable` 用它）；`Find(Name)` 查询，未注册返回 `nullptr`。

### TAutoConsoleVariable\<T\> —— 静态自注册

模板，`T` = int/float/bool/`std::string`（经 `TCVarType<T>` 特化映射到 `ECVarType`）。构造即注册（`FConsoleVariable::Get().Register`），`GetValue()` 类型化读取、`Set(v)` 写回。

```cpp
static TAutoConsoleVariable<int> CVarMaxFPS("r.MaxFPS", 60, "Max FPS");
const int MaxFPS = CVarMaxFPS.GetValue();
CVarMaxFPS.Set(120);
```

### IConsoleVariable —— 变量接口

`GetName / GetDescription / GetFlags` + 类型化读写。`Set` 从字符串解析；`ECVarFlags::ReadOnly` 的变量运行时不可改（`Set` 静默忽略）。

## 三方依赖

- 无（纯 std）。

## 相关文档

- [API.html](API.html) — API 文档
