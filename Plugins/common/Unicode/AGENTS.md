# Unicode — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 职责边界：编码转换积木，**纯库**——无单例、无状态，自由函数。别给它加生命周期。
- 引擎内部一律 UTF-8 `std::string`；只在平台边界（Windows API 调用、文件路径）用 `ToNative/FromNative` 转换，不要到处转码。
- 依赖只走 `.cplugin` `Dependencies`，include `<Unicode.h>`，不跨目录相对 include。
- 实现要点：
  - 接口全在 `Public/Unicode.h`，`Private/Unicode.cpp` 实现；Windows 下经 WinAPI 转 UTF-16，其他平台 passthrough。
  - `EnsureConsoleUtf8` 仅 Windows 生效。
- 遵循根 [AGENTS.md](../../../../AGENTS.md)。
