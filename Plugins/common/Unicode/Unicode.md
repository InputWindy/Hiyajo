# Unicode

UTF-8/16/32 与平台原生字符串转换纯库——零三方、无单例，纯自由函数。引擎内部字符串恒为 UTF-8 `std::string`，只在平台边界（Windows 文件/控制台）转换。

## 提供

- 类型别名：`FNativeString` / `FNativeStringView`（Windows = `std::wstring(_view)`，其他平台 = `std::string(_view)`）。
- `IsValidUtf8`：校验（无孤立代理/过长编码）。
- `Utf8ToUtf16 / Utf16ToUtf8 / Utf8ToUtf32 / Utf32ToUtf8`：编码互转。
- `ToNative` / `FromNative`：UTF-8 ↔ 平台原生。
- `EnsureConsoleUtf8`：Windows 设 `SetConsoleOutputCP(CP_UTF8)`，其他平台 no-op。

## 示例

```cpp
const bool Ok = Unicode::IsValidUtf8("...");
const std::u16string U16 = Unicode::Utf8ToUtf16("中文");
const FNativeString N = Unicode::ToNative("res/中文.png");
```

## 依赖

- 三方：无。
- 其他插件：无。
