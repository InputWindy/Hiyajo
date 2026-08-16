#pragma once

#include "ConsoleVariableApi.h"
#include <Engine.h>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

namespace Maho
{

namespace ConsoleVariable
{

/** Console variable registry extension. Pre-app toolkit (driven by EToolStage). */
class MAHO_CONSOLEVARIABLE_API FConsoleVariable final : public TExtension<EToolStage, FConsoleVariable>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

	/** Register a typed console variable (overwrites on duplicate name). */
	void Register(std::string_view Name, int Value, std::string_view Description);
	void Register(std::string_view Name, float Value, std::string_view Description);
	void Register(std::string_view Name, bool Value, std::string_view Description);
	void Register(std::string_view Name, std::string Value, std::string_view Description);

	[[nodiscard]] int GetInt(std::string_view Name) const;
	[[nodiscard]] float GetFloat(std::string_view Name) const;
	[[nodiscard]] bool GetBool(std::string_view Name) const;
	[[nodiscard]] std::string GetString(std::string_view Name) const;

	void SetInt(std::string_view Name, int Value);
	void SetFloat(std::string_view Name, float Value);
	void SetBool(std::string_view Name, bool Value);
	void SetString(std::string_view Name, std::string Value);

	[[nodiscard]] bool Has(std::string_view Name) const;

private:
	struct FCVarEntry
	{
		std::string Value;
		std::string Description;
	};

	friend TSingleton<FConsoleVariable>;
	FConsoleVariable() = default;

	std::map<std::string, FCVarEntry> Registry;
	mutable std::mutex Mutex;
};

} // namespace ConsoleVariable

} // namespace Maho
