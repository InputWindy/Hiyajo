#include <Engine/Common/Unicode.h>

#include <cstdint>

#if defined(_WIN32)
#	include <Windows.h>
#endif

namespace Maho::Unicode
{

bool IsValidUtf8(std::string_view In)
{
	std::size_t I = 0;
	const std::size_t N = In.size();
	while (I < N)
	{
		const unsigned char C = static_cast<unsigned char>(In[I]);
		if (C < 0x80)
		{
			++I;
			continue;
		}
		int Extra = 0;
		unsigned CodepointMin = 0;
		unsigned Mask = 0;
		if ((C & 0xE0) == 0xC0) { Extra = 1; CodepointMin = 0x80; Mask = 0x1F; }
		else if ((C & 0xF0) == 0xE0) { Extra = 2; CodepointMin = 0x800; Mask = 0x0F; }
		else if ((C & 0xF8) == 0xF0) { Extra = 3; CodepointMin = 0x10000; Mask = 0x07; }
		else { return false; }

		if (I + Extra >= N)
		{
			return false; // truncated
		}
		unsigned CP = C & Mask;
		for (int K = 1; K <= Extra; ++K)
		{
			const unsigned char CC = static_cast<unsigned char>(In[I + K]);
			if ((CC & 0xC0) != 0x80)
			{
				return false; // bad continuation byte
			}
			CP = (CP << 6) | (CC & 0x3F);
		}
		if (CP < CodepointMin || CP > 0x10FFFF || (CP >= 0xD800 && CP <= 0xDFFF))
		{
			return false; // overlong / out of range / surrogate
		}
		I += Extra + 1;
	}
	return true;
}

#if defined(_WIN32)

std::u16string Utf8ToUtf16(std::string_view In)
{
	if (In.empty())
	{
		return {};
	}
	const int Size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
		In.data(), static_cast<int>(In.size()), nullptr, 0);
	if (Size <= 0)
	{
		return {};
	}
	std::u16string Out(Size, u'\0');
	MultiByteToWideChar(CP_UTF8, 0, In.data(), static_cast<int>(In.size()),
		reinterpret_cast<wchar_t*>(Out.data()), Size);
	return Out;
}

std::string Utf16ToUtf8(std::u16string_view In)
{
	if (In.empty())
	{
		return {};
	}
	const int Size = WideCharToMultiByte(CP_UTF8, 0,
		reinterpret_cast<const wchar_t*>(In.data()), static_cast<int>(In.size()),
		nullptr, 0, nullptr, nullptr);
	if (Size <= 0)
	{
		return {};
	}
	std::string Out(Size, '\0');
	WideCharToMultiByte(CP_UTF8, 0,
		reinterpret_cast<const wchar_t*>(In.data()), static_cast<int>(In.size()),
		Out.data(), Size, nullptr, nullptr);
	return Out;
}

std::u32string Utf8ToUtf32(std::string_view In)
{
	const std::u16string U16 = Utf8ToUtf16(In);
	std::u32string Out;
	Out.reserve(U16.size());
	for (std::size_t I = 0; I < U16.size(); ++I)
	{
		std::uint16_t Unit = static_cast<std::uint16_t>(U16[I]);
		if (Unit >= 0xD800 && Unit < 0xDC00 && I + 1 < U16.size())
		{
			const std::uint16_t Lo = static_cast<std::uint16_t>(U16[I + 1]);
			const unsigned CP = 0x10000 + ((Unit - 0xD800) << 10) + (Lo - 0xDC00);
			Out.push_back(static_cast<char32_t>(CP));
			++I;
		}
		else if (Unit >= 0xD800 && Unit < 0xE000)
		{
			return {}; // lone surrogate
		}
		else
		{
			Out.push_back(static_cast<char32_t>(Unit));
		}
	}
	return Out;
}

std::string Utf32ToUtf8(std::u32string_view In)
{
	// surrogate pair encode
	std::u16string U16;
	U16.reserve(In.size() * 2);
	for (char32_t CP : In)
	{
		if (CP < 0x10000)
		{
			U16.push_back(static_cast<char16_t>(CP));
		}
		else
		{
			CP -= 0x10000;
			U16.push_back(static_cast<char16_t>(0xD800 + (CP >> 10)));
			U16.push_back(static_cast<char16_t>(0xDC00 + (CP & 0x3FF)));
		}
	}
	return Utf16ToUtf8(U16);
}

#else // non-Windows: UTF-8 passthrough / minimal

std::u16string Utf8ToUtf16(std::string_view In)
{
	std::u16string Out;
	// treat input as raw bytes → widen (best effort; Windows is the real target)
	Out.assign(In.begin(), In.end());
	return Out;
}
std::string Utf16ToUtf8(std::u16string_view In)
{
	return std::string(In.begin(), In.end());
}
std::u32string Utf8ToUtf32(std::string_view In)
{
	std::u32string Out;
	Out.assign(In.begin(), In.end());
	return Out;
}
std::string Utf32ToUtf8(std::u32string_view In)
{
	return std::string(In.begin(), In.end());
}

#endif // _WIN32

FNativeString ToNative(std::string_view Utf8)
{
#if defined(_WIN32)
	const std::u16string U16 = Utf8ToUtf16(Utf8);
	return FNativeString(U16.begin(), U16.end());
#else
	return FNativeString(Utf8);
#endif
}

std::string FromNative(FNativeStringView Native)
{
#if defined(_WIN32)
	const std::u16string U16(Native.begin(), Native.end());
	return Utf16ToUtf8(U16);
#else
	return std::string(Native);
#endif
}

void EnsureConsoleUtf8()
{
#if defined(_WIN32)
	SetConsoleOutputCP(CP_UTF8);
#endif
}

} // namespace Maho::Unicode
