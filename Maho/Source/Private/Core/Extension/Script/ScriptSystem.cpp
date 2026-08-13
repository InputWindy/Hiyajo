#include <Core/Extension/Script/ScriptSystem.h>
#include "Core/Extension/Script/LuaComponentBindings.h"
#include <Core/Extension/World/Components/TransformComponent.h>

#include <Core/Application/App.h>
#include <Core/System/Console.h>
#include <Core/System/Log.h>

#include <filesystem>
#include <unordered_map>
#include <utility>

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

[[nodiscard]] std::uint32_t PackEntityKey(FEntityHandle Handle)
{
	return (static_cast<std::uint32_t>(Handle.Index) << 8) | static_cast<std::uint32_t>(Handle.Generation);
}

} // namespace

struct FScriptSystem::FImpl
{
	sol::state Lua;
	std::unordered_map<std::string, sol::table> ScriptPrototypes;
	std::unordered_map<std::uint32_t, sol::table> EntityInstances;
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
		if (!GApp)
		{
			MAHO_CORE_ERROR("FScriptSystem: GApp missing at Init");
			return false;
		}
		const std::string& ScriptsDir = GApp->GetConfig().ProjectScriptsDir;
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
	case EEngineStage::Update:
		if (GApp)
		{
			(void)Call("OnUpdate", GApp->GetDeltaSeconds());
		}
		return true;
	case EEngineStage::FixedUpdate:
		if (GApp)
		{
			(void)Call("OnFixedUpdate", GApp->GetFixedDeltaSeconds());
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
	RegisterLuaComponentBindings(Impl->Lua);

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

void FScriptSystem::DispatchEntityScript(FEntityHandle Handle, const char* ScriptPath, FTransformComponent* Transform, float DeltaTime, const char* HookName)
{
	if (!bLuaInitialized || !Impl || !ScriptPath || ScriptPath[0] == '\0' || !HookName || HookName[0] == '\0')
	{
		return;
	}

	const std::string Resolved = ResolveScriptPath(ScriptsDirectory, ScriptPath);

	// 1. Load / cache script prototype (script returns a table of hooks).
	sol::table Prototype;
	auto ProtoIt = Impl->ScriptPrototypes.find(Resolved);
	if (ProtoIt == Impl->ScriptPrototypes.end())
	{
		sol::protected_function_result Result = Impl->Lua.safe_script_file(Resolved);
		if (!Result.valid())
		{
			const sol::error Error = Result;
			MAHO_CORE_ERROR("FScriptSystem::DispatchEntityScript('{}'): {}", Resolved, Error.what());
			return;
		}
		if (Result.return_count() < 1)
		{
			MAHO_CORE_ERROR("FScriptSystem::DispatchEntityScript('{}'): script must return a table", Resolved);
			return;
		}
		sol::object Returned = Result[0];
		if (!Returned.is<sol::table>())
		{
			MAHO_CORE_ERROR("FScriptSystem::DispatchEntityScript('{}'): script must return a table", Resolved);
			return;
		}
		Prototype = Returned.as<sol::table>();
		Impl->ScriptPrototypes[Resolved] = Prototype;
	}
	else
	{
		Prototype = ProtoIt->second;
	}

	// 2. Get or create per-entity instance (prototype as metatable __index).
	const std::uint32_t Key = PackEntityKey(Handle);
	sol::table Instance;
	auto InstIt = Impl->EntityInstances.find(Key);
	if (InstIt == Impl->EntityInstances.end())
	{
		Instance = Impl->Lua.create_table_with();
		sol::table Metatable = Impl->Lua.create_table_with();
		Metatable["__index"] = Prototype;
		Instance[sol::metatable_key] = Metatable;
		Impl->EntityInstances[Key] = Instance;

		sol::protected_function OnBegin = Instance["OnBegin"];
		if (OnBegin.valid())
		{
			sol::protected_function_result Result = OnBegin(Instance, DeltaTime);
			if (!Result.valid())
			{
				const sol::error Error = Result;
				MAHO_CORE_ERROR("FScriptSystem::OnBegin('{}'): {}", Resolved, Error.what());
			}
		}
	}
	else
	{
		Instance = InstIt->second;
	}

	// 3. Mount components (pointer-backed; refresh each dispatch).
	if (Transform)
	{
		Instance["Transform"] = Transform;
	}
	else
	{
		Instance["Transform"] = sol::nil;
	}

	// 4. Stage hook.
	sol::protected_function Hook = Instance[HookName];
	if (Hook.valid())
	{
		sol::protected_function_result Result = Hook(Instance, DeltaTime);
		if (!Result.valid())
		{
			const sol::error Error = Result;
			MAHO_CORE_ERROR("FScriptSystem::{}({}): {}", HookName, Resolved, Error.what());
		}
	}
}

void FScriptSystem::DestroyEntityScript(FEntityHandle Handle)
{
	if (!bLuaInitialized || !Impl)
	{
		return;
	}

	const std::uint32_t Key = PackEntityKey(Handle);
	auto It = Impl->EntityInstances.find(Key);
	if (It == Impl->EntityInstances.end())
	{
		return;
	}

	sol::table Instance = It->second;
	sol::protected_function OnDestroy = Instance["OnDestroy"];
	if (OnDestroy.valid())
	{
		sol::protected_function_result Result = OnDestroy(Instance);
		if (!Result.valid())
		{
			const sol::error Error = Result;
			MAHO_CORE_ERROR("FScriptSystem::OnDestroy: {}", Error.what());
		}
	}

	Impl->EntityInstances.erase(It);
}

} // namespace Maho
