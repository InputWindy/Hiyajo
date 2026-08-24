// Compile+run for the engine Common singletons/libraries (migrated from
// plugins): Unicode (zero-third-party), Name/Paths/Config, Archive,
// ConsoleVariable, Exception, Timer.
#include <Engine/Common/Unicode.h>
#include <Engine/Common/Name.h>
#include <Engine/Common/Paths.h>
#include <Engine/Common/Config.h>
#include <Engine/Common/Archive.h>
#include <Engine/Common/ConsoleVariable.h>
#include <Engine/Common/Exception.h>
#include <Engine/Common/Timer.h>
#include <Engine/Common/Text.h>
#include <Engine/Common/Asset.h>
#include <Engine/Common/CommandParser.h>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

using namespace Maho;

int main()
{
	// Engine third-party headers usable directly (Maho lib PUBLIC links the targets)
	const glm::vec3 HL = glm::mix(glm::vec3(0.f), glm::vec3(2.f), 0.5f);
	if (HL != glm::vec3(1.f))
	{
		std::puts("[FAIL] glm::mix vec3"); return 1;
	}
	nlohmann::json J = nlohmann::json::parse("{\"a\":[1,2,3]}");
	if (J["a"][2] != 3)
	{
		std::puts("[FAIL] nlohmann::json parse"); return 1;
	}

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

	// Archive (serialization streams)
	Archive::FMemoryWriter Ar;
	int WInt = 42; double WReal = 3.5; std::string WStr = "hi"; bool WYes = true;
	Ar << WInt << WReal << WStr << WYes;
	std::vector<std::uint8_t> Bytes = Ar.TakeBytes();
	Archive::FMemoryReader Rd(Bytes);
	int RInt = 0; double RReal = 0.0; std::string RStr; bool RYes = false;
	Rd << RInt << RReal << RStr << RYes;
	if (RInt != WInt || RReal != WReal || RStr != WStr || RYes != WYes)
	{
		std::puts("[FAIL] Archive roundtrip"); return 1;
	}

	// ConsoleVariable (CVar registry + static TAutoConsoleVariable)
	ConsoleVariable::FConsoleVariable::Get().Initiate(0, nullptr);
	static ConsoleVariable::TAutoConsoleVariable<int> CVarMaxFPS("r.MaxFPS", 60, "Max FPS");
	ConsoleVariable::TAutoConsoleVariable<std::string> CVarLabel("r.Label", "default", "Label");
	if (CVarMaxFPS.GetValue() != 60 || CVarLabel.GetValue() != "default")
	{
		std::puts("[FAIL] CVar default"); return 1;
	}
	CVarMaxFPS.Set(120);
	if (CVarMaxFPS.GetValue() != 120)
	{
		std::puts("[FAIL] CVar set"); return 1;
	}
	ConsoleVariable::FConsoleVariable::Get().Shutdown();

	// Exception (non-fatal broadcast)
	Exception::FException::Get().Initiate(0, nullptr);
	std::string Caught;
	Exception::FException::Get().OnException.Bind([&](const std::string& M) { Caught = M; });
	Exception::FException::Get().ReportException("boom");
	if (Caught != "boom")
	{
		std::puts("[FAIL] Exception broadcast"); return 1;
	}
	Exception::FException::Get().Shutdown();

	// Timer (scope profiler + game clock)
	Timer::FTimer::Get().Initiate(0, nullptr);
	{
		Timer::FScopedTimer Scope("frame");
	}
	std::string Dump = Timer::FTimer::Get().DumpToString();
	if (Dump.find("frame") == std::string::npos)
	{
		std::puts("[FAIL] Timer dump"); return 1;
	}
	Timer::FTimer::Get().Shutdown();
	Timer::FGameClock::Get().Initiate(0, nullptr);
	Timer::FGameClock::Get().SetTimeScale(0.5);
	if (Timer::FGameClock::Get().GetTimeScale() != 0.5)
	{
		std::puts("[FAIL] GameClock scale"); return 1;
	}
	Timer::FGameClock::Get().Shutdown();

	// Text (localized catalog)
	Text::FTextManager::Get().Initiate(0, nullptr);
	Text::FTextManager::Get().AddTranslation("MainMenu", "Title", Text::Culture::Chinese, "\xE4\xB8\xBB\xE8\x8F\x9C\xE5\x8D\x95"); // "主菜单"
	Text::FTextManager::Get().SetCulture(std::string(Text::Culture::Chinese));
	const Text::FText Title = Text::FText("MainMenu", "Title", "Main Menu");
	if (Title.Resolve() != "\xE4\xB8\xBB\xE8\x8F\x9C\xE5\x8D\x95")
	{
		std::puts("[FAIL] Text resolve zh"); return 1;
	}
	Text::FTextManager::Get().Shutdown();

	// Asset (registry over a temp content dir)
	Asset::FAssetRegistry::Get().Initiate(0, nullptr);
	const std::filesystem::path Tmp = std::filesystem::temp_directory_path() / "maho_asset_stress";
	std::error_code EC;
	std::filesystem::create_directories(Tmp / "Materials", EC);
	(EC.clear());
	{
		std::ofstream Ofs(Tmp / "Materials" / "M_Metal.material");
		Ofs << "metal";
	}
	Asset::FAssetRegistry::Get().Scan(Tmp, "Game");
	if (Asset::FAssetRegistry::Get().GetAssetCount() != 1)
	{
		std::puts("[FAIL] Asset scan count"); return 1;
	}
	const Asset::FAssetData* Data = Asset::FAssetRegistry::Get().Find(Asset::FAssetPath("/Game/Materials/M_Metal"));
	if (!Data || Data->Type != Asset::EAssetType::Material)
	{
		std::puts("[FAIL] Asset find"); return 1;
	}
	auto AssetBytes = Asset::FAssetRegistry::Get().Load(Asset::FAssetPath("/Game/Materials/M_Metal"));
	if (!AssetBytes || AssetBytes->size() != 5)
	{
		std::puts("[FAIL] Asset load"); return 1;
	}
	Asset::FAssetRegistry::Get().Shutdown();
	std::filesystem::remove_all(Tmp, EC);

	// CommandParser (CLI11-backed KV store)
	{
		char Arg0[] = "app";
		char Arg1[] = "-width=800";
		char Arg2[] = "-height";
		char Arg3[] = "600";
		char Arg4[] = "-fullscreen";
		char* TestArgs[] = { Arg0, Arg1, Arg2, Arg3, Arg4 };
		CommandParser::FCommandParser& Parser = CommandParser::FCommandParser::Get();
		Parser.Clear();
		Parser.Initiate(5, TestArgs);
		if (Parser.GetInt("width") != 800 || Parser.Get("height") != "600" || !Parser.GetBool("fullscreen"))
		{
			std::printf("[FAIL] CommandParser KV: width=%s height=%s fullscreen=%s\n",
				Parser.Get("width").c_str(), Parser.Get("height").c_str(),
				Parser.GetBool("fullscreen") ? "true" : "false");
			return 1;
		}
		Parser.Shutdown();
	}

	std::puts("ok: engine Common full set + glm/nlohmann/CLI11");
	return 0;
}
