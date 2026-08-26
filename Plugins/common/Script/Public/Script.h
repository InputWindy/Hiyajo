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
 * Pure Lua VM singleton service (sol2 + Lua 5.4). No ECS entity knowledge —
 * entity-script dispatch lives in the game project. Runs on the game thread
 * only — do not Call from worker / render threads.
 *
 * Built-in bindings (table `maho`):
 *   maho.log / log_warn / log_error(msg)
 *   maho.get/set_cvar_*          (ConsoleVariable plugin)
 *
 * Extra type bindings: MAHO_LUA_BIND_BEGIN inside the class + one
 * MAHO_LUA_BIND_REGISTER call — no per-type hardcode on FScriptSystem.
 *
 * Project-side execution (which script to load, per-frame OnUpdate driving)
 * is the host's job — the VM only provides the run primitives:
 *
 *   Script::FScriptSystem::Get().Initialize(0, nullptr);   // starts Lua VM
 *   Script::FScriptSystem::Get().DoFile("main.lua");       // host loads scripts
 *   Script::FScriptSystem::Get().Call("OnUpdate", dt);     // host drives per frame
 */
class FScriptSystem
	: public TSingleton<FScriptSystem>
	, public IPlugin<IInit, IShutdown>
{
public:
	/** Fired after Lua Initialize succeeds (and after any binder queued before init). */
	using FOnLuaReady = TMulticastEvent<void(FScriptSystem&)>;

	/**
	 * Type-level Lua binder — opaque `void*` Lua state so Script.h stays sol-free.
	 * Produced by MAHO_LUA_BIND_BEGIN; registered via RegisterTypeBinder and run
	 * batch after Initialize (or immediately when already initialized).
	 */
	using FTypeBinder = void (*)(void* LuaState);

	/** Process-unique accessor — defined in Script.cpp (in Script.dll). */
	static FScriptSystem& Get();

	void Initialize(int Argc, char** Argv) override;
	void Shutdown() override;

	[[nodiscard]] bool IsLuaInitialized() const { return bLuaInitialized; }
	[[nodiscard]] const std::string& GetScriptsDirectory() const { return ScriptsDirectory; }

	/** Opaque pointer to the engine sol::state (cast in .cpp that includes sol). */
	[[nodiscard]] void* TryGetLuaState();

	/**
	 * Load a Lua script file and return its top-level value (typically a table).
	 * The caller provides the sol type — call site must include <sol/sol.hpp>.
	 * Returns a null-type value when the file is missing or its top-level value
	 * is not of the requested sol type.
	 *
	 *   sol::table Script = FScriptSystem::Get().LoadScript<sol::table>("player.lua");
	 */
	template <typename TSolTable>
	[[nodiscard]] TSolTable LoadScript(const char* FilePath)
	{
		void* Opaque = LoadScriptRaw(FilePath);
		if (Opaque == nullptr)
		{
			return TSolTable{};
		}
		TSolTable Result = *static_cast<TSolTable*>(Opaque);
		delete static_cast<TSolTable*>(Opaque);
		return Result;
	}

	/**
	 * Call a function stored in an arbitrary table (an entity script instance,
	 * the `maho` table, ...). Overload of Call that resolves the function
	 * against the given table instead of the global table. The caller provides
	 * the sol table type — call site must include <sol/sol.hpp>.
	 *
	 *   FScriptSystem::Get().Call(Script, "on_update", dt);
	 */
	template <typename TSolTable>
	bool Call(TSolTable& Table, const char* FunctionName)
	{
		return CallRaw(&Table, FunctionName);
	}

	template <typename TSolTable>
	bool Call(TSolTable& Table, const char* FunctionName, float Arg0)
	{
		return CallRaw(&Table, FunctionName, Arg0);
	}

	[[nodiscard]] FOnLuaReady& GetOnLuaReady() { return OnLuaReady; }

	/** Register a type-level binder (from MAHO_LUA_BIND_BEGIN); queues until Initialize. */
	void RegisterTypeBinder(FTypeBinder Binder);

private:
	/** Internal LoadScript helper — returns `new TSolObject` or nullptr (impl in cpp). */
	void* LoadScriptRaw(const char* FilePath);

	/** Internal Call(table, ...) helpers — Table points at the caller's sol::table. */
	bool CallRaw(void* Table, const char* FunctionName);
	bool CallRaw(void* Table, const char* FunctionName, float Arg0);

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
	std::vector<FTypeBinder> PendingTypeBinders;
};

} // namespace Script
} // namespace Maho

// ── Lua 注册语法糖 ───────────────────────────────────────────────────────
// 供绑定代码（RegisterTypeBinder 回调 / 手动）在 include <sol/sol.hpp> 后使用。
// Table 是 sol::table（如 Lua.create_named_table("maho") 的返回值）。
//
//   sol::state& Lua = *static_cast<sol::state*>(Script.TryGetLuaState());
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
 * 把类内 BIND_BEGIN 生成的 static LuaBind(sol::state&) 注册给 FScriptSystem。
 * 在 FScriptSystem::Initialize 后批量执行（或已初始化则立即执行）；重复注册
 * 同类型幂等。
 *
 *   MAHO_LUA_BIND_REGISTER(FUnit);   // 任意位置调用一次（如主层 Initialize）
 */
#define MAHO_LUA_BIND_REGISTER(Type) \
	Maho::Script::FScriptSystem::Get().RegisterTypeBinder([](void* _LuaState) \
	{ \
		Type::LuaBind(*static_cast<sol::state*>(_LuaState)); \
	})

