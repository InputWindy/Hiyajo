#pragma once

#include "UnicodeApi.h"

// ───────────────────────────────────────────────────────────────────────
// Text encoding layer. Engine-internal strings are always UTF-8 std::string;
// FUnicode only converts at platform boundaries.
//   const bool Ok = IsValidUtf8("...");
//   const std::u16string U16 = Utf8ToUtf16("中文");
//   const FNativeString   N  = ToNative("res/中文.png");
// Invalid input throws utf8::invalid_utf8 — check IsValidUtf8 first on
// untrusted input (files, network).
// ───────────────────────────────────────────────────────────────────────
#include <string>
#include <string_view>

namespace Maho
{
namespace Unicode
{

#if defined(_WIN32)
	using FNativeString = std::wstring;
	using FNativeStringView = std::wstring_view;
#else
	using FNativeString = std::string;
	using FNativeStringView = std::string_view;
#endif

/** True when In is well-formed UTF-8. */
MAHO_UNICODE_API bool IsValidUtf8(std::string_view In);

/** UTF-8 ↔ UTF-16 ↔ UTF-32. */
MAHO_UNICODE_API std::u16string Utf8ToUtf16(std::string_view In);
MAHO_UNICODE_API std::string Utf16ToUtf8(std::u16string_view In);
MAHO_UNICODE_API std::u32string Utf8ToUtf32(std::string_view In);
MAHO_UNICODE_API std::string Utf32ToUtf8(std::u32string_view In);

/** UTF-8 → platform-native (Windows: UTF-16; others: passthrough). */
MAHO_UNICODE_API FNativeString ToNative(std::string_view Utf8);
/** platform-native → UTF-8. */
MAHO_UNICODE_API std::string FromNative(FNativeStringView Native);

/** Windows: SetConsoleOutputCP(CP_UTF8). No-op elsewhere. */
MAHO_UNICODE_API void EnsureConsoleUtf8();

} // namespace Unicode
} // namespace Maho
