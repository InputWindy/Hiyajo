#include "ConsoleVariable.h"

#include <Core/Fatal.h>   // ReportError: a refused write is a REPORT, not a silent no-op

#include <atomic>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
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

	[[nodiscard]] std::string Trim(std::string_view S)
	{
		const std::size_t First = S.find_first_not_of(" \t\r");
		if (First == std::string_view::npos)
		{
			return "";
		}
		const std::size_t Last = S.find_last_not_of(" \t\r");
		return std::string(S.substr(First, Last - First + 1));
	}

	// -- lazy ini source -----------------------------------------------
	//
	// The registry does NOT depend on the Config layer to honor the project's
	// config: the value is applied on the FIRST read of each variable (see
	// FCVarEntry::ApplyConfigOnce), so a consumer that reads before -- or without
	// -- Config still sees the file. The file, the section (`[ConsoleVariables]`)
	// and the key convention (key == CVar name) are EXACTLY what FConfig::Initialize
	// reads and pushes, so the two sources can never disagree.
	//
	// The file is parsed ONCE for the whole registry (per-variable file reads would
	// be one open per variable); each entry then memoizes its own application with a
	// `std::once_flag`. This mutex is deliberately NOT the registry mutex (GMutex):
	// VisitAll holds GMutex while its visitor reads values, so a shared lock would
	// deadlock on the first lazy application.
	std::mutex                         GIniMutex;
	bool                               bIniScanned = false;
	std::map<std::string, std::string> GIniValues;

	/** Parse `Config/DefaultEngine.ini` into `[ConsoleVariables]` key/value pairs. The path is
	 *  relative on purpose -- the same thing Config::Initialize loads (codegen stages Config/ next
	 *  to the binary, and the process runs there). */
	void ScanIniFile()
	{
		std::ifstream Stream{ "Config/DefaultEngine.ini" };
		if (!Stream)
		{
			return;
		}

		const std::string WantedSection = "ConsoleVariables";
		std::string       CurrentSection;
		std::string       Line;
		while (std::getline(Stream, Line))
		{
			const std::string Trimmed = Trim(Line);
			if (Trimmed.empty() || Trimmed.front() == ';' || Trimmed.front() == '#')
			{
				continue;
			}

			if (Trimmed.front() == '[' && Trimmed.back() == ']')
			{
				CurrentSection = Trim(Trimmed.substr(1, Trimmed.size() - 2));
				continue;
			}

			const std::size_t Eq = Trimmed.find('=');
			if (Eq == std::string::npos || CurrentSection != WantedSection)
			{
				continue;
			}
			GIniValues[Trim(Trimmed.substr(0, Eq))] = Trim(Trimmed.substr(Eq + 1));
		}
	}

	/** The ini value for one CVar name, or nullopt when the file / section / key is absent.
	 *  Returns BY VALUE: the cache can be reset by Shutdown, and a returned reference would
	 *  then dangle for whoever is holding it. Thread-safe (memoized under GIniMutex). */
	[[nodiscard]] std::optional<std::string> LookupIniValue(std::string_view Key)
	{
		std::lock_guard<std::mutex> Lock(GIniMutex);
		if (!bIniScanned)
		{
			ScanIniFile();
			bIniScanned = true;
		}
		const auto It = GIniValues.find(std::string(Key));
		if (It == GIniValues.end())
		{
			return std::nullopt;
		}
		return It->second;
	}

	/** A new registry (a re-Initialize after Shutdown) reads the file afresh. */
	void ResetIniCache()
	{
		std::lock_guard<std::mutex> Lock(GIniMutex);
		bIniScanned = false;
		GIniValues.clear();
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
			ApplyConfigOnce();
			std::lock_guard<std::mutex> Lock(ValueMutex);
			try { return std::stoi(Value); }
			catch (...) { return 0; }
		}

		[[nodiscard]] float GetFloat() const override
		{
			ApplyConfigOnce();
			std::lock_guard<std::mutex> Lock(ValueMutex);
			try { return std::stof(Value); }
			catch (...) { return 0.0f; }
		}

		[[nodiscard]] bool GetBool() const override
		{
			ApplyConfigOnce();
			std::lock_guard<std::mutex> Lock(ValueMutex);
			const std::string Lower = ToLower(Value);
			return Lower == "true" || Lower == "1" || Lower == "yes" || Lower == "on";
		}

		[[nodiscard]] std::string GetString() const override
		{
			ApplyConfigOnce();
			std::lock_guard<std::mutex> Lock(ValueMutex);
			return Value;
		}

		void Set(std::string_view InValue) override
		{
			if (HasFlag(Flags, ECVarFlags::ReadOnly))
			{
				// Settle the startup value first: what is being refused is the RUNTIME write, not the
				// config-file value. (So the variable is never left "not yet configured" merely
				// because somebody tried to write it.)
				ApplyConfigOnce();
				ReportRejectedSet(InValue);
				return;
			}
			// Consume the lazy config BEFORE writing, so the order of "the first read" and "an
			// explicit Set" cannot matter: an explicit write is always the last word.
			ApplyConfigOnce();
			std::lock_guard<std::mutex> Lock(ValueMutex);
			Value = std::string(InValue);
		}

	private:
		/** Apply this variable's `[ConsoleVariables]` value from `Config/DefaultEngine.ini`
		 *  the FIRST time its value is touched, and never again (`std::call_once` is the
		 *  memoization, and its synchronization is also what makes the plain `Value` write below
		 *  safe: every reader calls this BEFORE locking `ValueMutex`, so no reader can observe
		 *  the write in progress). Absent in the ini -> the registered default stands. */
		void ApplyConfigOnce() const
		{
			// call_once's callable is invoked from a const context; the memoized payload is this
			// variable's own value, so the cast is to the object we are already inside of.
			FCVarEntry* const Self = const_cast<FCVarEntry*>(this);
			std::call_once(ConfigOnce, [Self]()
			{
				if (const std::optional<std::string> IniValue = LookupIniValue(Self->Name))
				{
					// NOT via Set(): a config-file value is startup configuration, so
					// `ECVarFlags::ReadOnly` (which gates RUNTIME writes) is not a gate here.
					Self->Value = *IniValue;
				}
			});
		}

		/** ReadOnly write: refuse, and leave a durable record -- ONCE per variable (a console
		 *  spamming the same refused `Set` must not spam the log). `exchange` makes the
		 *  once-only claim atomic across pool workers. */
		void ReportRejectedSet(std::string_view InValue) const
		{
			if (bSetRejected.exchange(true))
			{
				return;
			}
			char Message[512];
			std::snprintf(Message, sizeof(Message),
				"CVar '%s' is ReadOnly: Set(\"%.*s\") refused",
				Name.c_str(), static_cast<int>(InValue.size()), InValue.data());
			ReportError(Message);
		}

		std::string Name;
		ECVarType Type;
		std::string Value;
		std::string Description;
		ECVarFlags Flags;
		mutable std::mutex ValueMutex;   // guards Value against cross-thread read/write races
		mutable std::once_flag ConfigOnce;                       // the lazy ini application, once
		mutable std::atomic<bool> bSetRejected{ false };         // the ReadOnly report, once
	};
}

void FConsoleVariable::Shutdown(FEngineBase&, FEngineContext&)
{
	std::lock_guard<std::mutex> Lock(GMutex);
	Registry.clear();
	ResetIniCache();   // a later registry re-reads the file instead of reusing this run's parse
}

IConsoleVariable* FConsoleVariable::Find(std::string_view Name)
{
	std::lock_guard<std::mutex> Lock(GMutex);
	const auto It = Registry.find(std::string(Name));
	return It != Registry.end() ? It->second.get() : nullptr;
}

void FConsoleVariable::VisitAll(const std::function<void(IConsoleVariable&)>& Visitor) const
{
	std::lock_guard<std::mutex> Lock(GMutex);
	for (const auto& Item : Registry)
	{
		if (Item.second)
		{
			Visitor(*Item.second);
		}
	}
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
