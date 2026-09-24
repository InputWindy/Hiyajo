#pragma once

// ConsoleVariable - CVar registry (engine Common, TSingleton). Static
// TAutoConsoleVariable globals self-register at static-init; Find looks them
// up. Values stored as strings, parsed on typed access.
#include <Core/Singleton.h>
#include <Maho.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Maho
{
namespace ConsoleVariable
{

/** Console variable flags (UE ECVF_* style). */
enum class ECVarFlags : std::uint32_t
{
	None = 0,
	Cheat = 1u << 0,     // cheat-only
	ReadOnly = 1u << 1,  // cannot be changed at runtime
	/** Stays part of a SHIPPING build: quality / gameplay settings a player switches at runtime.
	 *  Everything NOT carrying this flag is a development knob, and a Shipping build does not
	 *  register it (the variable degrades to a constant -- see TAutoConsoleVariable). The safe
	 *  default is "not registered nowhere": a setting that must survive release must say so. */
	Shipping = 1u << 2,
};

[[nodiscard]] constexpr bool HasFlag(ECVarFlags Flags, ECVarFlags Test)
{
	return (static_cast<std::uint32_t>(Flags) & static_cast<std::uint32_t>(Test)) != 0;
}

/** Console variable value type. */
enum class ECVarType : std::uint8_t
{
	Int,
	Float,
	Bool,
	String,
};

/**
 	 * Console variable interface - what FindConsoleVariable returns. Values are
 * stored as a string internally and parsed on typed access.
 */
class IConsoleVariable
{
public:
	virtual ~IConsoleVariable() = default;

	[[nodiscard]] virtual std::string_view GetName() const = 0;
	[[nodiscard]] virtual std::string_view GetDescription() const = 0;
	[[nodiscard]] virtual ECVarFlags GetFlags() const = 0;

	[[nodiscard]] virtual int GetInt() const = 0;
	[[nodiscard]] virtual float GetFloat() const = 0;
	[[nodiscard]] virtual bool GetBool() const = 0;
	[[nodiscard]] virtual std::string GetString() const = 0;

	/** Set from a string (parsed); ignored when ReadOnly. */
	virtual void Set(std::string_view Value) = 0;
};

/**
 * Console variable registry (UE IConsoleManager). Static TAutoConsoleVariable
 * globals register here at static-init; Find looks them up. TSingleton with the
 * fixed Initialize/Shutdown lifecycle (Shutdown clears the registry).
 */
class FConsoleVariable
	: public TSingleton<FConsoleVariable>
	, public IPlugin<IInit, IShutdown>
{
public:
	/** Process-unique accessor - declared here, defined in ConsoleVariable.cpp (in ConsoleVariable.dll). */
	static FConsoleVariable& Get();

	void Initialize(FEngineBase& Engine, FEngineContext& Frame) override;
	void Shutdown(FEngineBase& Engine, FEngineContext& Frame) override;

	/** Find a registered variable; nullptr when absent. */
	[[nodiscard]] IConsoleVariable* Find(std::string_view Name);

	/** Visit every registered variable in name order (map order). Used by editor
	 *  autocomplete to enumerate known names. The visitor runs while holding the
	 *  registry mutex, so callbacks should not call back into the registry. */
	void VisitAll(const std::function<void(IConsoleVariable&)>& Visitor) const;

	/** Register (used by TAutoConsoleVariable). Returns the interface. */
	IConsoleVariable* Register(
		std::string_view Name,
		ECVarType Type,
		std::string DefaultValue,
		std::string_view Description,
		ECVarFlags Flags);

protected:
	friend TSingleton<FConsoleVariable>;
	FConsoleVariable() = default;

	std::map<std::string, std::unique_ptr<IConsoleVariable>> Registry;
};

// -- type traits --

template <typename T> struct TCVarType;
template <> struct TCVarType<int>         { static constexpr ECVarType Value = ECVarType::Int; };
template <> struct TCVarType<float>       { static constexpr ECVarType Value = ECVarType::Float; };
template <> struct TCVarType<bool>        { static constexpr ECVarType Value = ECVarType::Bool; };
template <> struct TCVarType<std::string> { static constexpr ECVarType Value = ECVarType::String; };

/**
 	* Static console variable - registers on construction (static init), like
 * UE's TAutoConsoleVariable.
 *
 *   static TAutoConsoleVariable<int> CVarMaxFPS("r.MaxFPS", 60, "Max FPS");
 *   static TAutoConsoleVariable<int> CVarQuality("r.Quality", 2, "quality tier", ECVarFlags::Shipping);
 *
 * A SHIPPING build registers only the variables flagged `ECVarFlags::Shipping` (see
 * Core/BuildConfig.h: a release carries what the product needs, not the development knobs). An
 * unregistered variable is not gone -- `GetValue`/`Set` keep working on its own copy, so a setting
 * a release pruned reads as its default instead of breaking the build.
 */
template <typename T>
class TAutoConsoleVariable
{
public:
	TAutoConsoleVariable(std::string_view InName, T Default, std::string_view Description, ECVarFlags Flags = ECVarFlags::None)
		: Name(InName), Value(Default)
	{
#if defined(MAHO_BUILD_SHIPPING)
		if (!HasFlag(Flags, ECVarFlags::Shipping))
		{
			return;   // development knob: no registry entry, its own value is the whole story
		}
#endif
		Handle = FConsoleVariable::Get().Register(
			InName,
			TCVarType<T>::Value,
			ToString(Default),
			Description,
			Flags);
	}

	[[nodiscard]] T GetValue() const
	{
		if (Handle == nullptr)
		{
			return Value;   // pruned by the build configuration: the local copy IS the value
		}
		if constexpr (std::is_same_v<T, int>)         return Handle->GetInt();
		else if constexpr (std::is_same_v<T, float>)  return Handle->GetFloat();
		else if constexpr (std::is_same_v<T, bool>)   return Handle->GetBool();
		else                                          return Handle->GetString();
	}

	void Set(T NewValue)
	{
		Value = NewValue;
		if (Handle == nullptr)
		{
			return;
		}
		Handle->Set(ToString(NewValue));
	}

	[[nodiscard]] bool IsRegistered() const { return Handle != nullptr; }

	[[nodiscard]] std::string_view GetName() const { return Name; }

private:
	[[nodiscard]] static std::string ToString(const T& Value)
	{
		if constexpr (std::is_same_v<T, std::string>) return Value;
		else if constexpr (std::is_same_v<T, bool>)   return Value ? "true" : "false";
		else                                          return std::to_string(Value);
	}

	std::string Name;
	T Value;
	IConsoleVariable* Handle = nullptr;
};

} // namespace ConsoleVariable
} // namespace Maho
