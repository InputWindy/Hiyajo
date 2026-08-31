# Exception

## Code files

- [Exception.h](Public/Exception.h) — 异常中心头：`FException` + `OnException` 多播事件
- [ExceptionApi.h](Public/ExceptionApi.h) — DLL 导出宏 `MAHO_EXCEPTION_API`
- [Exception.cpp](Private/Exception.cpp) — `ReportException` 广播 + 跨 DLL 访问器 `GetExceptionCenter`

## Concept - Non-Fatal Exception Broadcast

Exception 是一个**广播中枢**：引擎各处把"出错了但不必死"的事件上报给它，订阅者（日志、遥测、崩溃上报）各自决定怎么处理。上报本身只做一件事——把消息分发给所有订阅者，**不终止、不记录、不写盘**。致命错误不属于这里，走 `Core/Fatal` 的 `ReportFatal`。

### 1. 订阅（OnException）

`OnException` 是 `TMulticastEvent<void(const std::string&)>`（`Core/Delegate`），在拥有线程上用 `Bind` 注册处理器；**非线程安全**——跨线程上报请走队列。

```cpp
#include <Exception.h>

using namespace Maho;

Exception::GetExceptionCenter()->OnException.Bind([](const std::string& M) {
    if (auto* L = ::Maho::GetLog())
    {
        L->Error("exception: {}", M);   // 或转发给遥测 / 上报
    }
});
```

### 2. 上报（ReportException）

- `ReportException("...")` 直接广播字符串消息。
- `ReportException(const std::exception&)` 转发 `what()`。

```cpp
Exception::GetExceptionCenter()->ReportException("failed to load texture");
```

生命周期：`FException::Initialize` 清空订阅并发布 `this`；`Shutdown` 撤回 `this` 并清空订阅。

## Third-party dependencies

- None (pure std).

## Related docs

- [API.md](API.md) - API documentation
- [ImplAPI.md](ImplAPI.md) - 实现算法字典
- [EngineDoc.md](../../../Source/Public/Engine/EngineDoc.md) - 层架构
