// TEMPORARY verification harness (driven by MAHO_TEST_CVAR) for the
// ConsoleVariable changes:
//   1. lazy `Config/DefaultEngine.ini` [ConsoleVariables] application on first
//      read, memoized per variable;
//   2. `ECVarFlags::ReadOnly` refusing Set/SetByString AND REPORTING it once;
//   3. the registry being ONE instance across DLLs (the export tag).
//
// Two of the checks below are about crossings that only a DLL boundary can
// expose, which is why this lives in the project's own plugin:
//   - `r.Trace` is registered by FLog.dll's static, so finding it from
//     FTaskGraphTest.dll proves both DLLs share one registry;
//   - `r.Window.Width` is declared HERE with Platform's real default (1280)
//     while `Config/DefaultEngine.ini` says 100, so "the ini won" is visible
//     without installing the Config layer at all.

#include <ConsoleVariable.h>

#include <cstdio>
#include <cstdlib>

namespace Maho
{

namespace
{
	using namespace ConsoleVariable;

	int GFailures = 0;

	void Check(const char* What, bool bOk)
	{
		std::printf("  %s %s\n", bOk ? "ok  " : "FAIL", What);
		if (!bOk)
		{
			++GFailures;
		}
		std::fflush(stdout);
	}

	// Registered defaults. r.Window.Width mirrors Platform.cpp:142 (1280) so the ini's 100 is a
	// DIFFERENT, observable value.
	static TAutoConsoleVariable<int> GTestWindowWidth("r.Window.Width", 1280, "temp verify: ini-driven width");
	static TAutoConsoleVariable<int> GTestNoIniEntry("r.Test.NoIniEntry", 33, "temp verify: no ini entry -> default");
	static TAutoConsoleVariable<int> GTestReadOnly("r.Test.ReadOnly", 7, "temp verify: refused Set", ECVarFlags::ReadOnly);
	static TAutoConsoleVariable<int> GTestReadOnlyIni("r.Test.ReadOnlyIni", 7, "temp verify: ini still applies", ECVarFlags::ReadOnly);
}

int RunConsoleVariableConfigTest()
{
	std::printf("[cvar] ConsoleVariable lazy-config / ReadOnly verification\n");
	FConsoleVariable& Registry = FConsoleVariable::Get();

	// (3) CROSS-DLL REGISTRY: r.Trace is declared in FLog.dll, this caller is FTaskGraphTest.dll.
	// A per-module registry would answer nullptr here.
	{
		IConsoleVariable* Other = Registry.Find("r.Trace");
		Check("r.Trace (declared in FLog.dll) is visible from FTaskGraphTest.dll", Other != nullptr);

		IConsoleVariable* Mine = Registry.Find("r.Window.Width");
		Check("r.Window.Width (declared here) is registered", Mine != nullptr);
	}

	// (2) LAZY INI: first read applies Config/DefaultEngine.ini [ConsoleVariables].
	{
		IConsoleVariable* Width = Registry.Find("r.Window.Width");
		if (Width != nullptr)
		{
			const int First = Width->GetInt();
			std::printf("[cvar] r.Window.Width: default=1280, ini=100, GetInt()=%d\n", First);
			Check("r.Window.Width reads the ini value (100), not the registered default (1280)", First == 100);
			Check("r.Window.Width is memoized (second read is identical)", Width->GetInt() == First);
			std::printf("[cvar] r.Window.Width GetString()=\"%s\"\n", Width->GetString().c_str());
			Check("r.Window.Width GetString() agrees with GetInt()", Width->GetString() == "100");
		}

		IConsoleVariable* NoIni = Registry.Find("r.Test.NoIniEntry");
		if (NoIni != nullptr)
		{
			std::printf("[cvar] r.Test.NoIniEntry (absent from the ini) GetInt()=%d\n", NoIni->GetInt());
			Check("a name absent from the ini keeps its registered default (33)", NoIni->GetInt() == 33);
		}
	}

	// (1) ReadOnly: Set AND SetByString are refused -- exactly one report for the pair.
	{
		IConsoleVariable* ReadOnly = Registry.Find("r.Test.ReadOnly");
		if (ReadOnly != nullptr)
		{
			std::printf("[cvar] r.Test.ReadOnly: Set(\"9\") then SetByString(\"9\")\n");
			ReadOnly->Set("9");
			ReadOnly->SetByString("9");
			const int After = ReadOnly->GetInt();
			std::printf("[cvar] r.Test.ReadOnly after the refused writes: %d (\"%s\")\n",
				After, ReadOnly->GetString().c_str());
			Check("ReadOnly: Set was refused (value still 7)", After == 7);
			Check("ReadOnly: value string untouched", ReadOnly->GetString() == "7");
			Check("ReadOnly flag is reported by GetFlags()",
				HasFlag(ReadOnly->GetFlags(), ECVarFlags::ReadOnly));
		}

		// The ini is startup configuration, so ReadOnly does NOT block it (only a runtime Set).
		IConsoleVariable* ReadOnlyIni = Registry.Find("r.Test.ReadOnlyIni");
		if (ReadOnlyIni != nullptr)
		{
			ReadOnlyIni->Set("99");   // refused + reported
			const int IniValue = ReadOnlyIni->GetInt();
			std::printf("[cvar] r.Test.ReadOnlyIni: Set(\"99\") refused, ini value reads %d\n", IniValue);
			Check("ReadOnly still picks up its ini value (21)", IniValue == 21);
		}
	}

	std::printf("[cvar] %s (%d failure(s))\n", GFailures == 0 ? "PASS" : "FAIL", GFailures);
	std::fflush(stdout);
	return GFailures;
}

} // namespace Maho
