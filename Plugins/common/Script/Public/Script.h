#pragma once

#include <Core/Delegate.h>
#include <Core/Interface.h>
#include <Core/Singleton.h>
#include <Engine/Layer.h>

#include <memory>
#include <string>
#include <vector>

namespace Maho
{
namespace Script
{

class FScriptSystem;

/**
 * Script language backend — one implementation per language (Lua, Python, C#).
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

	/** Language name ("Lua" / "Python" / "CSharp") — registration/select/log. */
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

	/** Call a global function. Missing → false (no error). */
	virtual bool CallGlobal(const char* FunctionName) = 0;
	virtual bool CallGlobal(const char* FunctionName, float Arg0) = 0;

	/** Call a function on an opaque handle (e.g. an entity script table). */
	virtual bool CallHandle(void* Handle, const char* FunctionName) = 0;
	virtual bool CallHandle(void* Handle, const char* FunctionName, float Arg0) = 0;

	/** Opaque pointer to the language state (sol::state* / PyObject* / ...). */
	[[nodiscard]] virtual void* GetState() = 0;

	/** Type binder — the language state is passed opaque to the binder. */
	using FTypeBinder = void (*)(void* LanguageState);

	/** Register a type-level binder; queued until Initialize (or run immediately). */
	virtual void RegisterTypeBinder(FTypeBinder Binder) = 0;
};

/**
 * Script host — manages multiple language backends (Lua/Python/C#...).
 * Initialize brings up every registered language; Shutdown tears them down
 * symmetrically. Convenience templates (LoadScript/Call) forward to the ACTIVE
 * language (the first registered one).
 *
 *   Script::FScriptSystem::Get().Initialize(0, nullptr);  // starts all backends
 *   Script::FScriptSystem::Get().DoFile("main.lua");      // host loads scripts
 *   Script::FScriptSystem::Get().Call("OnUpdate", dt);    // host drives per frame
 */
class FScriptSystem
	: public TSingleton<FScriptSystem>
	, public IPlugin<IInit, IShutdown>
{
public:
	/** Fired after each language Initialize succeeds (binder queue already run). */
	using FOnLanguageReady = TMulticastEvent<void(IScriptLanguage&)>;

	/** Process-unique accessor — defined in Script.cpp (in Script.dll). */
	static FScriptSystem& Get();

	void Initialize(int Argc, char** Argv) override;
	void Shutdown() override;

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
	 * The caller provides the sol type — call site must include <sol/sol.hpp>.
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
	 * handle instead of the global scope. The caller provides the handle type —
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
	std::vector<std::unique_ptr<IScriptLanguage>> Languages;
	IScriptLanguage* Active = nullptr;
	FOnLanguageReady OnLanguageReady;
};

} // namespace Script
} // namespace Maho

// ── Lua 注册语法糖（Lua backend only — see FLuaLanguage in Script.cpp）──────
// 供绑定代码（MAHO_LUA_BIND_REGISTER 回调 / 手动）在 include <sol/sol.hpp> 后使用。
// Table 是 sol::table（如 Lua.create_named_table("maho") 的返回值）。
//
//   sol::state& Lua = *static_cast<sol::state*>(Script.TryGetState());
//   sol::table Maho = Lua.create_named_table("maho");
//   MAHO_LUA_METHOD(Maho, FMesh, GetName);          // maho.get_name() 绑定成员函数
//   MAHO_LUA_FUNCTION(Maho, "my_free_fn", MyFreeFn); // 自由函数/lambda
//   MAHO_LUA_PROPERTY(Maho, "hp", &FUnit::GetHp, &FUnit::SetHp);  // 属性

/** 注册一个 C++ 成员函数到 Lua 表（sol2 set_function，snake_case 自动）。 */
#define MAHO_LUA_METHOD(Table, Class, Method) \
	(Table).set_function(#Method, &Class::Method)

/** 注册一个 C++ 自由函数 / lambda 到 Lua 表。 */
#define MAHO_LUA_FUNCTION(Table, Name, Fn) \
	(Table).set_function(Name, Fn)

/** 注册一个 C++ 属性（getter + setter，省略 setter 为只读）。 */
#define MAHO_LUA_PROPERTY(Table, Name, Getter, ...) \
	(Table).set_property(Name, Getter, __VA_ARGS__)

// ── 类内内联绑定宏（Begin/End 包裹，展开成 static void LuaBind(sol::state&)）──
// 在类体内使用，中间夹绑定宏。需 include <sol/sol.hpp> 可见。
//
//   class FUnit
//   {
//   public:
//       int HP = 0;
//       void Attack();
//
//       MAHO_LUA_BIND_BEGIN(FUnit)        // 类内，类型名写一次
//           MAHO_LUA_FIELD(HP)            //   unit.hp
//           MAHO_LUA_METHOD_FN(Attack)    //   unit:Attack()
//       MAHO_LUA_BIND_END();
//   };
//
//   展开成：static void LuaBind(sol::state& _Lua) { sol::usertype<FUnit> _Meta =
//   _Lua.new_usertype<FUnit>("FUnit"); ... }。调用方：Type::LuaBind(LuaState)。

/** Begin：建 usertype + 暴露绑定上下文（_Lua / _Meta 在中间宏作用域内）。 */
#define MAHO_LUA_BIND_BEGIN(Type) \
	static void LuaBind(sol::state& _Lua) \
	{ \
		sol::usertype<Type> _Meta = _Lua.new_usertype<Type>(#Type); \
		(void)_Meta; \
		{

/** 直接成员字段 → 点符号 unit.field（可读可写）。 */
#define MAHO_LUA_FIELD(Type, Field) \
			_Meta[#Field] = &Type::Field;

/** getter/setter 属性 → 点符号 unit.name（可读可写）。 */
#define MAHO_LUA_PROPERTY_MEMBER(Type, Name, Getter, Setter) \
			_Meta[Name] = sol::property(&Type::Getter, &Type::Setter);

/** 成员函数 → 点符号 unit:Method()。 */
#define MAHO_LUA_METHOD_FN(Type, Method) \
			_Meta[#Method] = &Type::Method;

/** End：关闭绑定块。 */
#define MAHO_LUA_BIND_END() \
			} \
		}

/**
 * 把类内 BIND_BEGIN 生成的 static LuaBind(sol::state&) 注册到 Lua 后端。
 * Lua Initialize 后批量执行（或已初始化则立即执行）。
 *
 *   MAHO_LUA_BIND_REGISTER(FUnit);   // 任意位置调用一次（如主层 Initialize）
 */
#define MAHO_LUA_BIND_REGISTER(Type) \
	Maho::Script::FScriptSystem::Get().RegisterTypeBinder("Lua", [](void* _LuaState) \
	{ \
		Type::LuaBind(*static_cast<sol::state*>(_LuaState)); \
	})
