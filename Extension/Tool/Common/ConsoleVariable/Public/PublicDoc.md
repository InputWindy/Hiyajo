# Public

## 代码文件

- [ConsoleVariable.h](ConsoleVariable.h)
- [ConsoleVariableApi.h](ConsoleVariableApi.h)

## 接口字典

| 声明 | 说明 |
|------|------|
| `FConsoleVariable : TExtensionList<FConsoleVariable>` | 控制台变量注册表单例（纯单例，无 Main/IAssembly） |
| `EConsoleVariableStage` | 本插件自定义 drive stage（Init / Shutdown） |
| `FConsoleVariable::ExecuteStage(EConsoleVariableStage)` | 阶段分发：仅 Shutdown 清空注册表 |
| `FConsoleVariable::Find(std::string_view)` | 按名查找；不存在返回 `nullptr` |
| `FConsoleVariable::Register(...)` | 注册变量，返回接口（`TAutoConsoleVariable` 使用） |
| `IConsoleVariable` | 变量接口（GetName/GetDescription/GetFlags + 类型化取值 + Set） |
| `ECVarFlags` / `HasFlag` | 变量标志（Cheat/ReadOnly）+ 位测试 |
| `ECVarType` | 变量值类型（Int/Float/Bool/String） |
| `TAutoConsoleVariable<T>` | 静态变量：构造时注册（静态初始化），`GetValue`/`Set` |

## 相关文档

- [../ConsoleVariable.md](../ConsoleVariable.md) — 概念 + 用法
- [../Private/PrivateDoc.md](../Private/PrivateDoc.md) — 实现算法字典
