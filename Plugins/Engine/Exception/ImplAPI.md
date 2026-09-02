# Exception（Private）— 实现算法字典

cpp 侧每个函数的算法伪代码解释。Public 侧 API 文档通过 `#fn-...` 锚点跳转落到这里。

## Exception.cpp

<a id="fn-exception-get"></a>
### GetExceptionCenter()

← [公开 API](API.md) · `FException*`

返回全局指针 `GExceptionCenter`——`Initialize` 置 `this`，`Shutdown` 置空。

```text
GetExceptionCenter():
1. return GExceptionCenter
```

<a id="fn-exception-init"></a>
### FException::Initialize(FEngineBase& Engine)

← [公开 API](API.md) · `void`

发布 `this` 并清空旧订阅——保证每次引擎启动从干净的订阅集开始。

```text
Initialize(Engine):
1. OnException.RemoveAll()
2. GExceptionCenter = this
```

<a id="fn-exception-shutdown"></a>
### FException::Shutdown(FEngineBase&)

← [公开 API](API.md) · `void`

回收：撤回 `this`，清空订阅。

```text
Shutdown(Engine):
1. GExceptionCenter = nullptr
2. OnException.RemoveAll()
```

<a id="fn-exception-report"></a>
### FException::ReportException(std::string_view Message)

← [公开 API](API.md) · `void`

把消息转成 `std::string` 广播给所有订阅者。

```text
ReportException(Message):
1. OnException.Broadcast(string(Message))
```

<a id="fn-exception-report-ex"></a>
### FException::ReportException(const std::exception& Error)

← [公开 API](API.md) · `void`

转发 `std::exception::what()`。

```text
ReportException(Error):
1. ReportException(Error.what())
```

<a id="fn-exception-createlayer"></a>
### CreateLayer()（extern "C"）

DLL 动态安装入口——宿主按符号名查找。委托静态 `FException::CreateLayer()`（`MAHO_DECLARE_LAYER` 生成）。

```text
CreateLayer():
1. return Maho::Exception::FException::CreateLayer()
```

- [Exception.md](Exception.md) — 概念 · [公开 API](API.md) — 签名入口
