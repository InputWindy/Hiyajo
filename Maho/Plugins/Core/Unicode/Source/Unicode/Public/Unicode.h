#pragma once

// ───────────────────────────────────────────────────────────────────────
// Text encoding layer. Engine-internal strings are always UTF-8 std::string;
// FUnicode only converts at platform boundaries.
//
//   using namespace Maho::Unicode;
//
//   // Validate
//   const bool Ok = IsValidUtf8("hello \xe4\xb8\xad\xe6\x96\x87");
//
//   // UTF-8 ↔ UTF-16 ↔ UTF-32
//   const std::u16string U16 = Utf8ToUtf16("中文");
//   const std::string    S8  = Utf16ToUtf8(U16);
//   const std::u32string U32 = Utf8ToUtf32("emoji \xf0\x9f\x98\x80");
//   const std::string    S8b = Utf32ToUtf8(U32);
//
//   // Platform boundary (Windows: UTF-16; Linux/Android/iOS: UTF-8 passthrough)
//   const FNativeString N = ToNative("res/中文.png");   // feed WinAPI / JNI / NSString
//   const std::string  B  = FromNative(N);              // and back to UTF-8
//
//   // Console (Windows: switch the console codepage to UTF-8)
//   EnsureConsoleUtf8();
//
// Invalid input throws utf8::invalid_utf8 — call IsValidUtf8 first when the
// source is untrusted (files, network).
// ───────────────────────────────────────────────────────────────────────

#include "UnicodeApi.h"
#include <Core/Core.h>

#include <string>
#include <string_view>

namespace Maho
{

namespace Unicode
{

// ───────────────────────────────────────────────────────────────────────
// Platform-native string type.
// Windows: UTF-16 (wstring); everything else: UTF-8 passthrough (string).
// ───────────────────────────────────────────────────────────────────────

#if defined(_WIN32)
	using FNativeString = std::wstring;
	using FNativeStringView = std::wstring_view;
#else
	using FNativeString = std::string;
	using FNativeStringView = std::string_view;
#endif

// ── Validation ──

/** True when In is well-formed UTF-8 (no lone surrogates, overlongs, etc.). */
[[nodiscard]] MAHO_UNICODE_API bool IsValidUtf8(std::string_view In);

// ── UTF conversion (UTF-8 ↔ UTF-16 ↔ UTF-32) ──

[[nodiscard]] MAHO_UNICODE_API std::u16string Utf8ToUtf16(std::string_view In);
[[nodiscard]] MAHO_UNICODE_API std::string Utf16ToUtf8(std::u16string_view In);
[[nodiscard]] MAHO_UNICODE_API std::u32string Utf8ToUtf32(std::string_view In);
[[nodiscard]] MAHO_UNICODE_API std::string Utf32ToUtf8(std::u32string_view In);

// ── Platform boundary ──

/** UTF-8 → platform-native (Windows: UTF-16; others: passthrough). */
[[nodiscard]] MAHO_UNICODE_API FNativeString ToNative(std::string_view Utf8);

/** platform-native → UTF-8 (Windows: UTF-16; others: passthrough). */
[[nodiscard]] MAHO_UNICODE_API std::string FromNative(FNativeStringView Native);

// ── Console ──

/** Windows: SetConsoleOutputCP(CP_UTF8). No-op elsewhere. */
MAHO_UNICODE_API void EnsureConsoleUtf8();

// ── Extension (pre-app singleton; pure function library, no state) ──

/** Text encoding extension (UTF-8/16/32 conversion). Pre-app toolkit (driven by EToolStage). */
class MAHO_UNICODE_API FUnicode : public TExtension<EToolStage, FUnicode>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

protected:
	friend TSingleton<FUnicode>;
	FUnicode() = default;
};

} // namespace Unicode

} // namespace Maho
