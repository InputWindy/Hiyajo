#pragma once

#include <Core/Misc/ConsoleVariable.h>
#include <Core/Misc/Export.h>

#include <string>
#include <vector>

namespace Maho
{

/**
 * CVar registry (UE IConsoleManager subset). Process-wide variable storage;
 * FEngineBase owns a facade — FConsole::Get() resolves via GEngine.
 *
 * Cross-module string access:
 *   FConsole::Get().GetInt("maho.Window.Width");
 */
class MAHO_API FConsole
{
public:
	FConsole() = default;
	~FConsole() = default;

	FConsole(const FConsole&) = delete;
	FConsole& operator=(const FConsole&) = delete;

	/**
	 * App-owned manager when GEngine is set; otherwise a pre-App fallback
	 * (static TAutoConsoleVariable registration before main).
	 */
	[[nodiscard]] static FConsole& Get();

	[[nodiscard]] IConsoleVariable* RegisterBool(
		const char* Name,
		bool DefaultValue,
		const char* Help,
		EConsoleVariableFlags Flags = EConsoleVariableFlags::Default);

	[[nodiscard]] IConsoleVariable* RegisterInt(
		const char* Name,
		int DefaultValue,
		const char* Help,
		EConsoleVariableFlags Flags = EConsoleVariableFlags::Default);

	[[nodiscard]] IConsoleVariable* RegisterFloat(
		const char* Name,
		float DefaultValue,
		const char* Help,
		EConsoleVariableFlags Flags = EConsoleVariableFlags::Default);

	[[nodiscard]] IConsoleVariable* RegisterString(
		const char* Name,
		const char* DefaultValue,
		const char* Help,
		EConsoleVariableFlags Flags = EConsoleVariableFlags::Default);

	/** Find by name (case-insensitive). nullptr if not registered yet (check early-set queue separately). */
	[[nodiscard]] IConsoleVariable* Find(const char* Name) const;
	[[nodiscard]] IConsoleVariable* Find(const std::string& Name) const { return Find(Name.c_str()); }

	/** Typed getters by name — safe across DLL/EXE without the defining header. */
	[[nodiscard]] bool GetBool(const char* Name, bool DefaultValue = false) const;
	[[nodiscard]] int GetInt(const char* Name, int DefaultValue = 0) const;
	[[nodiscard]] float GetFloat(const char* Name, float DefaultValue = 0.0f) const;
	[[nodiscard]] std::string GetString(const char* Name, const char* DefaultValue = "") const;

	[[nodiscard]] bool TryGetBool(const char* Name, bool& OutValue) const;
	[[nodiscard]] bool TryGetInt(const char* Name, int& OutValue) const;
	[[nodiscard]] bool TryGetFloat(const char* Name, float& OutValue) const;
	[[nodiscard]] bool TryGetString(const char* Name, std::string& OutValue) const;

	/**
	 * Set by name. If the CVar is not registered yet, queues an early set (UE-style)
	 * that is applied when Register* runs.
	 */
	bool SetBool(const char* Name, bool Value, EConsoleVariableSetBy SetBy = EConsoleVariableSetBy::Code);
	bool SetInt(const char* Name, int Value, EConsoleVariableSetBy SetBy = EConsoleVariableSetBy::Code);
	bool SetFloat(const char* Name, float Value, EConsoleVariableSetBy SetBy = EConsoleVariableSetBy::Code);
	bool SetString(const char* Name, const char* Value, EConsoleVariableSetBy SetBy = EConsoleVariableSetBy::Code);
	bool SetFromString(const char* Name, const char* Value, EConsoleVariableSetBy SetBy = EConsoleVariableSetBy::Code);

	/**
	 * Apply [ConsoleVariables] from an .ini (UE DefaultEngine.ini style).
	 * Returns number of variables applied or queued; -1 if the file cannot be opened.
	 */
	int LoadConsoleVariablesFromIni(const std::string& IniFilePath);

	/** Apply [ConsoleVariables] section already loaded into a config file. */
	int ApplyConsoleVariablesSection(
		const class FConfigFile& Config,
		const char* SectionName = "ConsoleVariables",
		EConsoleVariableSetBy SetBy = EConsoleVariableSetBy::ConsoleVariablesIni);

