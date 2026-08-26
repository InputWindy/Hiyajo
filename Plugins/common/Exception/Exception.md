# Exception

非致命异常广播单例——`ReportException` 把消息分发到所有 `OnException` 订阅者（日志、遥测、崩溃上报）。致命错误走 Core/Fatal，本插件绝不 abort。

## 提供

- `TMulticastEvent<void(Args...)>`：最小线程安全多播事件——`Bind` / `Broadcast` / `RemoveAll`。
- `FException`：`TSingleton<FException>` + `IPlugin<IInit, IShutdown>`。
  - `ReportException(std::string_view)` / `ReportException(const std::exception&)`（转发 `what()`）。
  - `OnException`：订阅事件（`void(const std::string&)`）。

## 示例

```cpp
Exception::FException::Get().OnException.Bind([](const std::string& M) {
    Log::Error("exception: {}", M);
});
Exception::FException::Get().ReportException("failed to load texture");
```

## 依赖

- 三方：无。
- 其他插件：无。
