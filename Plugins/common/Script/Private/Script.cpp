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
	[[nodiscard]] std::string ResolveScriptPath(const std::string& ScriptsDirectory, const char* FilePath)
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

// -- Lua backend ------------------------------------------------------------

class FLuaLanguage : public IScriptLanguage
{
public:
	const char* GetName() const override { return "Lua"; }

	bool Initialize(int Argc, char** Argv, const char* InScriptsDirectory) override
	{
		(void)Argc; (void)Argv;
		if (bInitialized)
		{
			return true;
		}

		ScriptsDirectory = (InScriptsDirectory && InScriptsDirectory[0]) ? InScriptsDirectory : "Scripts";
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
		Impl->Lua["package"]["path"] = Pattern + ";" + PatternInit;

		bInitialized = true;
		MAHO_LOG_CORE_INFO("Script Lua backend initialized (Scripts='{}')", ScriptsDirectory);

		for (FTypeBinder Binder : PendingTypeBinders)
		{
			Binder(&Impl->Lua);
		}
		PendingTypeBinders.clear();
		return true;
	}

	void Shutdown() override
	{
		PendingTypeBinders.clear();
		Impl.reset();
		bInitialized = false;
		MAHO_LOG_CORE_INFO("Script Lua backend shut down");
	}

	bool IsInitialized() const override { return bInitialized; }

	bool DoFile(const char* FilePath) override
	{
		if (!bInitialized || !Impl || !FilePath || FilePath[0] == '\0')
		{
			return false;
		}

		const std::string Resolved = ResolveScriptPath(ScriptsDirectory, FilePath);
		namespace fs = std::filesystem;
		if (!fs::is_regular_file(Resolved))
		{
			MAHO_LOG_CORE_WARN("Script Lua DoFile: file not found '{}'", Resolved);
			return false;
		}

		sol::protected_function_result Result = Impl->Lua.safe_script_file(Resolved);
		if (!Result.valid())
		{
			const sol::error Error = Result;
			MAHO_LOG_CORE_ERROR("Script Lua DoFile('{}'): {}", Resolved, Error.what());
			return false;
		}

		MAHO_LOG_CORE_INFO("Script Lua loaded '{}'", Resolved);
		return true;
	}

	void* LoadScriptRaw(const char* FilePath) override
	{
		if (!bInitialized || !Impl || !FilePath || FilePath[0] == '\0')
		{
			return nullptr;
		}

		const std::string Resolved = ResolveScriptPath(ScriptsDirectory, FilePath);
		namespace fs = std::filesystem;
		if (!fs::is_regular_file(Resolved))
		{
			MAHO_LOG_CORE_WARN("Script Lua LoadScript: file not found '{}'", Resolved);
			return nullptr;
		}

		sol::protected_function_result Result = Impl->Lua.safe_script_file(Resolved);
		if (!Result.valid())
		{
			const sol::error Error = Result;
			MAHO_LOG_CORE_ERROR("Script Lua LoadScript('{}'): {}", Resolved, Error.what());
			return nullptr;
		}

		sol::object Top = Result.get<sol::object>();
		if (Top.valid())
		{
			MAHO_LOG_CORE_INFO("Script Lua LoadScript loaded '{}'", Resolved);
			return new sol::object(Top);
		}

		MAHO_LOG_CORE_WARN("Script Lua LoadScript('{}'): script returned no value", Resolved);
		return nullptr;
	}

	bool CallGlobal(const char* FunctionName) override
	{
		if (!bInitialized || !Impl || !HasFunction(FunctionName))
		{
			return false;
		}

		sol::protected_function Function = Impl->Lua[FunctionName];
		sol::protected_function_result Result = Function();
		if (!Result.valid())
		{
			const sol::error Error = Result;
			MAHO_LOG_CORE_ERROR("Script Lua Call('{}'): {}", FunctionName, Error.what());
			return false;
		}
		return true;
	}

	bool CallGlobal(const char* FunctionName, float Arg0) override
	{
		if (!bInitialized || !Impl || !HasFunction(FunctionName))
		{
			return false;
		}

		sol::protected_function Function = Impl->Lua[FunctionName];
		sol::protected_function_result Result = Function(Arg0);
		if (!Result.valid())
		{
			const sol::error Error = Result;
			MAHO_LOG_CORE_ERROR("Script Lua Call('{}', float): {}", FunctionName, Error.what());
			return false;
		}
		return true;
	}

