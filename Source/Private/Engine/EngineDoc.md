# Engine（Private）

## 代码文件

*（无——Engine 层全 header-only，无 .cpp 实现）*

## 说明

`Engine/Layer.h` 是纯模板 + 内联实现（FLayerBase / FLayer / 命令 / DispatchInstance 全在头文件）。无 Private 侧编译单元，因此没有实现算法字典。

- 层依赖的跨平台原语（DLL 加载、致命错误）在 `Core` 的 `Assembly.cpp` / `Fatal.cpp`——见 `../Core/CoreDoc.md`。
- 并行执行在 `Core/Schedulers.h` + `Core/ThreadPool.h`（header-only 模板 + 内联）。

## 相关文档

- [../../Public/Engine/EngineDoc.md](../../Public/Engine/EngineDoc.md) — 层架构（Public）
- [../../Public/Core/CoreDoc.md](../../Public/Core/CoreDoc.md) — Core 基建概念
- [../../PrivateDoc.md](../../PrivateDoc.md) — Private 层
