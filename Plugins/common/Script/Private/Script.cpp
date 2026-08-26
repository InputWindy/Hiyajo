#include "Script.h"

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <ConsoleVariable.h>
#include <Log.h>

#include <filesystem>

namespace Maho::Script
{

namespace
{
	void RegisterCoreBindings(sol::state& Lua)
	{
		sol::table MahoTable = Lua.create_named_table("maho");

		MahoTable["log"] = [](const std::string& Message)
		{
			MAHO_LOG_CORE_INFO("[Lua] {}", Message);
		};
		MahoTable["log_warn"] = [](const std::string& Message)
		{
			MAHO_LOG_CORE_WARN("[Lua] {}", Message);
		};
		MahoTable["log_error"] = [](const std::string& Message)
		{
			MAHO_LOG_CORE_ERROR("[Lua] {}", Message);
		};

		MahoTable["get_cvar_int"] = [](const std::string& Name, sol::optional<int> DefaultValue) -> int
		{
			ConsoleVariable::IConsoleVariable* CVar = ConsoleVariable::FConsoleVariable::Get().Find(Name);
			return CVar ? CVar->GetInt() : DefaultValue.value_or(0);
		};
		MahoTable["get_cvar_float"] = [](const std::string& Name, sol::optional<float> DefaultValue) -> float
		{
			ConsoleVariable::IConsoleVariable* CVar = ConsoleVariable::FConsoleVariable::Get().Find(Name);
			return CVar ? CVar->GetFloat() : DefaultValue.value_or(0.0f);
		};
		MahoTable["get_cvar_bool"] = [](const std::string& Name, sol::optional<bool> DefaultValue) -> bool
		{
			ConsoleVariable::IConsoleVariable* CVar = ConsoleVariable::FConsoleVariable::Get().Find(Name);
			return CVar ? CVar->GetBool() : DefaultValue.value_or(false);
		};
		MahoTable["get_cvar_string"] = [](const std::string& Name, sol::optional<std::string> DefaultValue) -> std::string
		{
			ConsoleVariable::IConsoleVariable* CVar = ConsoleVariable::FConsoleVariable::Get().Find(Name);
			return CVar ? CVar->GetString() : DefaultValue.value_or("");
		};

		MahoTable["set_cvar_int"] = [](const std::string& Name, int Value) -> bool
		{
			ConsoleVariable::IConsoleVariable* CVar = ConsoleVariable::FConsoleVariable::Get().Find(Name);
			if (CVar)
			{
				CVar->Set(std::to_string(Value));
				return true;
			}
			return false;
		};
		MahoTable["set_cvar_float"] = [](const std::string& Name, float Value) -> bool
		{
			ConsoleVariable::IConsoleVariable* CVar = ConsoleVariable::FConsoleVariable::Get().Find(Name);
			if (CVar)
			{
				CVar->Set(std::to_string(Value));
				return true;
			}
			return false;
		};
		MahoTable["set_cvar_bool"] = [](const std::string& Name, bool Value) -> bool
		{
			ConsoleVariable::IConsoleVariable* CVar = ConsoleVariable::FConsoleVariable::Get().Find(Name);
			if (CVar)
			{
				CVar->Set(Value ? "true" : "false");
				return true;
			}
			return false;
		};
		MahoTable["set_cvar_string"] = [](const std::string& Name, const std::string& Value) -> bool
		{
			ConsoleVariable::IConsoleVariable* CVar = ConsoleVariable::FConsoleVariable::Get().Find(Name);
			if (CVar)
			{
				CVar->Set(Value);
				return true;
			}
			return false;
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
}

struct FScriptSystem::FImpl
{
	sol::state Lua;
};

FScriptSystem& FScriptSystem::Get()
{
	static FScriptSystem Instance;
	return Instance;
}

void FScriptSystem::Initialize(int Argc, char** Argv)
{
	(void)Argc; (void)Argv;
		// Scripts directory: fixed convention "Scripts" (project cwd). A launch
		// override could read --scripts-dir here in the future.
		(void)InitializeLua("Scripts");
	}

void FScriptSystem::Shutdown()
{
	ShutdownLua();
}

void FScriptSystem::Tick(float DeltaSeconds)
{
	(void)Call("OnUpdate", DeltaSeconds);
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
	MAHO_LOG_CORE_INFO("FScriptSystem Lua initialized (Scripts='{}')", ScriptsDirectory);

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
	OnLuaReady.RemoveAll();
	PendingBindables.clear();

	Impl.reset();
	bLuaInitialized = false;
	MAHO_LOG_CORE_INFO("FScriptSystem Lua shut down");
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
		MAHO_LOG_CORE_WARN("FScriptSystem::DoFile: file not found '{}'", Resolved);
		return false;
	}

	sol::protected_function_result Result = Impl->Lua.safe_script_file(Resolved);
	if (!Result.valid())
	{
		const sol::error Error = Result;
		MAHO_LOG_CORE_ERROR("FScriptSystem::DoFile('{}'): {}", Resolved, Error.what());
		return false;
	}

	MAHO_LOG_CORE_INFO("FScriptSystem loaded '{}'", Resolved);
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
		MAHO_LOG_CORE_ERROR("FScriptSystem::Call('{}'): {}", FunctionName, Error.what());
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
		MAHO_LOG_CORE_ERROR("FScriptSystem::Call('{}', float): {}", FunctionName, Error.what());
		return false;
	}
	return true;
}

} // namespace Maho::Script
