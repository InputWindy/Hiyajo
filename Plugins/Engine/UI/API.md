# UI — API 文档

UI engine layer: hosts the Dear ImGui context (CPU side) and drives it from the engine stages (IInit/ITick/IShutdown); the UI render feature draws the produced draw data over the scene before present

## FUI <class : FLayer<...>>

插件骨架。把要实现的 stage 接口（IInit/ITick/...）填进 `FLayer<...>` 模板列表并覆写，
然后在 `.cplugin` 的 `Dependencies` 手填依赖插件。

- [UI.md](UI.md) — 概念
