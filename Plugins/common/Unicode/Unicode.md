# Unicode

## 代码文件

- [Unicode.h](Unicode.h) — UTF-8/16/32 + 平台原生转换纯库（`FNativeString` / `IsValidUtf8` / `Utf8ToUtf16` / `ToNative` / `EnsureConsoleUtf8`）

## 概念——字符编码转换

UTF-8/16/32 与平台原生字符串转换**纯库**——零三方、无单例、纯自由函数。引擎内部字符串**恒为 UTF-8 `std::string`**，只在平台边界（Windows 文件路径 / 控制台）转换。Windows 上 UTF-8 ↔ UTF-16 走 WinAPI（`MultiByteToWideChar` / `WideCharToMultiByte`）；其他平台 passthrough（最佳努力，Windows 是主目标）。

### 类型别名

- `FNativeString` / `FNativeStringView`：Windows = `std::wstring(_view)`；其他 = `std::string(_view)`。

### 自由函数

- `IsValidUtf8(In)`：校验良构 UTF-8（拒绝孤立代理 / overlong / 越界码点 / 截断序列）。
- `Utf8ToUtf16 / Utf16ToUtf8 / Utf8ToUtf32 / Utf32ToUtf8`：编码互转（UTF-32 ↔ UTF-8 经 UTF-16 代理对）。
- `ToNative(Utf8)` / `FromNative(Native)`：UTF-8 ↔ 平台原生。
- `EnsureConsoleUtf8()`：Windows `SetConsoleOutputCP(CP_UTF8)`；其他平台 no-op。

```cpp
const bool Ok = Unicode::IsValidUtf8("...");
const std::u16string U16 = Unicode::Utf8ToUtf16("中文");
const FNativeString   N  = Unicode::ToNative("res/中文.png");
Unicode::EnsureConsoleUtf8();
```

## 三方依赖

- 无（纯 std + WinAPI）。

## 相关文档

- [API.html](API.html) — API 文档
