<!-- mahogen -->
# Private

## 子层级

- [Core](Core/CoreDoc.md)
- [Engine](Engine/EngineDoc.md)
<!-- mahogen end -->

## 概念——实现层

引擎实现代码。核心几乎全 header-only，只有 `Core/` 下两个 `.cpp`；Engine 层无 Private 编译单元（Layer.h 全 header-only）。

- [Core/Assembly.cpp](Core/Assembly.cpp) — 动态加载原语（`FAssembly`）
- [Core/Fatal.cpp](Core/Fatal.cpp) — 崩溃兜底
- [Engine/EngineDoc.md](Engine/EngineDoc.md) — Engine 层（无实现，说明页）

## 相关文档

- [Core/CoreDoc.md](Core/CoreDoc.md) — 实现算法字典
- [Engine/EngineDoc.md](Engine/EngineDoc.md) — Engine 层说明
- [../Public/PublicDoc.md](../Public/PublicDoc.md) — 接口层
- [../SourceDoc.md](../SourceDoc.md) — 源码根
