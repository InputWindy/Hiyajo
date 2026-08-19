<!-- mahogen -->
# Source

## 子层级

- [Private](Private/PrivateDoc.md)
- [Public](Public/PublicDoc.md)
<!-- mahogen end -->

## 概念——源码根

引擎源码根：`Public/` 放接口与头文件，`Private/` 放实现（.cpp）。

- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口 + 聚合头
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现

引擎核心零三方依赖，几乎全 header-only（模板 + inline），只 `Assembly.cpp` / `Fatal.cpp` 两个实现文件。
