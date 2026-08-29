# Unicode

## Code Files

- [Unicode.h](Unicode.h) - UTF-8/16/32 + platform-native conversion pure library (`FNativeString` / `IsValidUtf8` / `Utf8ToUtf16` / `ToNative` / `EnsureConsoleUtf8`)

## Concept - Character Encoding Conversion

UTF-8/16/32 and platform-native string conversion **pure library** - zero third-party, no singleton, pure free functions. Engine-internal strings are **always UTF-8 `std::string`**; convert only at platform boundaries (Windows file paths / console). On Windows, UTF-8 <-> UTF-16 goes through WinAPI (`MultiByteToWideChar` / `WideCharToMultiByte`); other platforms passthrough (best effort, Windows is the primary target).

### Type Aliases

- `FNativeString` / `FNativeStringView`: Windows = `std::wstring(_view)`; others = `std::string(_view)`.

### Free Functions

- `IsValidUtf8(In)`: validate well-formed UTF-8 (rejects lone surrogates / overlong / out-of-range code points / truncated sequences).
- `Utf8ToUtf16 / Utf16ToUtf8 / Utf8ToUtf32 / Utf32ToUtf8`: encoding interconversion (UTF-32 <-> UTF-8 via UTF-16 surrogate pairs).
- `ToNative(Utf8)` / `FromNative(Native)`: UTF-8 <-> platform-native.
- `EnsureConsoleUtf8()`: Windows `SetConsoleOutputCP(CP_UTF8)`; no-op elsewhere.

```cpp
const bool Ok = Unicode::IsValidUtf8("...");
const std::u16string U16 = Unicode::Utf8ToUtf16("hello");
const FNativeString   N  = Unicode::ToNative("res/hello.png");
Unicode::EnsureConsoleUtf8();
```

## Third-Party Dependencies

- None (pure std + WinAPI).

## Related Docs

- [API.html](API.html) - API docs
