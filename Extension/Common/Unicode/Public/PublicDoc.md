# Public

## 代码文件

- [Unicode.h](Unicode.h)
- [UnicodeApi.h](UnicodeApi.h)

## 接口字典

| `FUnicode : TExtensionList<FUnicode>` | 编码扩展单例 |
| `IsValidUtf8` | UTF-8 合法性校验 |
| `Utf8ToUtf16/Utf16ToUtf8/Utf8ToUtf32/Utf32ToUtf8` | UTF 转换 |
| `ToNative/FromNative` | 平台边界转换 |
| `EnsureConsoleUtf8` | Windows 控制台 UTF-8 |

## 相关文档

- [../Unicode.md](../Unicode.md) — 概念 + 用法
- [../Private/PrivateDoc.md](../Private/PrivateDoc.md) — 实现算法字典