	bool CallHandle(void* Handle, const char* FunctionName) override
	{
		if (!bInitialized || !Impl || !Handle || !FunctionName || FunctionName[0] == '\0')
		{
			return false;
		}

		sol::table& Table = *static_cast<sol::table*>(Handle);
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
			MAHO_LOG_CORE_ERROR("Script Lua Call(table, '{}'): {}", FunctionName, Error.what());
			return false;
		}
		return true;
	}

	bool CallHandle(void* Handle, const char* FunctionName, float Arg0) override
	{
		if (!bInitialized || !Impl || !Handle || !FunctionName || FunctionName[0] == '\0')
		{
			return false;
		}

		sol::table& Table = *static_cast<sol::table*>(Handle);
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
			MAHO_LOG_CORE_ERROR("Script Lua Call(table, '{}', float): {}", FunctionName, Error.what());
			return false;
		}
		return true;
	}

	void* GetState() override
	{
		return (bInitialized && Impl) ? static_cast<void*>(&Impl->Lua) : nullptr;
	}

	void RegisterTypeBinder(FTypeBinder Binder) override
	{
		if (!Binder)
		{
			return;
		}
		if (!bInitialized || !Impl)
		{
			PendingTypeBinders.push_back(Binder);
			return;
		}
		Binder(&Impl->Lua);
	}

private:
	bool HasFunction(const char* FunctionName) const
	{
		if (!bInitialized || !Impl || !FunctionName || FunctionName[0] == '\0')
		{
			return false;
		}
		sol::object Object = Impl->Lua[FunctionName];
		return Object.is<sol::function>();
	}

	struct FImpl
	{
		sol::state Lua;
	};

	std::unique_ptr<FImpl> Impl;
	bool bInitialized = false;
	std::string ScriptsDirectory;
	std::vector<FTypeBinder> PendingTypeBinders;
};

// -- Host (FScriptSystem) ---------------------------------------------------

FScriptSystem& FScriptSystem::Get()
{
	static FScriptSystem Instance;
	return Instance;
}

void FScriptSystem::Initialize(FEngineBase& Engine)
{
	// Default backend: Lua. Other languages register themselves (e.g. a
	// ScriptPython / ScriptCSharp plugin) during their own Initialize.
	RegisterLanguage(new FLuaLanguage());

	for (auto& Language : Languages)
	{
		// Script languages no longer receive raw argc/argv; the engine parses the
		// command line into FEngineBase::Get(). Pass nulls - the language can read
		// configuration through the engine instead.
		if (Language->Initialize(0, nullptr, "Scripts"))
		{
			OnLanguageReady.Broadcast(*Language);
		}
	}
}

void FScriptSystem::Shutdown(FEngineBase&)
{
	for (auto& Language : Languages)
	{
		Language->Shutdown();
	}
	Languages.clear();
	Active = nullptr;
	OnLanguageReady.RemoveAll();
}

void FScriptSystem::RegisterLanguage(IScriptLanguage* Language)
{
	if (!Language)
	{
		return;
	}
	for (const auto& Existing : Languages)
	{
		if (Existing->GetName() == Language->GetName())
		{
			delete Language;
			return;
		}
	}
	if (Active == nullptr)
	{
		Active = Language;
	}
	Languages.emplace_back(Language);
}

IScriptLanguage* FScriptSystem::GetLanguage(const char* Name) const
{
	if (!Name)
	{
		return nullptr;
	}
	for (const auto& Language : Languages)
	{
		if (Language->GetName() == Name)
		{
			return Language.get();
		}
	}
	return nullptr;
}

IScriptLanguage* FScriptSystem::GetActive() const
{
	return Active;
}

void FScriptSystem::RegisterTypeBinder(const char* LanguageName, IScriptLanguage::FTypeBinder Binder)
{
	if (IScriptLanguage* Language = GetLanguage(LanguageName))
	{
		Language->RegisterTypeBinder(Binder);
	}
}

bool FScriptSystem::Call(const char* FunctionName)
{
	IScriptLanguage* Language = GetActive();
	return Language != nullptr && Language->CallGlobal(FunctionName);
}

bool FScriptSystem::Call(const char* FunctionName, float Arg0)
{
	IScriptLanguage* Language = GetActive();
	return Language != nullptr && Language->CallGlobal(FunctionName, Arg0);
}

bool FScriptSystem::DoFile(const char* FilePath)
{
	IScriptLanguage* Language = GetActive();
	return Language != nullptr && Language->DoFile(FilePath);
}

void* FScriptSystem::TryGetState()
{
	IScriptLanguage* Language = GetActive();
	return Language != nullptr ? Language->GetState() : nullptr;
}

} // namespace Maho::Script
