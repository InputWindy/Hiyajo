#include <Core/Extension/Script/ScriptSystem.h>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <Core/EngineBase.h>
#include <Core/Misc/Console.h>
#include <Core/Misc/Log.h>

#include <filesystem>

namespace Maho
{

namespace
{

void RegisterCoreBindings(sol::state& Lua)
{
	sol::table MahoTable = Lua.create_named_table("maho");

	MahoTable["log"] = [](const std::string& Message)
	{
		MAHO_INFO("[Lua] {}", Message);
	};
	MahoTable["log_warn"] = [](const std::string& Message)
	{
		MAHO_WARN("[Lua] {}", Message);
	};
	MahoTable["log_error"] = [](const std::string& Message)
	{
		MAHO_ERROR("[Lua] {}", Message);
	};

	MahoTable["get_cvar_int"] = [](const std::string& Name, sol::optional<int> DefaultValue) -> int
	{
		return FConsole::Get().GetInt(Name.c_str(), DefaultValue.value_or(0));
	};
	MahoTable["get_cvar_float"] = [](const std::string& Name, sol::optional<float> DefaultValue) -> float
	{
		return FConsole::Get().GetFloat(Name.c_str(), DefaultValue.value_or(0.0f));
	};
	MahoTable["get_cvar_bool"] = [](const std::string& Name, sol::optional<bool> DefaultValue) -> bool
	{
		return FConsole::Get().GetBool(Name.c_str(), DefaultValue.value_or(false));
	};
	MahoTable["get_cvar_string"] = [](const std::string& Name, sol::optional<std::string> DefaultValue) -> std::string
	{
		const char* Default = DefaultValue ? DefaultValue->c_str() : "";
		return FConsole::Get().GetString(Name.c_str(), Default);
	};

	MahoTable["set_cvar_int"] = [](const std::string& Name, int Value) -> bool
	{
		return FConsole::Get().SetInt(Name.c_str(), Value);
	};
	MahoTable["set_cvar_float"] = [](const std::string& Name, float Value) -> bool
	{
		return FConsole::Get().SetFloat(Name.c_str(), Value);
	};
	MahoTable["set_cvar_bool"] = [](const std::string& Name, bool Value) -> bool
	{
		return FConsole::Get().SetBool(Name.c_str(), Value);
	};
	MahoTable["set_cvar_string"] = [](const std::string& Name, const std::string& Value) -> bool
	{
		return FConsole::Get().SetString(Name.c_str(), Value.c_str());
	};
}

[[nodiscard]] std::string ResolveScriptPath(const std::string& ScriptsDirectory, const std::string& FilePath)
{
	namespace fs = std::filesystem;
	const fs::path Path = FilePath;
	if (Path.is_absolute())
	{
		return Path.string();
	}
	return (fs::path(ScriptsDirectory) / Path).string();
}

} // namespace

struct FScriptSystem::FImpl
{
	sol::state Lua;
};

FScriptSystem::FScriptSystem() = default;

FScriptSystem::~FScriptSystem()
{
	ShutdownLua();
}

bool FScriptSystem::ExecuteStage(EEngineStage Stage)
{
	switch (Stage)
	{
	case EEngineStage::Init:
	{
		if (!GEngine)
		{
			MAHO_CORE_ERROR("FScriptSystem: GEngine missing at Init");
			return false;
		}
		const std::string& ScriptsDir = GEngine->GetConfig().ProjectScriptsDir;
		if (!InitializeLua(ScriptsDir.empty() ? "Scripts" : ScriptsDir))
		{
			MAHO_CORE_ERROR("FScriptSystem: InitializeLua failed");
			return false;
		}
		return true;
	}
	case EEngineStage::Attach:
	{
		// Global bootstrap script: Scripts/main.lua defines optional OnUpdate / OnFixedUpdate globals.
		namespace fs = std::filesystem;
		const fs::path MainScript = fs::path(ScriptsDirectory) / "main.lua";
		if (fs::is_regular_file(MainScript))
		{
			(void)DoFile("main.lua");
		}
		else
		{
			MAHO_CORE_INFO("FScriptSystem: no '{}' (skip)", MainScript.string());
		}
		return true;
	}
	case EEngineStage::Tick:
		if (GEngine)
		{
			for (int Step = 0; Step < GEngine->GetFixedStepsRemaining(); ++Step)
			{
				(void)Call("OnFixedUpdate", GEngine->GetFixedDeltaSeconds());
			}
			(void)Call("OnUpdate", GEngine->GetDeltaSeconds());
		}
		return true;
	case EEngineStage::Shutdown:
		ShutdownLua();
		return true;
	default:
		return true;
	}
}

bool FScriptSystem::InitializeLua(const std::string& InScriptsDirectory)
{
	if (bLuaInitialized)
	{
		return true;
	}

	ScriptsDirectory = InScriptsDirectory.empty() ? "Scripts" : InScriptsDirectory;
	Impl = std::make_unique<FImpl>();

	Impl->Lua.open_libraries(
		sol::lib::base,
		sol::lib::package,
		sol::lib::coroutine,
		sol::lib::string,
		sol::lib::table,
		sol::lib::math,
		sol::lib::utf8);

	namespace fs = std::filesystem;
	std::error_code ErrorCode;
	fs::create_directories(ScriptsDirectory, ErrorCode);

	const std::string Pattern = (fs::path(ScriptsDirectory) / "?.lua").string();
	const std::string PatternInit = (fs::path(ScriptsDirectory) / "?" / "init.lua").string();
	const std::string PackagePath = Pattern + ";" + PatternInit;
	Impl->Lua["package"]["path"] = PackagePath;

	RegisterCoreBindings(Impl->Lua);

	bLuaInitialized = true;
	MAHO_CORE_INFO("FScriptSystem Lua initialized (Scripts='{}')", ScriptsDirectory);

	for (ILuaBindable* Bindable : PendingBindables)
	{
		if (Bindable)
		{
			Bindable->BindLua(*this);
		}
	}
	PendingBindables.clear();

	OnLuaReady.Broadcast(*this);
	return true;
}

void* FScriptSystem::TryGetLuaState()
{
	if (!bLuaInitialized || !Impl)
	{
		return nullptr;
	}
	return &Impl->Lua;
}

void FScriptSystem::Bind(ILuaBindable& Bindable)
{
	if (!bLuaInitialized || !Impl)
	{
		PendingBindables.push_back(&Bindable);
		return;
	}

	Bindable.BindLua(*this);
}

void FScriptSystem::ShutdownLua()
{
	OnLuaReady.Clear();
	PendingBindables.clear();

	if (!bLuaInitialized)
	{
		Impl.reset();
		return;
	}

	Impl.reset();
	bLuaInitialized = false;
	MAHO_CORE_INFO("FScriptSystem Lua shut down");
}

bool FScriptSystem::DoFile(const std::string& FilePath)
{
	if (!bLuaInitialized || !Impl)
	{
		return false;
	}

	const std::string Resolved = ResolveScriptPath(ScriptsDirectory, FilePath);
	namespace fs = std::filesystem;
	if (!fs::is_regular_file(Resolved))
	{
		MAHO_CORE_WARN("FScriptSystem::DoFile: file not found '{}'", Resolved);
		return false;
	}

	sol::protected_function_result Result = Impl->Lua.safe_script_file(Resolved);
	if (!Result.valid())
	{
		const sol::error Error = Result;
		MAHO_CORE_ERROR("FScriptSystem::DoFile('{}'): {}", Resolved, Error.what());
		return false;
	}

	MAHO_CORE_INFO("FScriptSystem loaded '{}'", Resolved);
	return true;
}

bool FScriptSystem::HasFunction(const char* FunctionName)
{
	if (!bLuaInitialized || !Impl || !FunctionName || FunctionName[0] == '\0')
	{
		return false;
	}

	sol::object Object = Impl->Lua[FunctionName];
	return Object.is<sol::function>();
}

bool FScriptSystem::Call(const char* FunctionName)
{
	if (!HasFunction(FunctionName))
	{
		return false;
	}

	sol::protected_function Function = Impl->Lua[FunctionName];
	sol::protected_function_result Result = Function();
	if (!Result.valid())
	{
		const sol::error Error = Result;
		MAHO_CORE_ERROR("FScriptSystem::Call('{}'): {}", FunctionName, Error.what());
		return false;
	}
	return true;
}

bool FScriptSystem::Call(const char* FunctionName, float Arg0)
{
	if (!HasFunction(FunctionName))
	{
		return false;
	}

	sol::protected_function Function = Impl->Lua[FunctionName];
	sol::protected_function_result Result = Function(Arg0);
	if (!Result.valid())
	{
		const sol::error Error = Result;
		MAHO_CORE_ERROR("FScriptSystem::Call('{}', float): {}", FunctionName, Error.what());
		return false;
	}
	return true;
}

} // namespace Maho
