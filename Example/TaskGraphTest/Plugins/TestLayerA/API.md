# TestLayerA — API 文档

test layer for the TaskGraph sandbox

## FTestLayerA <class : FFrameExtension + IPipeline<...>>

插件骨架。把要实现的 stage 接口（IInit/ITick/...）填进 `FFrameExtension + IPipeline<...>` 模板列表并覆写，
然后在 `.cplugin` 的 `Dependencies` 手填依赖插件。

- [TestLayerA.md](TestLayerA.md) — 概念
