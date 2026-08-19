# Config — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 本插件职责：INI 风格配置解析（section + key=value）。就做这一件事，不越界。
- 插件 = 纯单例：`FConfig : TExtensionList<FConfig>`，无 Main、无 IAssembly（应用才继承）。
- 依赖只走 `.cplugin` `Dependencies`，include `<Name.h>`，不跨目录相对 include。
- 无三方依赖，`Config.cmake` 保持占位注释。
- 遵循根 [AGENTS.md](../../../AGENTS.md)。

## 文档

- [Config.md](Config.md) — 概念 + 用法
- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典

