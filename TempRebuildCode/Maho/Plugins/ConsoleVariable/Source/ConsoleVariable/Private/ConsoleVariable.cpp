#include <ConsoleVariable.h>

#include <string>
#include <utility>

namespace Maho::ConsoleVariable
{

void FConsoleVariable::Register(std::string_view Name, int Value, std::string_view Description)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	Registry[std::string(Name)] = { std::to_string(Value), std::string(Description) };
}

void FConsoleVariable::Register(std::string_view Name, float Value, std::string_view Description)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	Registry[std::string(Name)] = { std::to_string(Value), std::string(Description) };
}

void FConsoleVariable::Register(std::string_view Name, bool Value, std::string_view Description)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	Registry[std::string(Name)] = { Value ? "true" : "false", std::string(Description) };
}

void FConsoleVariable::Register(std::string_view Name, std::string Value, std::string_view Description)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	Registry[std::string(Name)] = { std::move(Value), std::string(Description) };
}

int FConsoleVariable::GetInt(std::string_view Name) const
{
	std::lock_guard<std::mutex> Lock(Mutex);
	const auto It = Registry.find(std::string(Name));
	if (It == Registry.end())
	{
		return 0;
	}
	return std::stoi(It->second.Value);
}

float FConsoleVariable::GetFloat(std::string_view Name) const
{
	std::lock_guard<std::mutex> Lock(Mutex);
	const auto It = Registry.find(std::string(Name));
	if (It == Registry.end())
	{
		return 0.0f;
	}
	return std::stof(It->second.Value);
}

bool FConsoleVariable::GetBool(std::string_view Name) const
{
	std::lock_guard<std::mutex> Lock(Mutex);
	const auto It = Registry.find(std::string(Name));
	if (It == Registry.end())
	{
		return false;
	}
	const std::string& Value = It->second.Value;
	return Value == "true" || Value == "1";
}

std::string FConsoleVariable::GetString(std::string_view Name) const
{
	std::lock_guard<std::mutex> Lock(Mutex);
	const auto It = Registry.find(std::string(Name));
	if (It == Registry.end())
	{
		return std::string();
	}
	return It->second.Value;
}

void FConsoleVariable::SetInt(std::string_view Name, int Value)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	const auto It = Registry.find(std::string(Name));
	if (It != Registry.end())
	{
		It->second.Value = std::to_string(Value);
	}
}

void FConsoleVariable::SetFloat(std::string_view Name, float Value)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	const auto It = Registry.find(std::string(Name));
	if (It != Registry.end())
	{
		It->second.Value = std::to_string(Value);
	}
}

void FConsoleVariable::SetBool(std::string_view Name, bool Value)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	const auto It = Registry.find(std::string(Name));
	if (It != Registry.end())
	{
		It->second.Value = Value ? "true" : "false";
	}
}

void FConsoleVariable::SetString(std::string_view Name, std::string Value)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	const auto It = Registry.find(std::string(Name));
	if (It != Registry.end())
	{
		It->second.Value = std::move(Value);
	}
}

bool FConsoleVariable::Has(std::string_view Name) const
{
	std::lock_guard<std::mutex> Lock(Mutex);
	return Registry.find(std::string(Name)) != Registry.end();
}

bool FConsoleVariable::ExecuteStage(EToolStage Stage)
{
	switch (Stage)
	{
	case EToolStage::Init:
	case EToolStage::Shutdown:
		std::lock_guard<std::mutex> Lock(Mutex);
		Registry.clear();
		return true;
	default:
		return true;
	}
}

} // namespace Maho::ConsoleVariable

// ── Dynamic plugin entry (runtime load/unload via FPluginManager) ──

namespace
{

class FConsoleVariableAdapter final : public Maho::IExtension<Maho::EToolStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EToolStage Stage) override
	{
		return Maho::ConsoleVariable::FConsoleVariable::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_CONSOLEVARIABLE_API Maho::IExtension<Maho::EToolStage>* CreateExtension()
{
	return new FConsoleVariableAdapter();
}
