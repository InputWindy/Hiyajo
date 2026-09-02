# Unicode — API 文档

Unicode 插件 = UTF-8/16/32 与平台原生字符串转换纯库（namespace `Maho::Unicode`）。零第三方依赖、无生命周期、无单例——纯自由函数。引擎内部字符串**始终是 UTF-8 `std::string`**，只在平台边界转换（Windows：UTF-16 经 WinAPI；其余平台直通）。

## FNativeString / FNativeStringView <alias>

平台原生字符串别名：Windows = `std::wstring(_view)`；其他 = `std::string(_view)`（以 `_WIN32` 判定）。

| 别名 | 类型 | 说明 |
|------|------|------|
| `FNativeString` | `std::wstring` / `std::string` | 平台原生字符串 |
| `FNativeStringView` | `std::wstring_view` / `std::string_view` | 对应视图 |

## IsValidUtf8 <自由函数>

校验输入是否为良好 UTF-8（拒绝孤立代理对 / overlong / 越界码点 / 截断序列）。

#### 接口

| 签名 | 说明 |
|------|------|
| `bool IsValidUtf8(std::string_view In)` | 良好 UTF-8 → `true`；否则 `false` |

## Utf8ToUtf16 / Utf16ToUtf8 / Utf8ToUtf32 / Utf32ToUtf8 <自由函数>

编码互转。Windows 上经 WinAPI（`MultiByteToWideChar` / `WideCharToMultiByte`），UTF-32 <-> UTF-8 走 UTF-16 代理对中转；其他平台直通（字节加宽/收窄，best effort，Windows 是主要目标）。

#### 接口

| 签名 | 说明 |
|------|------|
| `std::u16string Utf8ToUtf16(std::string_view In)` | UTF-8 → UTF-16 |
| `std::string Utf16ToUtf8(std::u16string_view In)` | UTF-16 → UTF-8 |
| `std::u32string Utf8ToUtf32(std::string_view In)` | UTF-8 → UTF-32（经 UTF-16 代理对解码） |
| `std::string Utf32ToUtf8(std::u32string_view In)` | UTF-32 → UTF-8（经 UTF-16 代理对编码） |

## ToNative / FromNative <自由函数>

UTF-8 <-> 平台原生。Windows：UTF-8 <-> UTF-16（`wstring`）；其他：直通。

#### 接口

| 签名 | 说明 |
|------|------|
| `FNativeString ToNative(std::string_view Utf8)` | UTF-8 → 平台原生 |
| `std::string FromNative(FNativeStringView Native)` | 平台原生 → UTF-8 |

## EnsureConsoleUtf8 <自由函数>

Windows 下 `SetConsoleOutputCP(CP_UTF8)`；其他平台 no-op。

#### 接口

| 签名 | 说明 |
|------|------|
| `void EnsureConsoleUtf8()` | 控制台输出切 UTF-8（仅 Windows 有效） |

- [Unicode.md](Unicode.md) — 概念 · [实现字典](ImplAPI.md) — 算法
