// Compile+run for the engine Common singleton libraries still in the engine
// core: Unicode (zero-third-party), Config, Archive, ConsoleVariable,
// Exception, Timer, Text, CommandParser, Compress.
// (Name/Paths/Asset/Resource migrated to plugins.)
#include <Engine/Common/Unicode.h>
#include <Engine/Common/Config.h>
#include <Engine/Common/Archive.h>
#include <Engine/Common/ConsoleVariable.h>
#include <Engine/Common/Exception.h>
#include <Engine/Common/Timer.h>
#include <Engine/Common/Text.h>
#include <Engine/Common/CommandParser.h>
#include <Engine/Common/Compress.h>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <thread>

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

	// Compress (zstd pure library)
	{
		std::vector<std::uint8_t> Raw;
		for (int I = 0; I < 2000; ++I)
		{
			Raw.push_back(static_cast<std::uint8_t>(I % 8));   // compressible
		}
		auto Packed = Compress::Compress(Raw, 5);
		if (!Packed || Packed->size() >= Raw.size())
		{
			std::puts("[FAIL] Compress compressed"); return 1;
		}
		auto RoundTrip = Compress::Decompress(*Packed);
		if (!RoundTrip || *RoundTrip != Raw)
		{
			std::puts("[FAIL] Compress roundtrip"); return 1;
		}
	}

	std::puts("ok: engine Common libs (Unicode/Config/Archive/CVar/Exception/Timer/Text/CommandParser/Compress)");
	return 0;
}
