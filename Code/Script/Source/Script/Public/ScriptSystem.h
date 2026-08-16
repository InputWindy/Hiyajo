#pragma once

#include <Core/Misc/Delegate.h>
#include <Core/Misc/DependsPack.h>
#include "ScriptApi.h"
#include <Core/Engine/EngineExtension.h>
#include <Core/Misc/TypeList.h>
#include <ResourceSystem.h>

#include <memory>
#include <string>
#include <vector>

namespace Maho
{

class FScriptSystem;

/**
 * Types that can register themselves into the Lua VM.
 * FScriptSystem::Bind forwards to BindLua — no per-type hardcode on FScriptSystem.
 * Prefer auto-bind by listening to FScriptSystem::GetOnLuaReady().
 */
class MAHO_SCRIPT_API ILuaBindable
{
public:
	virtual ~ILuaBindable() = default;

	/** Called when Lua is ready (or immediately if already initialized). */
	virtual void BindLua(FScriptSystem& Script) = 0;
};

/**
 * Pure Lua VM extension (sol2 + Lua 5.4). No ECS entity / component / transform
 * knowledge — entity-script dispatch lives in the game project.
 * Init after Resource so BindLua / reflect see a live catalog.
 * Runs on the game thread only — do not Call from worker / render threads.
 *
 * Built-in bindings (table `maho`):
 *   maho.log / log_warn / log_error(msg)
 *   maho.get/set_cvar_*
 *   maho.object / package / resource — codegen usertypes (snake_case methods)
 *
 * Extra bindings: implement ILuaBindable::BindLua and either
 *   Script.Bind(Obj) or subscribe to GetOnLuaReady() for auto-bind.
 * Global bootstrap script Scripts/main.lua is loaded on Attach;
 * its optional OnUpdate / OnFixedUpdate globals are driven each frame.
 */
class MAHO_SCRIPT_API FScriptSystem final
	: public IEngineExtension
	, public TDependsPack<
		TDependsOn<EEngineStage::Init, TTypeList<FResourceSystem>>>
{
public:
	/** Fired after Lua Initialize succeeds (and after any Bind queued before init). */
	MAHO_DECLARE_MULTICAST_DELEGATE_OneParam(FOnLuaReady, FScriptSystem&);

	FScriptSystem();
	~FScriptSystem() override;

	FScriptSystem(const FScriptSystem&) = delete;
	FScriptSystem& operator=(const FScriptSystem&) = delete;

	[[nodiscard]] bool IsLuaInitialized() const
	{
		return bLuaInitialized;
	}
	[[nodiscard]] const std::string& GetScriptsDirectory() const
	{
		return ScriptsDirectory;
	}

	/**
	 * Opaque pointer to the engine sol::state (cast in .cpp that includes sol).
	 * nullptr if not initialized. Public headers must not depend on sol2.
	 */
	[[nodiscard]] void* TryGetLuaState();

	[[nodiscard]] FOnLuaReady& GetOnLuaReady()
	{
		return OnLuaReady;
	}
	[[nodiscard]] const FOnLuaReady& GetOnLuaReady() const
	{
		return OnLuaReady;
	}

	/**
	 * Forward to Bindable.BindLua(*this). If Lua is not ready yet, queues until Init.
	 */
	void Bind(ILuaBindable& Bindable);

	/**
	 * Load and run a .lua file. Relative paths are resolved under ScriptsDirectory.
	 * Returns false on load/runtime error (logged); does not abort the engine.
	 */
	[[nodiscard]] bool DoFile(const std::string& FilePath);

	[[nodiscard]] bool HasFunction(const char* FunctionName);

	/** Call a global Lua function with no args. Missing function → false (no error). */
	[[nodiscard]] bool Call(const char* FunctionName);

	/** Call a global Lua function with one float (e.g. OnUpdate / OnFixedUpdate). */
	[[nodiscard]] bool Call(const char* FunctionName, float Arg0);

private:
	const char* GetName() const override
	{
		return "Script";
	}
	bool ExecuteStage(EEngineStage Stage) override;

	[[nodiscard]] bool InitializeLua(const std::string& ScriptsDirectory);
	void ShutdownLua();

	struct FImpl;
	std::unique_ptr<FImpl> Impl;
	bool bLuaInitialized = false;
	std::string ScriptsDirectory;
	FOnLuaReady OnLuaReady;
	std::vector<ILuaBindable*> PendingBindables;
};

} // namespace Maho
