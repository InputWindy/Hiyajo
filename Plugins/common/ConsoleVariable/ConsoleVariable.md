# ConsoleVariable

控制台变量注册表（UE IConsoleManager 风格）——静态 `TAutoConsoleVariable` 全局变量在 static-init 自注册，`Find` 按名查询；值以字符串存储、类型化访问时解析。

## 提供

- `ECVarFlags`：`None` / `Cheat` / `ReadOnly`（运行时不可改）+ `HasFlag`。
- `ECVarType`：`Int` / `Float` / `Bool` / `String`。
- `IConsoleVariable`：变量接口——`GetName/GetDescription/GetFlags` + `GetInt/GetFloat/GetBool/GetString/Set`。
- `FConsoleVariable`：注册表单例——`Find(Name)` / `Register(...)`。
- `TAutoConsoleVariable<T>`：静态自注册变量模板（`GetValue` / `Set`）。

## 示例

```cpp
static TAutoConsoleVariable<int> CVarMaxFPS("r.MaxFPS", 60, "Max FPS");
const int MaxFPS = CVarMaxFPS.GetValue();
CVarMaxFPS.Set(120);
```

## 依赖

- 三方：无。
- 其他插件：无。
