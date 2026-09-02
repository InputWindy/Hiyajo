# Unicode（Private）— 实现算法字典

转换函数实现。公开 API 签名入口见 API.md。

## Unicode.cpp

<a id="fn-unicode-isvalidutf8"></a>
### IsValidUtf8(string_view In)

← [公开 API](API.md) · `bool`

逐字节校验 UTF-8：按首字节高比特位推序列长度，校验连续字节、overlong / 越界码点 / 代理对范围、截断。

```text
IsValidUtf8(In):
1. I = 0; N = In.size()
2. while I < N:
3.   C = byte(In[I])
4.   if C < 0x80: ++I; continue                        // ASCII
5.   按 C 前缀推 Extra(=1..3)、CodepointMin、Mask；非法前缀 → return false
6.   if I + Extra >= N: return false                   // 截断
7.   CP = C & Mask
8.   for K in [1..Extra]:                              // 校验连续字节
9.       CC = byte(In[I+K]); if (CC & 0xC0) != 0x80: return false
10.      CP = (CP << 6) | (CC & 0x3F)
11.  if CP < CodepointMin || CP > 0x10FFFF || (CP 在 [0xD800, 0xDFFF]): return false
12.  I += Extra + 1
13. return true
```

<a id="fn-unicode-utf8toutf16"></a>
### Utf8ToUtf16(string_view In)

← [公开 API](API.md) · `std::u16string`

- Windows：`MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS)` 两次调用（先取尺寸再转换）；失败返回空串。
- 其他平台：按字节加宽（best effort，Windows 是主要目标）。

```text
Utf8ToUtf16(In):                    // _WIN32 分支
1. if In.empty(): return {}
2. Size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, In.data(), len, nullptr, 0)
3. if Size <= 0: return {}
4. Out = u16string(Size, 0); MultiByteToWideChar(CP_UTF8, 0, ..., Out.data(), Size)
5. return Out
```

<a id="fn-unicode-utf16toutf8"></a>
### Utf16ToUtf8(u16string_view In)

← [公开 API](API.md) · `std::string`

- Windows：`WideCharToMultiByte(CP_UTF8, ...)` 两次调用；失败返回空串。
- 其他平台：按字节收窄。

```text
Utf16ToUtf8(In):                    // _WIN32 分支
1. if In.empty(): return {}
2. Size = WideCharToMultiByte(CP_UTF8, 0, In.data(), len, nullptr, 0, nullptr, nullptr)
3. if Size <= 0: return {}
4. Out = string(Size, 0); WideCharToMultiByte(CP_UTF8, 0, ..., Out.data(), Size, ...)
5. return Out
```

<a id="fn-unicode-utf8toutf32"></a>
### Utf8ToUtf32(string_view In)

← [公开 API](API.md) · `std::u32string`

Windows 分支：先 `Utf8ToUtf16`，再逐 UTF-16 单元解码——高代理对 + 低代理对拼成增补平面码点；孤立代理 → 返回空串。

```text
Utf8ToUtf32(In):                    // _WIN32 分支
1. U16 = Utf8ToUtf16(In); Out 预留
2. for I in [0, U16.size()):
3.   Unit = U16[I]
4.   if 高代理(Unit) 且 I+1 存在且为低代理: CP = 0x10000 + ((Hi-0xD800)<<10) + (Lo-0xDC00); ++I
5.   else if Unit 在 [0xD800, 0xE000): return {}          // 孤立代理
6.   else: Out.push_back(Unit)
7. return Out
```

<a id="fn-unicode-utf32toutf8"></a>
### Utf32ToUtf8(u32string_view In)

← [公开 API](API.md) · `std::string`

Windows 分支：逐码点编码——增补平面（>= 0x10000）拆成 UTF-16 代理对；再 `Utf16ToUtf8`。

```text
Utf32ToUtf8(In):                    // _WIN32 分支
1. U16 预留 size*2
2. for CP in In:
3.   if CP < 0x10000: U16.push_back(CP)
4.   else: CP -= 0x10000; U16.push_back(0xD800 + (CP >> 10)); U16.push_back(0xDC00 + (CP & 0x3FF))
5. return Utf16ToUtf8(U16)
```

<a id="fn-unicode-tonative"></a>
### ToNative(string_view Utf8)

← [公开 API](API.md) · `FNativeString`

Windows：`Utf8ToUtf16` 后按字符拷贝进 `std::wstring`；其他平台直接拷贝。

```text
ToNative(Utf8):
1. _WIN32: return wstring(Utf8ToUtf16(Utf8).begin(), .end())
2. else:   return FNativeString(Utf8)
```

<a id="fn-unicode-fromnative"></a>
### FromNative(FNativeStringView Native)

← [公开 API](API.md) · `std::string`

Windows：拷贝进 `std::u16string` 后 `Utf16ToUtf8`；其他平台直接拷贝。

```text
FromNative(Native):
1. _WIN32: return Utf16ToUtf8(u16string(Native.begin(), .end()))
2. else:   return std::string(Native)
```

<a id="fn-unicode-ensureconsole"></a>
### EnsureConsoleUtf8()

← [公开 API](API.md) · `void`

Windows：`SetConsoleOutputCP(CP_UTF8)`；其他平台 no-op。

```text
EnsureConsoleUtf8():
1. _WIN32: SetConsoleOutputCP(CP_UTF8)
```

- [Unicode.md](Unicode.md) — 概念 · [公开 API](API.md) — 签名入口
