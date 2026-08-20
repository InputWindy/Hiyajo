# Unicode

Text encoding extension (UTF-8/16/32).

## 扩展类

| 字段 | 值 |
|------|-----|
| Class | `Maho::Unicode::FUnicode` |
| Header | `Unicode.h` |
| Stage | 无（纯函数库） |
| Dependencies | — |

## 说明

字符编码扩展，封装 utfcpp。引擎内部字符串统一 UTF-8，本插件在平台边界做转换（UTF-8 ↔ UTF-16 ↔ UTF-32），并处理 Windows 控制台 UTF-8 切换。无生命周期。

## 用法

```cpp
#include <Unicode.h>
using namespace Maho::Unicode;

const bool Ok = IsValidUtf8("hello");
const std::u16string U16 = Utf8ToUtf16("中文");
const FNativeString N = ToNative("res/emoji.png");
```

## 三方依赖

- utfcpp（`Unicode.cmake` FetchContent 拉取，header-only）

## 相关文档

- [API.html](API.html) — API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现算法字典
- [../../AGENTS.md](../../AGENTS.md) — 引擎根 Agent 入口
- [../../Source/Public/Core/CoreDoc.md](../../Source/Public/Core/CoreDoc.md) — 核心基础设施
