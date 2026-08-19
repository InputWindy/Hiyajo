# Exception — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 本插件职责：非致命异常处理（组播订阅 + 上报）。就做这一件事，不越界；致命错误走 `Core/Fatal`。
- 插件 = 纯单例：`FException : TExtensionList<FException>`，无 Main、无 IAssembly（应用才继承）。
- 旧引擎的 unicast `TDelegate` 换成新引擎的 `TMulticastDelegate`（一对多订阅，`Add`/`Broadcast`/`Clear`）。
- 依赖只走 `.cplugin` `Dependencies`，include `<Name.h>`，不跨目录相对 include。
- 遵循根 [AGENTS.md](../../../AGENTS.md)。

## 文档

- [Exception.md](Exception.md) — 概念 + 用法
- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典
