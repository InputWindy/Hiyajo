# Compress — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 职责边界：字节压缩积木，**纯库**——无单例、无状态，只有自由函数。别给它加生命周期。
- 依赖只走 `.cplugin` `Dependencies`，include `<Compress.h>`，不跨目录相对 include。
- 实现要点：
  - 接口全在 `Public/Compress.h`，`Private/Compress.cpp` 调 zstd C API。
  - zstd 是引擎三方静态库，include 路径与链接由构建系统补全，插件侧不手配。
  - 所有函数失败返回 `std::nullopt`，不抛异常。
- 遵循根 [AGENTS.md](../../../../AGENTS.md)。
