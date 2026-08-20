# Private

## 代码文件

- [ConsoleVariable.cpp](ConsoleVariable.cpp)

## 实现算法字典

| 函数 | 说明 |
|------|------|
| `FConsoleVariable::ExecuteStage(EConsoleVariableStage Stage)` | `Shutdown` → `Registry.clear()`；`Init` 不动（静态 CVar 须存活） |
| `FConsoleVariable::Find(std::string_view Name)` | 加锁 + `Registry.find`；命中返回裸指针，否则 `nullptr` |
| `FConsoleVariable::Register(...)` | 加锁 + 构造 `FCVarEntry` 并插入 `Registry`，返回接口指针 |
| `FCVarEntry`（匿名命名空间） | 具体 `IConsoleVariable`：`GetInt`/`GetFloat` 用 `std::stoi`/`std::stof` + try/catch，`Set` 遇 ReadOnly 不写 |
| `ToLower`（匿名命名空间） | `GetBool` 解析 helper：ASCII 转小写 |

## 相关文档

- [../ConsoleVariable.md](../ConsoleVariable.md) — 概念 + 用法
- [../Public/PublicDoc.md](../Public/PublicDoc.md) — 接口层
