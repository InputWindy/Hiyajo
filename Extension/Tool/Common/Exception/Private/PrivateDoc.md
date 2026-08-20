# Private

## 代码文件

- [Exception.cpp](Exception.cpp)

## 实现算法字典

| 函数 | 说明 |
|------|------|
| `FException::ExecuteStage(EExceptionStage Stage)` | `Init`/`Shutdown` 都 `OnException.Clear()` |
| `FException::ReportException(std::string_view Message)` | `OnException.Broadcast(std::string(Message))` |
| `FException::ReportException(const std::exception& Error)` | 取 `Error.what()` 转发到 string_view 重载 |

## 相关文档

- [../Exception.md](../Exception.md) — 概念 + 用法
- [../Public/PublicDoc.md](../Public/PublicDoc.md) — 接口层
