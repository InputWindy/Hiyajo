#pragma once

#include <Core/Delegate.h>
#include <Core/Interface.h>
#include <Maho.h>
#include <Engine/Engine.h>

#include "ScriptApi.h"

#include <memory>
#include <string>
#include <vector>

namespace Maho
{
namespace Script
{

class FScriptSystem;

/** Global script system accessor - returns FScriptSystem* (cross-DLL via function). */
MAHO_SCRIPT_API FScriptSystem* GetScriptSystem();

/**
 * Script language backend - one implementation per language (Lua, Python, C#).
 * The host (FScriptSystem) drives languages ONLY through this interface; all
 * VM details, type binding and per-language state live inside the backend.
 *
 * Language neutrality: every value crossing the boundary is an opaque void*
 * (the caller casts it in a context where the language type is known). Script
 * files resolve relative to the backend's scripts directory.
 */
class IScriptLanguage
{
public:
	virtual ~IScriptLanguage() = default;

	/** Language name ("Lua" / "Python" / "CSharp") - registration/select/log. */
	[[nodiscard]] virtual const char* GetName() const = 0;

	virtual bool Initialize(int Argc, char** Argv, const char* ScriptsDirectory) = 0;
	virtual void Shutdown() = 0;
	[[nodiscard]] virtual bool IsInitialized() const = 0;

	/** Execute a script file (top-level code). */
	virtual bool DoFile(const char* FilePath) = 0;

	/**
	 * Execute a file and return its top-level value as `new TSolObject`
	 * (consumed by the host's LoadScript<> template); nullptr on failure.
	 */
	virtual void* LoadScriptRaw(const char* FilePath) = 0;

	/** Call a global function. Missing - false (no error). */
	virtual bool CallGlobal(const char* FunctionName) = 0;
	virtual bool CallGlobal(const char* FunctionName, float Arg0) = 0;

	/** Call a function on an opaque handle (e.g. an entity script table). */
	virtual bool CallHandle(void* Handle, const char* FunctionName) = 0;
	virtual bool CallHandle(void* Handle, const char* FunctionName, float Arg0) = 0;

	/** Opaque pointer to the language state (sol::state* / PyObject* / ...). */
	[[nodiscard]] virtual void* GetState() = 0;

	/** Type binder - the language state is passed opaque to the binder. */
	using FTypeBinder = void (*)(void* LanguageState);

	/** Register a type-level binder; queued until Initialize (or run immediately). */
	virtual void RegisterTypeBinder(FTypeBinder Binder) = 0;
};

/**
 * Script host - manages multiple language backends (Lua/Python/C#...).
 * Initialize brings up every registered language; Shutdown tears them down
 * symmetrically. Convenience templates (LoadScript/Call) forward to the ACTIVE
 * language (the first registered one).
 *
 *   Script::GetScriptSystem()->Initialize(0, nullptr);  // starts all backends
 *   Script::GetScriptSystem()->DoFile("main.lua");      // host loads scripts
 *   Script::GetScriptSystem()->Call("OnUpdate", dt);    // host drives per frame
 */
class FScriptSystem : public FEngineLayer
{
public:
	MAHO_DECLARE_ENGINE_LAYER(FScriptSystem, "Script.dll");

	/** Fired after each language Initialize succeeds (binder queue already run). */
	using FOnLanguageReady = TMulticastEvent<void(IScriptLanguage&)>;

	/** Install a language backend (host takes ownership). Idempotent by name. */
	void RegisterLanguage(IScriptLanguage* Language);

	/** Look up a backend by name; nullptr when not registered. */
	[[nodiscard]] IScriptLanguage* GetLanguage(const char* Name) const;

	/** The ACTIVE backend (first registered); nullptr when none. */
	[[nodiscard]] IScriptLanguage* GetActive() const;

	/** Register a type binder on a named backend (from MAHO_*_BIND_REGISTER). */
	void RegisterTypeBinder(const char* LanguageName, IScriptLanguage::FTypeBinder Binder);

	[[nodiscard]] FOnLanguageReady& GetOnLanguageReady() { return OnLanguageReady; }

	/**
	 * Load a script file and return its top-level value (typically a table).
	 * The caller provides the sol type - call site must include <sol/sol.hpp>.
	 * Returns a null-type value when the file is missing or the active language
	 * returned no value.
	 *
	 *   sol::table Script = FScriptSystem::Get().LoadScript<sol::table>("player.lua");
	 */
	template <typename TSolObject>
	[[nodiscard]] TSolObject LoadScript(const char* FilePath)
	{
		IScriptLanguage* Lang = GetActive();
		if (Lang == nullptr)
		{
			return TSolObject{};
		}
		void* Opaque = Lang->LoadScriptRaw(FilePath);
		if (Opaque == nullptr)
		{
			return TSolObject{};
		}
		TSolObject Result = *static_cast<TSolObject*>(Opaque);
		delete static_cast<TSolObject*>(Opaque);
		return Result;
	}

	/**
	 * Call a function on an arbitrary handle (entity script instance, a
	 * namespace table, ...). Overload of Call that resolves against the given
	 * handle instead of the global scope. The caller provides the handle type -
	 * call site must include <sol/sol.hpp>.
	 *
	 *   FScriptSystem::Get().Call(Script, "on_update", dt);
	 */
	template <typename TSolHandle>
	bool Call(TSolHandle& Handle, const char* FunctionName)
	{
		IScriptLanguage* Lang = GetActive();
		return Lang != nullptr && Lang->CallHandle(&Handle, FunctionName);
	}

