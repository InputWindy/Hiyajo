# ConsoleVariable — API 文档

ConsoleVariable 插件 = CVar 注册表（namespace `Maho::ConsoleVariable`）。**单例形态**：`FConsoleVariable : TSingleton<FConsoleVariable>, IPlugin<IInit, IShutdown>`。静态 `TAutoConsoleVariable` 全局对象在 static-init 自注册，`Find` 按名查询。值以字符串存储、typed 访问时解析（UE `IConsoleManager` 风格）。

## ECVarFlags <enum class>

CVar 标志（UE `ECVF_*` 风格）。

| 值 | 说明 |
|------|------|
| `None = 0` | 无标志 |
| `Cheat = 1u << 0` | 仅作弊可用 |
| `ReadOnly = 1u << 1` | 运行时不可改 |

## HasFlag <自由函数>

标志位与测试。

#### 接口

| 签名 | 说明 |
|------|------|
| `[[nodiscard]] constexpr bool HasFlag(ECVarFlags Flags, ECVarFlags Test)` | `(uint32(Flags) & uint32(Test)) != 0` |

## ECVarType <enum class>

CVar 值类型。

| 值 | 说明 |
|------|------|
| `Int` | 整型 |
| `Float` | 浮点 |
| `Bool` | 布尔 |
| `String` | 字符串 |

## IConsoleVariable <class>

CVar 接口——`Find` / `Register` 返回的类型。值内部以字符串存储，typed 访问时解析。

#### 接口

| 签名 | 说明 |
|------|------|
| `[[nodiscard]] virtual std::string_view GetName() const = 0` | 变量名 |
| `[[nodiscard]] virtual std::string_view GetDescription() const = 0` | 描述 |
| `[[nodiscard]] virtual ECVarFlags GetFlags() const = 0` | 标志 |
| `[[nodiscard]] virtual int GetInt() const = 0` | 以整型读（字符串解析） |
| `[[nodiscard]] virtual float GetFloat() const = 0` | 以浮点读 |
| `[[nodiscard]] virtual bool GetBool() const = 0` | 以布尔读（`"true"`/`"1"`/`"yes"`/`"on"` → true，大小写不敏感） |
| `[[nodiscard]] virtual std::string GetString() const = 0` | 以字符串读（原始值） |
| `virtual void Set(std::string_view Value) = 0` | 从字符串设置（解析）；`ReadOnly` 时静默忽略 |

## FConsoleVariable <class>

CVar 注册表（UE `IConsoleManager`）。`TSingleton<FConsoleVariable>` + `IPlugin<IInit, IShutdown>`——进程唯一实例入口 `Get()` 定义在 `ConsoleVariable.cpp`（编进插件 DLL，跨 DLL 进程唯一），生命周期经 Engine 层 Init/Shutdown。`Shutdown` 清空注册表。

#### 接口

| 签名 | 说明 |
|------|------|
| `static FConsoleVariable& Get()` | 进程唯一实例入口（声明在头、定义在 ConsoleVariable.cpp） |
| `void Initialize(FEngineBase& Engine) override` | 生命周期 Init（no-op：static-init 全局已注册，显式不清空） |
| `void Shutdown(FEngineBase& Engine) override` | 生命周期 Shutdown：清空注册表 |
| `[[nodiscard]] IConsoleVariable* Find(std::string_view Name)` | 按名查找；未注册 → `nullptr` |
| `IConsoleVariable* Register(std::string_view Name, ECVarType Type, std::string DefaultValue, std::string_view Description, ECVarFlags Flags)` | 注册变量（`TAutoConsoleVariable` 构造调用）；返回接口 |

#### 约束

| 签名 | 说明 |
|------|------|
| `FConsoleVariable() protected` + `friend TSingleton<FConsoleVariable>` | 构造仅 TSingleton 可访问 |
| `Registry` protected `std::map<std::string, std::unique_ptr<IConsoleVariable>>` | 注册表存储 |

## TCVarType<T> <struct（类型特征）>

`T` → `ECVarType` 的编译期映射。特化：`int → Int`、`float → Float`、`bool → Bool`、`std::string → String`。

| 特化 | `Value` |
|------|------|
| `TCVarType<int>` | `ECVarType::Int` |
| `TCVarType<float>` | `ECVarType::Float` |
| `TCVarType<bool>` | `ECVarType::Bool` |
| `TCVarType<std::string>` | `ECVarType::String` |

## TAutoConsoleVariable<T> <class（模板）>

静态 CVar——构造即自注册（static-init），UE 的 `TAutoConsoleVariable` 风格。`T` = int/float/bool/`std::string`（经 `TCVarType<T>` 映射）。

```cpp
static TAutoConsoleVariable<int> CVarMaxFPS("r.MaxFPS", 60, "Max FPS");

const int MaxFPS = CVarMaxFPS.GetValue();
CVarMaxFPS.Set(120);
```

#### 接口

| 签名 | 说明 |
|------|------|
| `TAutoConsoleVariable(std::string_view InName, T Default, std::string_view Description, ECVarFlags Flags = ECVarFlags::None)` | 构造即 `FConsoleVariable::Get().Register(...)`；`Default` 经 `ToString` 转字符串 |
| `[[nodiscard]] T GetValue() const` | typed 读取（经 `Handle`，按 `T` 分派到对应 Getter） |
| `void Set(T Value)` | typed 写回（`ToString` 后经 `Handle->Set`） |
| `[[nodiscard]] std::string_view GetName() const` | 变量名 |

- [ConsoleVariable.md](ConsoleVariable.md) — 概念 · [实现字典](ImplAPI.md) — 算法
