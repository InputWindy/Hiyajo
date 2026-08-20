# Private

## 代码文件

- [Archive.cpp](Archive.cpp)

## 实现算法字典

Archive 插件的实现集中在 `Archive.cpp`——二进制序列化。

| 函数 | 说明 |
|------|------|
| `FArchive::operator<<(int32/uint32/int64/uint64/float/double/bool)` | 直接 `Serialize(&V, sizeof(V))` |
| `FArchive::operator<<(string)` | 先序列化长度（uint32），再按长度读写字节 |
| `FMemoryReader::Serialize` | 越界直接 return；否则 memcpy 并推进 Pos |
| `FMemoryReader::Seek / Tell` | 设置 / 返回读取位置 |
| `FMemoryWriter::Serialize` | 尾部 resize 扩展缓冲再 memcpy 追加 |
| `FMemoryWriter::Seek` | 位置小于缓冲时截断缓冲 |
| `FMemoryWriter::Tell` | 返回缓冲大小（写入位置） |
| `FMemoryWriter::GetBytes / TakeBytes` | 引用 / 移动取走缓冲 |

## 相关文档

- [../Archive.md](../Archive.md) — 概念 + 用法
- [../Public/PublicDoc.md](../Public/PublicDoc.md) — 接口层