	template <typename TSolHandle>
	bool Call(TSolHandle& Handle, const char* FunctionName, float Arg0)
	{
		IScriptLanguage* Lang = GetActive();
		return Lang != nullptr && Lang->CallHandle(&Handle, FunctionName, Arg0);
	}

	/** Call a global function on the active language. */
	[[nodiscard]] bool Call(const char* FunctionName);

	/** Call a global function with one float on the active language. */
	[[nodiscard]] bool Call(const char* FunctionName, float Arg0);

	/** Execute a script file on the active language. */
	[[nodiscard]] bool DoFile(const char* FilePath);

	/** Opaque state of the active language (sol::state* / PyObject* / ...). */
	[[nodiscard]] void* TryGetState();

private:
	// -- engine pipeline stages (scheduler-only) --
	void Initialize(FEngineBase& Engine) override;
	void Shutdown(FEngineBase& Engine) override;

	std::vector<std::unique_ptr<IScriptLanguage>> Languages;
	IScriptLanguage* Active = nullptr;
	FOnLanguageReady OnLanguageReady;
};

} // namespace Script
} // namespace Maho

// -- Lua binding sugar (Lua backend only -> see FLuaLanguage in Script.cpp) ------
// For binding code (MAHO_LUA_BIND_REGISTER callbacks / manual) after include <sol/sol.hpp>.
// Table - sol::table (e.g. the return value of Lua.create_named_table("maho"))
//   sol::state& Lua = *static_cast<sol::state*>(Script.TryGetState());
//   sol::table Maho = Lua.create_named_table("maho");
//   MAHO_LUA_METHOD(Maho, FMesh, GetName);          // binds member fn: maho.get_name()
//   MAHO_LUA_FUNCTION(Maho, "my_free_fn", MyFreeFn); // free function/lambda
//   MAHO_LUA_PROPERTY(Maho, "hp", &FUnit::GetHp, &FUnit::SetHp);  // property
/** Register a C++ member function on a Lua table (sol2 set_function, snake_case auto). */
#define MAHO_LUA_METHOD(Table, Class, Method) \
	(Table).set_function(#Method, &Class::Method)

/** Register a C++ free function / lambda on a Lua table. */
#define MAHO_LUA_FUNCTION(Table, Name, Fn) \
	(Table).set_function(Name, Fn)

/** Register a C++ property (getter + setter; omit setter for read-only). */
#define MAHO_LUA_PROPERTY(Table, Name, Getter, ...) \
	(Table).set_property(Name, Getter, __VA_ARGS__)

// -- In-class inline binding macros (Begin/End wrap; expand to static void LuaBind(sol::state&)) --
// Use inside a class body, with binding macros in between. Needs <sol/sol.hpp> visible.
//   class FUnit
//   {
//   public:
//       int HP = 0;
//       void Attack();
//
//       MAHO_LUA_BIND_BEGIN(FUnit)        // in class, write type name once
//           MAHO_LUA_FIELD(HP)            //   unit.hp
//           MAHO_LUA_METHOD_FN(Attack)    //   unit:Attack()
//       MAHO_LUA_BIND_END();
//   };
//
//   Expands to: static void LuaBind(sol::state& _Lua) { sol::usertype<FUnit> _Meta =
//   _Lua.new_usertype<FUnit>("FUnit"); ... }. Caller: Type::LuaBind(LuaState).
/** Begin: create usertype + expose binding context (_Lua / _Meta visible to inner macros). */
#define MAHO_LUA_BIND_BEGIN(Type) \
	static void LuaBind(sol::state& _Lua) \
	{ \
		sol::usertype<Type> _Meta = _Lua.new_usertype<Type>(#Type); \
		(void)_Meta; \
		{

/** Direct member field - dot access unit.field (read/write). */
#define MAHO_LUA_FIELD(Type, Field) \
			_Meta[#Field] = &Type::Field;

/** getter/setter property - dot access unit.name (read/write). */
#define MAHO_LUA_PROPERTY_MEMBER(Type, Name, Getter, Setter) \
			_Meta[Name] = sol::property(&Type::Getter, &Type::Setter);

/** Member function - dot access unit:Method(). */
#define MAHO_LUA_METHOD_FN(Type, Method) \
			_Meta[#Method] = &Type::Method;

/** End: close the binding block. */
#define MAHO_LUA_BIND_END() \
			} \
		}

/**
 * Register the static LuaBind(sol::state&) generated by a class's BIND_BEGIN
 * with the Lua backend. Runs in batch after Lua Initialize (or immediately if
 * already initialized).
 *
 *   MAHO_LUA_BIND_REGISTER(FUnit);   // call once anywhere (e.g. main Initialize)
 */
#define MAHO_LUA_BIND_REGISTER(Type) \
	Maho::Script::GetScriptSystem()->RegisterTypeBinder("Lua", [](void* _LuaState) \
	{ \
		Type::LuaBind(*static_cast<sol::state*>(_LuaState)); \
	})
