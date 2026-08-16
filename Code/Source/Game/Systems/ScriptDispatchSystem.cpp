#include "Game/Systems/ScriptDispatchSystem.h"

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <Core/EngineBase.h>
#include <ScriptSystem.h>
#include <ECS/EntityHandle.h>
#include <ECS/Query.h>
#include <ECS/World.h>
#include <Core/Misc/Log.h>

#include "Game/Components/ScriptComponent.h"
#include "Game/Components/TransformComponent.h"

#include <filesystem>
#include <unordered_map>

namespace Maho
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

[[nodiscard]] std::uint32_t PackEntityKey(FEntityHandle Handle)
{
	return (static_cast<std::uint32_t>(Handle.Index) << 8) | static_cast<std::uint32_t>(Handle.Generation);
}

} // namespace

struct FScriptDispatchSystem::FImpl
{
	sol::state* Lua = nullptr;
	std::string ScriptsDirectory;
	std::unordered_map<std::string, sol::table> ScriptPrototypes;
	std::unordered_map<std::uint32_t, sol::table> EntityInstances;
};

FScriptDispatchSystem::FScriptDispatchSystem()
	: Impl(std::make_unique<FImpl>())
{
}

FScriptDispatchSystem::~FScriptDispatchSystem() = default;

void FScriptDispatchSystem::OnBeginFrame(FWorld& World)
{
	DispatchStage(World, 0.0f, "OnBeginFrame");
}

void FScriptDispatchSystem::OnProcessInput(FWorld& World)
{
	DispatchStage(World, 0.0f, "OnProcessInput");
}

void FScriptDispatchSystem::OnFixedUpdate(float DeltaTime, FWorld& World)
{
	DispatchStage(World, DeltaTime, "OnFixedUpdate");
}

void FScriptDispatchSystem::OnUpdate(float DeltaTime, FWorld& World)
{
	DispatchStage(World, DeltaTime, "OnUpdate");
}

void FScriptDispatchSystem::OnLateUpdate(float DeltaTime, FWorld& World)
{
	DispatchStage(World, DeltaTime, "OnLateUpdate");
}

void FScriptDispatchSystem::OnEndFrame(FWorld& World)
{
	DispatchStage(World, 0.0f, "OnEndFrame");
}

void FScriptDispatchSystem::DispatchStage(FWorld& InWorld, float DeltaTime, const char* HookName)
{
	// Lazy: bind Lua + component bindings once the Script extension is ready.
	if (!Impl->Lua)
	{
		FScriptSystem* Script = GEngine ? GEngine->GetExtension<FScriptSystem>() : nullptr;
		if (!Script || !Script->IsLuaInitialized() || !Script->TryGetLuaState())
		{
			return;
		}
		Impl->ScriptsDirectory = Script->GetScriptsDirectory();
		Impl->Lua = static_cast<sol::state*>(Script->TryGetLuaState());
	}

	sol::state& Lua = *Impl->Lua;

	auto Query = InWorld.Query<FScriptComponent>();
	Query.ForEach([&](FEntityHandle Handle, FScriptComponent& Component)
	{
		if (!Component.bEnabled || !Component.IsValid())
		{
			return;
		}

		const std::string Resolved = ResolveScriptPath(Impl->ScriptsDirectory, Component.ScriptPath);

		// 1. Load / cache script prototype (script returns a table of hooks).
		sol::table Prototype;
		auto ProtoIt = Impl->ScriptPrototypes.find(Resolved);
		if (ProtoIt == Impl->ScriptPrototypes.end())
		{
			sol::protected_function_result Result = Lua.safe_script_file(Resolved);
			if (!Result.valid())
			{
				const sol::error Error = Result;
				MAHO_CORE_ERROR("FScriptDispatchSystem('{}'): {}", Resolved, Error.what());
				return;
			}
			if (Result.return_count() < 1 || !Result[0].is<sol::table>())
			{
				MAHO_CORE_ERROR("FScriptDispatchSystem('{}'): script must return a table", Resolved);
				return;
			}
			Prototype = Result[0].as<sol::table>();
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
			Instance = Lua.create_table_with();
			sol::table Metatable = Lua.create_table_with();
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
					MAHO_CORE_ERROR("FScriptDispatchSystem::OnBegin('{}'): {}", Resolved, Error.what());
				}
			}
		}
		else
		{
			Instance = InstIt->second;
		}

		// 3. Mount the transform as a typed usertype pointer (refresh each dispatch).
		FTransformComponent* Transform =
			InWorld.GetEntityManager().GetComponent<FTransformComponent>(Handle);
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
				MAHO_CORE_ERROR("FScriptDispatchSystem::{}({}): {}", HookName, Resolved, Error.what());
			}
		}
	});
}

} // namespace Maho
