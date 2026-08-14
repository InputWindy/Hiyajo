#pragma once

/**
 * UTF-8 path helpers for Windows (native wide APIs) and portable filesystem::path bridging.
 * Engine path strings are UTF-8; never treat UTF-8 bytes as wchar_t or apply byte-wise tolower.
 */

#include <Core/Misc/Export.h>

#include <filesystem>
#include <string>

namespace Maho
{

[[nodiscard]] MAHO_API std::wstring Utf8ToWide(const std::string& Text);
[[nodiscard]] MAHO_API std::string WideToUtf8(const std::wstring& Text);

/** Build filesystem::path from a UTF-8 path string (Win32: via UTF-16). */
[[nodiscard]] MAHO_API std::filesystem::path PathFromUtf8(const std::string& Utf8Path);

/** Serialize filesystem::path to UTF-8 (Win32: from native wide). */
[[nodiscard]] MAHO_API std::string PathToUtf8(const std::filesystem::path& Path);

/** ASCII-only A–Z → a–z; leaves UTF-8 multi-byte sequences untouched. */
MAHO_API void AsciiToLowerInPlace(std::string& Text);

} // namespace Maho
