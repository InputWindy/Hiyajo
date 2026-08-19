# Asset — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 本插件职责：资产路径 + 注册表（逻辑路径 → 物理文件 + 类型）。就做这一件事，不越界。
- 插件 = 纯单例：`FAssetRegistry : TExtensionList<FAssetRegistry>`，带 stage（`EAssetStage: Init / Shutdown`），无 Main、无 IAssembly（应用才继承）。
- mount alias → root 解析逻辑内联在 `FAssetRegistry` 内（`MountRoots`），不再依赖独立的 `Paths` 插件。
- 依赖只走 `.cplugin` `Dependencies`，include `<Name.h>`，不跨目录相对 include。
- 无三方依赖，`Asset.cmake` 保持占位注释。
- 遵循根 [AGENTS.md](../../../AGENTS.md)。

## 文档

- [Asset.md](Asset.md) — 概念 + 用法
- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典

