# Log

Logging extension (spdlog)。

## 扩展类

| 字段 | 值 |
|------|-----|
| Class | `Maho::Log::FLog` |
| Header | `Log.h` |
| Stage | `ELogStage`（本插件自定义） |
| Dependencies | — |

## 说明

日志插件，封装 spdlog（header-only）。**Public 头不泄露三方依赖**——`Log.h` 只暴露 `FLog` 单例 + `ELogStage`/`ELogLevel` 枚举 + 简单字符串日志函数，spdlog 只在 `Private/Log.cpp` 里。

### 驱动

宿主用 stage 版 Execute 驱动：

```cpp
// 宿主 Main 里
FParallelScheduler S;
S.Execute<Maho::Log::ELogStage, FExtensions>();
// → 对每个插件调 T::Get().ExecuteStage(ELogStage{...})
```

Log 的 `ExecuteStage` 处理两个阶段：

| Stage | 行为 |
|-------|------|
| `ELogStage::Init` | `spdlog::set_level(info)` |
| `ELogStage::Shutdown` | flush + `spdlog::shutdown()` |

### 用法

```cpp
#include <Log.h>
using namespace Maho::Log;

Info("游戏启动");
Warn("配置缺失");
SetLogLevel(ELogLevel::Debug);
```

## 三方依赖

- spdlog（`Log.cmake` FetchContent 拉取，header-only）

## 相关文档

- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典（cpp 函数表）
- [../../../AGENTS.md](../../../AGENTS.md) — 引擎根 Agent 入口
- [../../../Source/Public/Core/CoreDoc.md](../../../Source/Public/Core/CoreDoc.md) — 核心基础设施
