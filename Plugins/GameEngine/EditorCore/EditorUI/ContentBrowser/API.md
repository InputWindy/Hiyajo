# ContentBrowser — API 文档

Editor content browser panel - virtual-filesystem view of the project/engine Content roots (.casset only) with OS file-drop import.

## FContentBrowser <class : FLayer<...>>

插件骨架。把要实现的 stage 接口（IInit/ITick/...）填进 `FLayer<...>` 模板列表并覆写，
然后在 `.cplugin` 的 `Dependencies` 手填依赖插件。

- [ContentBrowser.md](ContentBrowser.md) — 概念
