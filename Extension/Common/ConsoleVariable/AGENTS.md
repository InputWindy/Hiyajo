# ConsoleVariable — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 本插件职责：控制台变量注册表（UE `IConsoleManager` 风格）。就做这一件事，不越界。
- 插件 = 纯单例：`FConsoleVariable : TExtensionList<FConsoleVariable>`，无 Main、无 IAssembly（应用才继承）。
- `Registry` 成员是 `private`——内存细节藏在 cpp，Public 头只暴露 `Find`/`Register`。
- 依赖只走 `.cplugin` `Dependencies`，include `<Name.h>`，不跨目录相对 include。
- 遵循根 [AGENTS.md](../../../AGENTS.md)。

## 文档

- [ConsoleVariable.md](ConsoleVariable.md) — 概念 + 用法
- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典
