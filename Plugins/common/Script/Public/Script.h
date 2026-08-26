#pragma once

#include <Core/Interface.h>
#include <Core/Singleton.h>
#include <Engine/Layer.h>
#include <Exception.h>

#include <memory>
#include <string>
#include <vector>

namespace Maho
{
namespace Script
{

class FScriptSystem;

/**
 * Types that can register themselves into the Lua VM.
 * FScriptSystem::Bind forwards to BindLua — no per-type hardcode on FScriptSystem.
 * Prefer auto-bind by listening to FScriptSystem::GetOnLuaReady().
 */
class ILuaBindable
{
public:
	virtual ~ILuaBindable() = default;

	/** Called when Lua is ready (or immediately if already initialized). */
	virtual void BindLua(FScriptSystem& Script) = 0;
};

/**
 * Pure Lua VM singleton service (sol2 + Lua 5.4). No ECS entity knowledge —
 * entity-script dispatch lives in the game project. Runs on the game thread
 * only — do not Call from worker / render threads.
 *
 * Built-in bindings (table `maho`):
 *   maho.log / log_warn / log_error(msg)
 *   maho.get/set_cvar_*          (ConsoleVariable plugin)
 *
 * Extra bindings: implement ILuaBindable::BindLua and either
 *   Script.Bind(Obj) or subscribe to GetOnLuaReady() for auto-bind.
 * Global bootstrap script Scripts/main.lua is loaded on Initialize; its optional
 * OnUpdate(float) global is driven each frame via the host calling Tick(dt).
 *
 *   Script::FScriptSystem::Get().Initialize(0, nullptr);   // starts Lua VM
 *   Script::FScriptSystem::Get().DoFile("main.lua");       // load script
 *   while (running) { Script::FScriptSystem::Get().Tick(dt); }
 */
class FScriptSystem
	: public TSingleton<FScriptSystem>
	, public IPlugin<IInit, IShutdown>
{
public:
	/** Fired after Lua Initialize succeeds (and after any Bind queued before init). */
	using FOnLuaReady = Exception::TMulticastEvent<void(FScriptSystem&)>;

	/** Process-unique accessor — defined in Script.cpp (in Script.dll). */
	static FScriptSystem& Get();

	void Initialize(int Argc, char** Argv) override;
	void Shutdown() override;

	/** Drive the per-frame update: calls the Lua global OnUpdate(dt). */
	void Tick(float DeltaSeconds);

	[[nodiscard]] bool IsLuaInitialized() const { return bLuaInitialized; }
	[[nodiscard]] const std::string& GetScriptsDirectory() const { return ScriptsDirectory; }

	/** Opaque pointer to the engine sol::state (cast in .cpp that includes sol). */
	[[nodiscard]] void* TryGetLuaState();

	[[nodiscard]] FOnLuaReady& GetOnLuaReady() { return OnLuaReady; }

	/** Forward to Bindable.BindLua(*this); queues until Initialize if not ready. */
	void Bind(ILuaBindable& Bindable);

	/** Load + run a .lua file (relative paths resolve under ScriptsDirectory). */
	[[nodiscard]] bool DoFile(const std::string& FilePath);

	[[nodiscard]] bool HasFunction(const char* FunctionName);

	/** Call a global Lua function with no args. Missing → false (no error). */
	[[nodiscard]] bool Call(const char* FunctionName);

	/** Call a global Lua function with one float. */
	[[nodiscard]] bool Call(const char* FunctionName, float Arg0);

private:
	[[nodiscard]] bool InitializeLua(const std::string& ScriptsDirectory);
	void ShutdownLua();

	struct FImpl;
	std::unique_ptr<FImpl> Impl;
	bool bLuaInitialized = false;
	std::string ScriptsDirectory;
	FOnLuaReady OnLuaReady;
	std::vector<ILuaBindable*> PendingBindables;
};

} // namespace Script
} // namespace Maho
