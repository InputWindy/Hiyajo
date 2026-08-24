// Compile+run for the engine Common singletons/libraries (migrated from
// plugins): Unicode (zero-third-party), Name/Paths/Config singletons.
#include <Engine/Common/Unicode.h>
#include <Engine/Common/Name.h>
#include <Engine/Common/Paths.h>
#include <Engine/Common/Config.h>

#include <cstdio>

using namespace Maho;

int main()
{
	// Unicode (zero third-party) — "中文" as explicit UTF-8 bytes
	std::string Utf8("\xE4\xB8\xAD\xE6\x96\x87");
	if (!Unicode::IsValidUtf8(Utf8))
	{
		std::puts("[FAIL] IsValidUtf8 rejected valid text"); return 1;
	}
	std::u16string U16 = Unicode::Utf8ToUtf16(Utf8);
	std::string Back = Unicode::Utf16ToUtf8(U16);
	if (Back != Utf8)
	{
		std::puts("[FAIL] UTF-8↔UTF-16 roundtrip"); return 1;
	}
	Unicode::EnsureConsoleUtf8();

	// Name (intern pool singleton)
	Name::FNamePool::Get().Initiate(0, nullptr);
	const Name::FName A = Name::FName("head");
	const Name::FName B = Name::FName("head");
	if (!(A == B) || A.ToString() != "head")
	{
		std::puts("[FAIL] FName intern"); return 1;
	}
	Name::FNamePool::Get().Shutdown();

	// Paths (root alias singleton)
	Paths::FPaths::Get().Initiate(0, nullptr);
	Paths::FPaths::Get().SetRoot("Engine", "./EngineRoot");
	auto Resolved = Paths::FPaths::Get().Resolve("Engine/Sub/File.txt");
	if (Resolved.generic_string() != "./EngineRoot/Sub/File.txt")
	{
		std::printf("[FAIL] Paths::Resolve got %s\n", Resolved.generic_string().c_str()); return 1;
	}
	Paths::FPaths::Get().Shutdown();

	// Config (INI singleton)
	Config::FConfig::Get().Initiate(0, nullptr);
	Config::FConfig::Get().SetString("/Script/Engine", "GameName", "MyGame");
	if (Config::FConfig::Get().GetString("/Script/Engine", "GameName") != std::optional<std::string>("MyGame"))
	{
		std::puts("[FAIL] Config get"); return 1;
	}
	Config::FConfig::Get().Shutdown();

	std::puts("ok: engine Common Unicode/Name/Paths/Config");
	return 0;
}
