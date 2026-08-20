# Archive

Serialization archive extension.

## 扩展类

| 字段 | 值 |
|------|-----|
| Class | —（纯函数库，无单例/无 stage） |
| Header | `Archive.h` |
| Stage | — |
| Dependencies | — |

## 说明

二进制序列化库——原始字节与类型化数据之间的桥。纯函数库，无生命周期、无单例、无 stage。提供：

- `FArchive`：抽象基类，`Serialize`/`Seek`/`Tell` 纯虚 + 内建类型 `operator<<`。
- `ISerialize`：自序列化类型接口。
- `FMemoryReader`：从已有字节缓冲读取。
- `FMemoryWriter`：累积到自持字节缓冲写入。

### 用法

```cpp
#include <Archive.h>
using namespace Maho::Archive;

// 写
FMemoryWriter Writer;
int X = 42; Writer << X;
auto Bytes = Writer.TakeBytes();

// 读
FMemoryReader Reader(Bytes);
int Y = 0; Reader << Y;   // Y == 42
```

## 三方依赖

无。

## 相关文档

- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典
- [../../Source/Public/Core/CoreDoc.md](../../Source/Public/Core/CoreDoc.md) — 核心基础设施

