# Archive

## 代码文件

- [Archive.h](Archive.h) — 二进制序列化纯库（`EArchiveMode` / `FArchive` / `ISerialize` / `FMemoryReader` / `FMemoryWriter`）

## 概念——二进制序列化

二进制序列化**纯库**——抽象读写流 + 内存字节流 + 自序列化接口。**无单例、无状态、零三方**，header-only 实现，编进使用方。`FArchive` 是原始字节与类型化数据之间的桥：写方向内存流 `<<` 数据，读方向从内存流 `>>` 还原。

### FArchive —— 抽象读写流

- 虚接口：`Serialize(void*, size_t)`（memcpy 语义）/ `Seek` / `Tell`，`IsReading / IsWriting` 区分方向。
- 内建类型 `operator<<`：int32/uint32/int64/uint64/float/double/bool/`std::string`（string 前置 uint32 长度）。
- 通用 POD 模板 `operator<<(T&)`：`static_assert` trivially copyable（如 `glm::vec3`），直接整块序列化。

### ISerialize —— 自序列化接口

类型实现 `void Serialize(FArchive& Ar)`，把全部字段 `Ar <<` 出去——同一函数同时处理读与写。

### FMemoryReader / FMemoryWriter —— 内存字节流

- `FMemoryReader`：持**外部** buffer 引用（buffer 必须比 reader 活得久），越界读静默跳过。
- `FMemoryWriter`：写进**自持** buffer，`GetBytes()` 查看 / `TakeBytes()` 移走。

```cpp
// Writing
FMemoryWriter Writer;
int X = 42; Writer << X;
auto Bytes = Writer.TakeBytes();

// Reading
FMemoryReader Reader(Bytes);
int Y = 0; Reader << Y;   // Y == 42
```

## 三方依赖

- 无（纯 std）。

## 相关文档

- [API.html](API.html) — API 文档
