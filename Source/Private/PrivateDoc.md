<!-- mahogen -->
# Private

## 子层级

- [Core](Core/CoreDoc.md)
<!-- mahogen end -->

## 概念——实现层

引擎实现代码。核心几乎全 header-only，只 `Core/` 下两个 `.cpp`：

- [Core/Assembly.cpp](Core/Assembly.cpp) — 动态加载原语（`FAssembly`）
- [Core/Fatal.cpp](Core/Fatal.cpp) — 崩溃兜底

## 相关文档

- [Core/CoreDoc.md](Core/CoreDoc.md) — 实现算法字典
- [../Public/PublicDoc.md](../Public/PublicDoc.md) — 接口层
- [../SourceDoc.md](../SourceDoc.md) — 源码根
