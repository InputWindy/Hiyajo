#pragma once

// -----------------------------------------------------------------------
// Unicode - UTF-8/16/32 + platform-native string conversion (zero third-party).
// Engine-internal strings are always UTF-8 std::string; convert only at
// platform boundaries (Windows: UTF-16 via WinAPI; elsewhere: passthrough).
//
//   const bool Ok = Unicode::IsValidUtf8("...");
//   const std::u16string U16 = Unicode::Utf8ToUtf16("hello");
//   const FNativeString   N  = Unicode::ToNative("res/hello.png");
// -----------------------------------------------------------------------
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

/** True when In is well-formed UTF-8 (no lone surrogates / overlongs). */
bool IsValidUtf8(std::string_view In);

/** UTF-8 <-> UTF-16 <-> UTF-32. */
std::u16string Utf8ToUtf16(std::string_view In);
std::string Utf16ToUtf8(std::u16string_view In);
std::u32string Utf8ToUtf32(std::string_view In);
std::string Utf32ToUtf8(std::u32string_view In);

/** UTF-8 -> platform-native (Windows: UTF-16; others: passthrough). */
FNativeString ToNative(std::string_view Utf8);
/** platform-native -> UTF-8. */
std::string FromNative(FNativeStringView Native);

/** Windows: SetConsoleOutputCP(CP_UTF8). No-op elsewhere. */
void EnsureConsoleUtf8();

} // namespace Unicode
} // namespace Maho
