# Archive

二进制序列化纯库——抽象读写流 + 内存字节流 + 自序列化接口。零三方、纯 std、无单例（header-only 实现）。

## 提供

- `EArchiveMode`：`Read` / `Write`。
- `FArchive`：抽象基类——`Serialize / Seek / Tell` + 内建类型 `operator<<`（int/uint/float/double/bool/string）+ 通用 POD 模板（限 trivially copyable，如 glm::vec3）。
- `ISerialize`：类型自序列化接口（`Serialize(FArchive&)`）。
- `FMemoryReader`：读现有字节 buffer（buffer 必须比 reader 活得久）。
- `FMemoryWriter`：写自持 buffer（`GetBytes` / `TakeBytes`）。

## 示例

```cpp
FMemoryWriter Writer;
int X = 42; Writer << X;
auto Bytes = Writer.TakeBytes();

FMemoryReader Reader(Bytes);
int Y = 0; Reader << Y;   // Y == 42
```

## 依赖

- 三方：无。
- 其他插件：无。
