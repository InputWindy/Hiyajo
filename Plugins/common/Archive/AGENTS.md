# Archive — Agent 入口

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 职责边界：二进制序列化积木，**纯库**——无单例、无状态，就是自由类 + 自由函数。别给它加生命周期。
- 依赖只走 `.cplugin` `Dependencies`，include `<Archive.h>`，不跨目录相对 include。
- 实现要点：
  - 接口全在 `Public/Archive.h`（header-only），`Private/Archive.cpp` 只实现模板外方法。
  - `FMemoryReader` 引用外部 buffer 不拷贝——注意生命周期。
  - 通用 `operator<<` 模板有 `static_assert(std::is_trivially_copyable_v<T>)`；非 POD 类型走 `ISerialize`。
- 遵循根 [AGENTS.md](../../../../AGENTS.md)。
