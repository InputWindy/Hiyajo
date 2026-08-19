# Exception

Non-fatal exception handling extension。

## 扩展类

| 字段 | 值 |
|------|-----|
| Class | `Maho::Exception::FException` |
| Header | `Exception.h` |
| Stage | `EExceptionStage`（本插件自定义） |
| Dependencies | — |

## 说明

非致命异常处理插件。`ReportException` 把消息广播给每个订阅者（日志、遥测、崩溃上报都 hook `OnException`）——这里**不 abort**，致命错误走 `Core/Fatal`。`OnException` 是 void 组播委托，`Init` / `Shutdown` 阶段清空订阅。

## 驱动

宿主用 stage 版 Execute 驱动：

```cpp
// 宿主 Main 里
FParallelScheduler S;
S.Execute<Maho::Exception::EExceptionStage, FExtensions>();
// → 对插件调 T::Get().ExecuteStage(EExceptionStage{...})
```

`ExecuteStage` 处理两个阶段：

| Stage | 行为 |
|-------|------|
| `EExceptionStage::Init` | 清空 `OnException` 订阅 |
| `EExceptionStage::Shutdown` | 清空 `OnException` 订阅 |

## 用法

```cpp
#include <Exception.h>
using namespace Maho::Exception;

FException::Get().OnException.Add([](const std::string& M) {
	Maho::Log::Error("exception: {}", M);
});
FException::Get().ReportException("failed to load texture");
```

## 三方依赖

无。

## 相关文档

- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典（cpp 函数表）
- [../../../AGENTS.md](../../../AGENTS.md) — 引擎根 Agent 入口
- [../../../Source/Public/Core/CoreDoc.md](../../../Source/Public/Core/CoreDoc.md) — 核心基础设施
