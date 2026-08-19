# Public

## 代码文件

- [Exception.h](Exception.h)
- [ExceptionApi.h](ExceptionApi.h)

## 接口字典

| 声明 | 说明 |
|------|------|
| `FException : TExtensionList<FException>` | 异常单例（纯单例，无 Main/IAssembly） |
| `EExceptionStage` | 本插件自定义 drive stage（Init / Shutdown） |
| `FException::ExecuteStage(EExceptionStage)` | 阶段分发，清空订阅 |
| `FException::ReportException(std::string_view)` | 广播一条非致命异常消息 |
| `FException::ReportException(const std::exception&)` | 从 `std::exception` 转发 `what()` |
| `FException::OnException` | void 组播委托 `Maho::TMulticastDelegate<void(const std::string&)>` |

## 相关文档

- [../Exception.md](../Exception.md) — 概念 + 用法
- [../Private/PrivateDoc.md](../Private/PrivateDoc.md) — 实现算法字典
