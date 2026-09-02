# Exception — API 文档

服务层：`FException` 是 `FLayer<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>`（`Exception.dll`）。非致命异常中心——`ReportException` 把消息广播给所有 `OnException` 订阅者（日志、遥测、崩溃上报）；**没有任何东西在这里 abort**，致命错误走 `Core/Fatal` 的 `ReportFatal`。

## Exception.h

### FException <class>

非致命异常处理层（mount Init + Shutdown）。核心是一个公开的 `TMulticastEvent` 成员 `OnException`（来自 `Core/Delegate`）——订阅者收到每次上报的异常消息。

#### 接口

| 签名 | 说明 |
|------|------|
| `MAHO_DECLARE_LAYER(FException, "Exception.dll")` | 层声明宏（DLL 导出入口） |
| `void ReportException(std::string_view Message)` | 上报一条非致命异常消息（广播给所有 `OnException` 订阅者） |
| `void ReportException(const std::exception& Error)` | 从 `std::exception` 上报（转发 `what()`） |
| `TMulticastEvent<void(const std::string&)> OnException` | 多播事件——订阅者注册处，收到每条上报消息 |

### GetExceptionCenter <自由函数>

全局异常中心访问器——跨 DLL 经函数访问。

#### 接口

| 签名 | 说明 |
|------|------|
| `MAHO_EXCEPTION_API FException* GetExceptionCenter()` | 返回已初始化的 `FException*`；`Initialize` 前 / `Shutdown` 后为 `nullptr` |

## ExceptionApi.h

### MAHO_EXCEPTION_API <宏>

DLL 导出/导入宏——`MAHO_EXCEPTION_MODULE_EXPORTS` 定义时展开为 `MAHO_EXPORT`，否则为 `MAHO_IMPORT`（详见 `Core/Export.h`）。

#### 宏

| 签名 | 说明 |
|------|------|
| `MAHO_EXCEPTION_API` | 修饰本 DLL 导出的符号（`GetExceptionCenter`、`CreateLayer`） |

- [Exception.md](Exception.md) — 概念 · [实现字典](ImplAPI.md) — 算法
