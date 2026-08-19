# ConsoleVariable

Console variable registry extension。

## 扩展类

| 字段 | 值 |
|------|-----|
| Class | `Maho::ConsoleVariable::FConsoleVariable` |
| Header | `ConsoleVariable.h` |
| Stage | `EConsoleVariableStage`（本插件自定义） |
| Dependencies | — |

## 说明

控制台变量注册表（UE `IConsoleManager` 风格）。`TAutoConsoleVariable` 静态全局变量在静态初始化阶段注册到这里，`Find` 按名查找。值内部存字符串，类型化访问时解析。

`Registry` 成员从旧引擎的 `protected` 改为 `private`，把内存细节隐藏到 cpp 里。

## 驱动

宿主用 stage 版 Execute 驱动：

```cpp
// 宿主 Main 里
FParallelScheduler S;
S.Execute<Maho::ConsoleVariable::EConsoleVariableStage, FExtensions>();
// → 对插件调 T::Get().ExecuteStage(EConsoleVariableStage{...})
```

`ExecuteStage` 处理两个阶段：

| Stage | 行为 |
|-------|------|
| `EConsoleVariableStage::Init` | 无操作（静态 CVar 全局量在静态初始化注册，须存活到 Init 后） |
| `EConsoleVariableStage::Shutdown` | 清空注册表 |

## 用法

```cpp
#include <ConsoleVariable.h>
using namespace Maho::ConsoleVariable;

static TAutoConsoleVariable<int> CVarMaxFPS("r.MaxFPS", 60, "Max FPS");

const int MaxFPS = CVarMaxFPS.GetValue();
CVarMaxFPS.Set(120);
```

## 三方依赖

无。

## 相关文档

- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典（cpp 函数表）
- [../../../AGENTS.md](../../../AGENTS.md) — 引擎根 Agent 入口
- [../../../Source/Public/Core/CoreDoc.md](../../../Source/Public/Core/CoreDoc.md) — 核心基础设施
