# Unicode

## Code files

- [Unicode.h](Public/Unicode.h) — UTF-8/16/32 + 平台原生转换纯库（`FNativeString` / `IsValidUtf8` / 转换函数族 / `EnsureConsoleUtf8`）
- [Unicode.cpp](Private/Unicode.cpp) — 转换实现（Windows WinAPI / 其他直通）

## Concept - 字符编码转换

UTF-8/16/32 与平台原生字符串转换**纯库**——零第三方、无单例、纯自由函数。引擎内部字符串**始终是 UTF-8 `std::string`**，只在平台边界转换（Windows 文件路径 / 控制台）。Windows 上 UTF-8 <-> UTF-16 走 WinAPI（`MultiByteToWideChar` / `WideCharToMultiByte`）；其余平台直通（best effort，Windows 是主要目标）。

### 类型别名

- `FNativeString` / `FNativeStringView`：Windows = `std::wstring(_view)`；其他 = `std::string(_view)`。

### 校验与转换函数

- `IsValidUtf8(In)`：验证良好 UTF-8（拒绝孤立代理对 / overlong / 越界码点 / 截断序列）。
- `Utf8ToUtf16 / Utf16ToUtf8 / Utf8ToUtf32 / Utf32ToUtf8`：编码互转；UTF-32 <-> UTF-8 经 UTF-16 代理对。
- `ToNative(Utf8)` / `FromNative(Native)`：UTF-8 <-> 平台原生。
- `EnsureConsoleUtf8()`：Windows `SetConsoleOutputCP(CP_UTF8)`；其他 no-op。

```cpp
const bool Ok = Unicode::IsValidUtf8("...");
const std::u16string U16 = Unicode::Utf8ToUtf16("hello");
const FNativeString   N  = Unicode::ToNative("res/hello.png");
Unicode::EnsureConsoleUtf8();
```

## Third-party dependencies

- None（pure std + WinAPI）。

## Related docs

- [API.md](API.md) - API documentation
