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

	bLuaInitialized = true;
	MAHO_LOG_CORE_INFO("FScriptSystem Lua initialized (Scripts='{}')", ScriptsDirectory);

	for (FTypeBinder Binder : PendingTypeBinders)
	{
		Binder(&Impl->Lua);
	}
	PendingTypeBinders.clear();

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

void FScriptSystem::RegisterTypeBinder(FTypeBinder Binder)
{
	if (!Binder)
	{
		return;
	}
	if (!bLuaInitialized || !Impl)
	{
		PendingTypeBinders.push_back(Binder);
		return;
	}
	Binder(&Impl->Lua);
}

void FScriptSystem::ShutdownLua()
{
	OnLuaReady.RemoveAll();
	PendingTypeBinders.clear();

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

void* FScriptSystem::LoadScriptRaw(const char* FilePath)
{
	if (!bLuaInitialized || !Impl || !FilePath || FilePath[0] == '\0')
	{
		return nullptr;
	}

	const std::string Resolved = ResolveScriptPath(ScriptsDirectory, FilePath);
	namespace fs = std::filesystem;
	if (!fs::is_regular_file(Resolved))
	{
		MAHO_LOG_CORE_WARN("FScriptSystem::LoadScript: file not found '{}'", Resolved);
		return nullptr;
	}

	sol::protected_function_result Result = Impl->Lua.safe_script_file(Resolved);
	if (!Result.valid())
	{
		const sol::error Error = Result;
		MAHO_LOG_CORE_ERROR("FScriptSystem::LoadScript('{}'): {}", Resolved, Error.what());
		return nullptr;
	}

	sol::object Top = Result.get<sol::object>();
	if (Top.valid())
	{
		MAHO_LOG_CORE_INFO("FScriptSystem::LoadScript loaded '{}'", Resolved);
		return new sol::object(Top);
	}

	MAHO_LOG_CORE_WARN("FScriptSystem::LoadScript('{}'): script returned no value", Resolved);
	return nullptr;
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

bool FScriptSystem::CallRaw(void* TableHandle, const char* FunctionName)
{
	if (!bLuaInitialized || !Impl || !TableHandle || !FunctionName || FunctionName[0] == '\0')
	{
		return false;
	}

	sol::table& Table = *static_cast<sol::table*>(TableHandle);
	sol::object Object = Table[FunctionName];
	if (!Object.is<sol::function>())
	{
		return false;
	}

	sol::protected_function Function = Object.as<sol::function>();
	sol::protected_function_result Result = Function();
	if (!Result.valid())
	{
		const sol::error Error = Result;
		MAHO_LOG_CORE_ERROR("FScriptSystem::Call(table, '{}'): {}", FunctionName, Error.what());
		return false;
	}
	return true;
}

bool FScriptSystem::CallRaw(void* TableHandle, const char* FunctionName, float Arg0)
{
	if (!bLuaInitialized || !Impl || !TableHandle || !FunctionName || FunctionName[0] == '\0')
	{
		return false;
	}

	sol::table& Table = *static_cast<sol::table*>(TableHandle);
	sol::object Object = Table[FunctionName];
	if (!Object.is<sol::function>())
	{
		return false;
	}

	sol::protected_function Function = Object.as<sol::function>();
	sol::protected_function_result Result = Function(Arg0);
	if (!Result.valid())
	{
		const sol::error Error = Result;
		MAHO_LOG_CORE_ERROR("FScriptSystem::Call(table, '{}', float): {}", FunctionName, Error.what());
		return false;
	}
	return true;
}

} // namespace Maho::Script
