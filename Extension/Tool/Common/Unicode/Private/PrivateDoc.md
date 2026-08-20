# Private

## 代码文件

- [Unicode.cpp](Unicode.cpp)

## 实现算法字典

| `IsValidUtf8` | `utf8::is_valid` |
| `Utf8ToUtf16` 等 | `utf8to16` / `utf16to8` / `utf8to32` / `utf32to8` |
| `ToNative/FromNative` | Windows: wstring 转换；其他: passthrough |
| `EnsureConsoleUtf8` | Windows: `SetConsoleOutputCP(CP_UTF8)` |

## 相关文档

- [../Unicode.md](../Unicode.md) — 概念 + 用法
- [../Public/PublicDoc.md](../Public/PublicDoc.md) — 接口字典
