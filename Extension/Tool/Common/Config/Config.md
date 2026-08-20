# Config

Configuration file extension (JSON).

## 扩展类

| 字段 | 值 |
|------|-----|
| Class | `Maho::Config::FConfig` |
| Header | `Config.h` |
| Stage | `EConfigStage`（本插件自定义） |
| Dependencies | — |

## 说明

INI 风格配置解析插件——UE `DefaultEngine.ini` 格式（section + `key=value`）。`FConfig` 是纯单例，内部用 `std::map` 存 section → (key → value)。`Init`/`Shutdown` 都清空所有 section。

### 驱动

宿主用 stage 版 Execute 驱动：

```cpp
// 宿主 Main 里
FParallelScheduler S;
S.Execute<Maho::Config::EConfigStage, FExtensions>();
// → 对每个插件调 T::Get().ExecuteStage(EConfigStage{...})
```

`FConfig::ExecuteStage` 处理两个阶段：

| Stage | 行为 |
|-------|------|
| `EConfigStage::Init` | `Sections.clear()` |
| `EConfigStage::Shutdown` | `Sections.clear()` |

### 用法

```cpp
#include <Config.h>
using namespace Maho::Config;

FConfig::Get().Load("DefaultEngine.ini");
auto Name = FConfig::Get().GetString("/Script/Engine.Engine", "GameName");
auto MaxFps = FConfig::Get().GetInt("/Script/Engine.Engine", "MaxFps", 60);
```

## 三方依赖

无。

## 相关文档

- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典
- [../../AGENTS.md](../../AGENTS.md) — 引擎根 Agent 入口
- [../../Source/Public/Core/CoreDoc.md](../../Source/Public/Core/CoreDoc.md) — 核心基础设施

