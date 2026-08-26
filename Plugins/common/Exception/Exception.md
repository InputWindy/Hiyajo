# Exception

## 代码文件

- [Exception.h](Exception.h) — 非致命异常广播（`TMulticastEvent` + `FException`）

## 概念——异常广播

非致命异常广播单例服务——`ReportException` 把消息分发到所有 `OnException` 订阅者（日志、遥测、崩溃上报）。**本插件绝不 abort**：致命错误走 `Core/Fatal`，这里只做"报告给感兴趣的人"。引擎暂无独立委托积木，因此自带一个最小多播事件（`TMulticastEvent`，仅需 `void(const std::string&)` 广播）。

### FException —— 报告入口（单例服务）

`TSingleton<FException>` + `IPlugin<IInit, IShutdown>`（Initialize/Shutdown 清空订阅）。`ReportException(std::string_view)` 直接广播；`ReportException(const std::exception&)` 转发 `what()`。

### TMulticastEvent —— 最小多播事件

模板 `TMulticastEvent<void(Args...)>`：`Bind(FHandler)` 追加订阅（`std::function`）、`Broadcast(Args...)` 顺序调用非空 handler、`RemoveAll()` 清空。无锁——单线程广播语义。

```cpp
Exception::FException::Get().OnException.Bind([](const std::string& M) {
    Log::Error("exception: {}", M);
});
Exception::FException::Get().ReportException("failed to load texture");
```

## 三方依赖

- 无（纯 std）。

## 相关文档

- [API.html](API.html) — API 文档
