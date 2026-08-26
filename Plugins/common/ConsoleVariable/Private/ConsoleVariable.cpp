#include "ConsoleVariable.h"

#include <mutex>
#include <string>
#include <utility>

namespace Maho::ConsoleVariable
{

FConsoleVariable& FConsoleVariable::Get()
{
	static FConsoleVariable Instance;
	return Instance;
}

namespace
{
	std::mutex GMutex;

	[[nodiscard]] std::string ToLower(std::string_view S)
	{
		std::string Out(S);
		for (char& C : Out)
		{
			if (C >= 'A' && C <= 'Z')
			{
				C = static_cast<char>(C - 'A' + 'a');
			}
		}
		return Out;
	}

	class FCVarEntry final : public IConsoleVariable
	{
	public:
		FCVarEntry(std::string InName, ECVarType InType, std::string InValue, std::string InDescription, ECVarFlags InFlags)
			: Name(std::move(InName))
			, Type(InType)
			, Value(std::move(InValue))
			, Description(std::move(InDescription))
			, Flags(InFlags)
		{
		}

		[[nodiscard]] std::string_view GetName() const override { return Name; }
		[[nodiscard]] std::string_view GetDescription() const override { return Description; }
		[[nodiscard]] ECVarFlags GetFlags() const override { return Flags; }

		[[nodiscard]] int GetInt() const override
		{
			try { return std::stoi(Value); }
			catch (...) { return 0; }
		}

		[[nodiscard]] float GetFloat() const override
		{
			try { return std::stof(Value); }
			catch (...) { return 0.0f; }
		}

		[[nodiscard]] bool GetBool() const override
		{
			const std::string Lower = ToLower(Value);
			return Lower == "true" || Lower == "1" || Lower == "yes" || Lower == "on";
		}

		[[nodiscard]] std::string GetString() const override { return Value; }

		void Set(std::string_view InValue) override
		{
			if (HasFlag(Flags, ECVarFlags::ReadOnly))
			{
				return;
			}
			Value = std::string(InValue);
		}

	private:
		std::string Name;
		ECVarType Type;
		std::string Value;
		std::string Description;
		ECVarFlags Flags;
	};
}

void FConsoleVariable::Initialize(int Argc, char** Argv)
{
	// Static TAutoConsoleVariable globals registered at static-init already;
	// nothing to bring up. Explicitly no clear — those globals must survive.
	(void)Argc; (void)Argv;
}

void FConsoleVariable::Shutdown()
{
	std::lock_guard<std::mutex> Lock(GMutex);
	Registry.clear();
}

IConsoleVariable* FConsoleVariable::Find(std::string_view Name)
{
	std::lock_guard<std::mutex> Lock(GMutex);
	const auto It = Registry.find(std::string(Name));
	return It != Registry.end() ? It->second.get() : nullptr;
}

IConsoleVariable* FConsoleVariable::Register(
	std::string_view Name,
	ECVarType Type,
	std::string DefaultValue,
	std::string_view Description,
	ECVarFlags Flags)
{
	std::lock_guard<std::mutex> Lock(GMutex);
	auto Entry = std::make_unique<FCVarEntry>(
		std::string(Name), Type, std::move(DefaultValue), std::string(Description), Flags);
	IConsoleVariable* Result = Entry.get();
	Registry[std::string(Name)] = std::move(Entry);
	return Result;
}

} // namespace Maho::ConsoleVariable
