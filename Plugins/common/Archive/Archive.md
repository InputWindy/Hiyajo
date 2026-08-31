# Archive

## Code files

- [Archive.h](Public/Archive.h) — 二进制序列化纯库（`EArchiveMode` / `FArchive` / `ISerialize` / `FMemoryReader` / `FMemoryWriter`）
- [Archive.cpp](Private/Archive.cpp) — 内建 typed 序列化算子 + 内存字节流实现

## Concept - 二进制序列化

二进制序列化**纯库**——抽象读写流 + 内存字节流 + 自序列化接口。**无单例、无状态、零第三方依赖**，接口集中在 `Archive.h`，内建 typed 算子在 `Archive.cpp` 编译进插件目标。`FArchive` 是原始字节与类型化数据之间的桥：写入方向经 `<<` 把数据流进字节流，读取方向从字节流恢复出数据。

### FArchive — 抽象读写流

- 虚接口：`Serialize(void*, size_t)`（memcpy 语义）/ `Seek` / `Tell`；`IsReading` / `IsWriting` 区分方向。
- 内建 `operator<<`：int32/uint32/int64/uint64/float/double/bool/`std::string`（string 前带 `uint32` 长度前缀）。
- 通用 POD 模板 `operator<<(T&)`：`static_assert` 平凡可拷贝（如 `glm::vec3`），整块序列化，header 内联。

### ISerialize — 自序列化接口

类型实现 `void Serialize(FArchive& Ar)` 并把所有字段用 `Ar <<` 推出——同一个函数同时处理读和写。

### FMemoryReader / FMemoryWriter — 内存字节流

- `FMemoryReader`：持有**外部**缓冲的引用（缓冲须比 reader 存活更久）；越界读静默跳过（TODO 报告越界）。
- `FMemoryWriter`：写进**自有**缓冲；`GetBytes()` 只读视图 / `TakeBytes()` 移动取走。

```cpp
// Writing
FMemoryWriter Writer;
int X = 42; Writer << X;
auto Bytes = Writer.TakeBytes();

// Reading
FMemoryReader Reader(Bytes);
int Y = 0; Reader << Y;   // Y == 42
```

## Third-party dependencies

- None（pure std）。

## Related docs

- [API.md](API.md) - API documentation
