#pragma once

// ───────────────────────────────────────────────────────────────────────
// Text encoding layer. Engine-internal strings are always UTF-8 std::string;
// FUnicodeTool only converts at platform boundaries.
//
//   using namespace Maho::Unicode;
//
//   const bool Ok = IsValidUtf8("hello");
//   const std::u16string U16 = Utf8ToUtf16("中文");
//   const std::string S8 = Utf16ToUtf8(U16);
//   const FNativeString N = ToNative("res/中文.png");
//   const std::string B = FromNative(N);
//   EnsureConsoleUtf8();
//
// Invalid input throws utf8::invalid_utf8 — call IsValidUtf8 first when the
// source is untrusted (files, network).
// ───────────────────────────────────────────────────────────────────────

#include "UnicodeApi.h"
#include <Maho.h>
#include <Engine/Tool.h>

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
[[nodiscard]] MAHO_UNICODE_API bool IsValidUtf8(std::string_view In);

// UTF-8 ↔ UTF-16 ↔ UTF-32 conversion.
[[nodiscard]] MAHO_UNICODE_API std::u16string Utf8ToUtf16(std::string_view In);
[[nodiscard]] MAHO_UNICODE_API std::string Utf16ToUtf8(std::u16string_view In);
[[nodiscard]] MAHO_UNICODE_API std::u32string Utf8ToUtf32(std::string_view In);
[[nodiscard]] MAHO_UNICODE_API std::string Utf32ToUtf8(std::u32string_view In);

// Platform boundary: UTF-8 → native (Windows: UTF-16; others: passthrough).
[[nodiscard]] MAHO_UNICODE_API FNativeString ToNative(std::string_view Utf8);
[[nodiscard]] MAHO_UNICODE_API std::string FromNative(FNativeStringView Native);

/** Windows: SetConsoleOutputCP(CP_UTF8). No-op elsewhere. */
MAHO_UNICODE_API void EnsureConsoleUtf8();

/** Text encoding extension (UTF-8/16/32). A plain singleton, pure function library. */
class MAHO_UNICODE_API FUnicodeTool : public Maho::TTool<FUnicodeTool>
{
public:
	/** Aggregate identity tags: base + this Tool's own (empty for now). */
	using FTags = TCatch<typename Maho::TTool<FUnicodeTool>::FTags, TTypeList<>>::Type;

	// Pure function library — no lifecycle, no stage.
};

} // namespace Unicode

} // namespace Maho
