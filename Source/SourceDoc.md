# Source

## Sub Layers

- [Public](Public/PublicDoc.md) — 公开接口（头文件）
- [Private](Private/PrivateDoc.md) — 实现（cpp）

## Concept -- Source Root

引擎源码分两层：**Public**（`Source/Public/`）只放公开头文件，任何模块/插件可 include；**Private**（`Source/Private/`）放对应实现 cpp，不外露。引擎核心按能力拆成 `Core` 与 `Engine` 两个子模块，见各自文档。

### Layering

```
Source/
  Public/           公开头（接口 + 声明）
    Core/           类型无关基础设施
    Engine/         层系统（FLayer / FLayerTaskGraph / FEngineBase）
  Private/          实现 cpp
    Core/           Assembly.cpp / Fatal.cpp / TaskGraph.cpp
    Engine/         Engine.cpp / Layer.cpp
```

引擎核心零第三方依赖、零应用假设；具体功能全在 `Plugins/`，核心只提供通用构件。

## Related Docs

- [Public/PublicDoc.md](Public/PublicDoc.md) — 公开层
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现层
- [README.md](../README.md) — 引擎总览