	[[nodiscard]] std::vector<std::string> GetNames() const;

	/** Log all registered CVars (name = value). */
	void Dump() const;
};

/**
 * Static registration helper (define in a .cpp — one instance per CVar name).
 * Type is deduced from the default value (bool / int / float / const char*).
 * Other modules should read via FConsole::Get().GetInt("name"), not this object.
 *
 * Example:
 * ```
 *   static Maho::TAutoConsoleVariable CVarSamples(
 *       "r.MyPass.Samples", 4, "Sample count for MyPass");
 *
 *   void FMyPass::Execute()
 *   {
 *       const int Samples = CVarSamples.GetValue();
 *   }
 * ```
 */
template <typename T>
class TAutoConsoleVariable
{
public:
	TAutoConsoleVariable(
		const char* Name,
		T DefaultValue,
		const char* Help,
		EConsoleVariableFlags Flags = EConsoleVariableFlags::Default);

	[[nodiscard]] T GetValue() const;
	[[nodiscard]] IConsoleVariable& AsVariable() const { return *Variable; }

private:
	IConsoleVariable* Variable = nullptr;
};

template <>
inline TAutoConsoleVariable<bool>::TAutoConsoleVariable(
	const char* Name,
	bool DefaultValue,
	const char* Help,
	EConsoleVariableFlags Flags)
	: Variable(FConsole::Get().RegisterBool(Name, DefaultValue, Help, Flags))
{
}

template <>
inline bool TAutoConsoleVariable<bool>::GetValue() const
{
	return Variable ? Variable->GetBool() : false;
}

template <>
inline TAutoConsoleVariable<int>::TAutoConsoleVariable(
	const char* Name,
	int DefaultValue,
	const char* Help,
	EConsoleVariableFlags Flags)
	: Variable(FConsole::Get().RegisterInt(Name, DefaultValue, Help, Flags))
{
}

template <>
inline int TAutoConsoleVariable<int>::GetValue() const
{
	return Variable ? Variable->GetInt() : 0;
}

template <>
inline TAutoConsoleVariable<float>::TAutoConsoleVariable(
	const char* Name,
	float DefaultValue,
	const char* Help,
	EConsoleVariableFlags Flags)
	: Variable(FConsole::Get().RegisterFloat(Name, DefaultValue, Help, Flags))
{
}

template <>
inline float TAutoConsoleVariable<float>::GetValue() const
{
	return Variable ? Variable->GetFloat() : 0.0f;
}

template <>
inline TAutoConsoleVariable<std::string>::TAutoConsoleVariable(
	const char* Name,
	std::string DefaultValue,
	const char* Help,
	EConsoleVariableFlags Flags)
	: Variable(FConsole::Get().RegisterString(Name, DefaultValue.c_str(), Help, Flags))
{
}

template <>
inline std::string TAutoConsoleVariable<std::string>::GetValue() const
{
	return Variable ? Variable->GetString() : std::string{};
}

// CTAD: write TAutoConsoleVariable("name", 1.0f, "help") without <float>.
TAutoConsoleVariable(const char*, bool, const char*, EConsoleVariableFlags = EConsoleVariableFlags::Default)
	-> TAutoConsoleVariable<bool>;
TAutoConsoleVariable(const char*, int, const char*, EConsoleVariableFlags = EConsoleVariableFlags::Default)
	-> TAutoConsoleVariable<int>;
TAutoConsoleVariable(const char*, float, const char*, EConsoleVariableFlags = EConsoleVariableFlags::Default)
	-> TAutoConsoleVariable<float>;
TAutoConsoleVariable(const char*, double, const char*, EConsoleVariableFlags = EConsoleVariableFlags::Default)
	-> TAutoConsoleVariable<float>;
TAutoConsoleVariable(const char*, const char*, const char*, EConsoleVariableFlags = EConsoleVariableFlags::Default)
	-> TAutoConsoleVariable<std::string>;

/** Sync built-in maho.* / app.Name CVars into FConfig (call after loading ini). */
MAHO_API void ApplyEngineCVarsToConfig(struct FConfig& OutConfig);

} // namespace Maho
