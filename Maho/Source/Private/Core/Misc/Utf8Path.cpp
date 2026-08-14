#include <Core/Misc/Utf8Path.h>

#if defined(_WIN32)
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <Windows.h>
#endif

namespace Maho
{

std::wstring Utf8ToWide(const std::string& Text)
{
#if defined(_WIN32)
	if (Text.empty())
	{
		return {};
	}
	const int Size = MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), -1, nullptr, 0);
	if (Size <= 1)
	{
		return {};
	}
	std::wstring Out(static_cast<std::size_t>(Size - 1), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), -1, Out.data(), Size);
	return Out;
#else
	// Best-effort: assume locale can round-trip (non-Win builds rare for this project).
	return std::wstring(Text.begin(), Text.end());
#endif
}

std::string WideToUtf8(const std::wstring& Text)
{
#if defined(_WIN32)
	if (Text.empty())
	{
		return {};
	}
	const int Size = WideCharToMultiByte(CP_UTF8, 0, Text.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (Size <= 1)
	{
		return {};
	}
	std::string Out(static_cast<std::size_t>(Size - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, Text.c_str(), -1, Out.data(), Size, nullptr, nullptr);
	return Out;
#else
	return std::string(Text.begin(), Text.end());
#endif
}

std::filesystem::path PathFromUtf8(const std::string& Utf8Path)
{
#if defined(_WIN32)
	return std::filesystem::path(Utf8ToWide(Utf8Path));
#else
	return std::filesystem::path(Utf8Path);
#endif
}

std::string PathToUtf8(const std::filesystem::path& Path)
{
#if defined(_WIN32)
	return WideToUtf8(Path.native());
#else
	return Path.string();
#endif
}

void AsciiToLowerInPlace(std::string& Text)
{
	for (char& Ch : Text)
	{
		if (Ch >= 'A' && Ch <= 'Z')
		{
			Ch = static_cast<char>(Ch - 'A' + 'a');
		}
	}
}

} // namespace Maho
