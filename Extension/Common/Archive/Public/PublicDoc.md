# Public

## 代码文件

- [Archive.h](Archive.h)
- [ArchiveApi.h](ArchiveApi.h)

## 接口字典

| 声明 | 说明 |
|------|------|
| `EArchiveMode` | 序列化方向（Read / Write） |
| `FArchive` | 抽象基类：`Serialize`/`Seek`/`Tell` 纯虚 + 内建类型 `<<` + 泛型 POD `<<` |
| `ISerialize` | 自序列化接口（`Serialize(FArchive&)`） |
| `FMemoryReader` | 从已有字节缓冲读取（缓冲须比 reader 活得久） |
| `FMemoryWriter` | 累积到自持缓冲写入；`GetBytes`/`TakeBytes` |

## 相关文档

- [../Archive.md](../Archive.md) — 概念 + 用法
- [../Private/PrivateDoc.md](../Private/PrivateDoc.md) — 实现算法字典

