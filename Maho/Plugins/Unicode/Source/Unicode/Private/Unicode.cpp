#include <Unicode.h>

#include <utf8.h>

#if defined(_WIN32)
#	include <Windows.h>
#endif

namespace Maho::Unicode
{

bool IsValidUtf8(std::string_view In)
{
	return utf8::is_valid(In.begin(), In.end());
}

std::u16string Utf8ToUtf16(std::string_view In)
{
	std::u16string Out;
	utf8::utf8to16(In.begin(), In.end(), std::back_inserter(Out));
	return Out;
}

std::string Utf16ToUtf8(std::u16string_view In)
{
	std::string Out;
	utf8::utf16to8(In.begin(), In.end(), std::back_inserter(Out));
	return Out;
}

std::u32string Utf8ToUtf32(std::string_view In)
{
	std::u32string Out;
	utf8::utf8to32(In.begin(), In.end(), std::back_inserter(Out));
	return Out;
}

std::string Utf32ToUtf8(std::u32string_view In)
{
	std::string Out;
	utf8::utf32to8(In.begin(), In.end(), std::back_inserter(Out));
	return Out;
}

FNativeString ToNative(std::string_view Utf8)
{
#if defined(_WIN32)
	std::u16string Utf16 = Utf8ToUtf16(Utf8);
	return FNativeString(Utf16.begin(), Utf16.end());
#else
	return FNativeString(Utf8);
#endif
}

std::string FromNative(FNativeStringView Native)
{
#if defined(_WIN32)
	std::u16string Utf16(Native.begin(), Native.end());
	return Utf16ToUtf8(Utf16);
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

bool FUnicode::ExecuteStage(EToolStage Stage)
{
	// Pure function library — no state.
	(void)Stage;
	return true;
}

} // namespace Maho::Unicode

// ── Dynamic plugin entry (runtime load/unload via FPluginManager) ──

namespace
{

class FUnicodeAdapter final : public Maho::IExtension<Maho::EToolStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EToolStage Stage) override
	{
		return Maho::Unicode::FUnicode::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_UNICODE_API Maho::IExtension<Maho::EToolStage>* CreateExtension()
{
	return new FUnicodeAdapter();
}
