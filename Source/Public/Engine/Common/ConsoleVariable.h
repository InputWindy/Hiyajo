#pragma once

// ConsoleVariable — CVar registry (engine Common, TSingleton). Static
// TAutoConsoleVariable globals self-register at static-init; Find looks them
// up. Values stored as strings, parsed on typed access.
#include <Core/Singleton.h>

#include <cstdint>
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
 * Console variable interface — what FindConsoleVariable returns. Values are
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
 * fixed Initiate/Shutdown lifecycle (Shutdown clears the registry).
 */
class FConsoleVariable : public TSingleton<FConsoleVariable>
{
public:
	/** Process-unique accessor — declared here, defined in ConsoleVariable.cpp (in Maho.dll). */
	static FConsoleVariable& Get();

	void Initiate(int Argc, char** Argv) override;
	void Shutdown() override;

	/** Find a registered variable; nullptr when absent. */
	[[nodiscard]] IConsoleVariable* Find(std::string_view Name);

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

// ── type traits ──

template <typename T> struct TCVarType;
template <> struct TCVarType<int>         { static constexpr ECVarType Value = ECVarType::Int; };
template <> struct TCVarType<float>       { static constexpr ECVarType Value = ECVarType::Float; };
template <> struct TCVarType<bool>        { static constexpr ECVarType Value = ECVarType::Bool; };
template <> struct TCVarType<std::string> { static constexpr ECVarType Value = ECVarType::String; };

/**
 * Static console variable — registers on construction (static init), like
 * UE's TAutoConsoleVariable.
 *
 *   static TAutoConsoleVariable<int> CVarMaxFPS("r.MaxFPS", 60, "Max FPS");
 *
 *   const int MaxFPS = CVarMaxFPS.GetValue();
 *   CVarMaxFPS.Set(120);
 */
template <typename T>
class TAutoConsoleVariable
{
public:
	TAutoConsoleVariable(std::string_view InName, T Default, std::string_view Description, ECVarFlags Flags = ECVarFlags::None)
		: Name(InName)
	{
		Handle = FConsoleVariable::Get().Register(
			InName,
			TCVarType<T>::Value,
			ToString(Default),
			Description,
			Flags);
	}

	[[nodiscard]] T GetValue() const
	{
		if constexpr (std::is_same_v<T, int>)         return Handle->GetInt();
		else if constexpr (std::is_same_v<T, float>)  return Handle->GetFloat();
		else if constexpr (std::is_same_v<T, bool>)   return Handle->GetBool();
		else                                          return Handle->GetString();
	}

	void Set(T Value)
	{
		Handle->Set(ToString(Value));
	}

	[[nodiscard]] std::string_view GetName() const { return Name; }

private:
	[[nodiscard]] static std::string ToString(const T& Value)
	{
		if constexpr (std::is_same_v<T, std::string>) return Value;
		else if constexpr (std::is_same_v<T, bool>)   return Value ? "true" : "false";
		else                                          return std::to_string(Value);
	}

	std::string Name;
	IConsoleVariable* Handle = nullptr;
};

} // namespace ConsoleVariable
} // namespace Maho
