# EditorOutliner — API 文档

Editor outliner panel - lists scene entities, publishes selected id.

## FEditorOutliner <class : FLayer<...>>

插件骨架。把要实现的 stage 接口（IInit/ITick/...）填进 `FLayer<...>` 模板列表并覆写，
然后在 `.cplugin` 的 `Dependencies` 手填依赖插件。

- [EditorOutliner.md](EditorOutliner.md) — 概念
