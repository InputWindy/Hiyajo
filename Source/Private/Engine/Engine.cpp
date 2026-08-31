#include <Engine/Engine.h>

#include <CLI/CLI.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace Maho
{

FEngineBase::FEngineBase() = default;

FEngineBase::~FEngineBase() = default;

void FEngineBase::ParseCommandLine(int Argc, char** Argv)
{
	// Normalize each "-key" (or "-key=value") to a "--key" long-option so CLI11
	// handles single-dash names without interpreting them as short-flag chains.
	std::vector<std::string> Normalized;
	Normalized.reserve(static_cast<std::size_t>(Argc) + 1);
	Normalized.push_back("maho");   // program name slot CLI11 consumes
	for (int I = 1; I < Argc; ++I)
	{
		const std::string Arg = Argv[I];
		if (Arg.size() < 2 || Arg[0] != '-')
		{
			continue;   // positional/non-hyphen junk - ignore
		}
		if (Arg[1] == '-')
		{
			// already a long option; keep as-is
			Normalized.push_back(Arg);
			continue;
		}

		// single-dash -> long-option. Bind the value inline so we never drop it.
		std::string Body = Arg.substr(1);
		if (Body.find('=') != std::string::npos)
		{
			Normalized.push_back("--" + Body);   // --key=value
			continue;
		}

		if (I + 1 < Argc && Argv[I + 1][0] != '-')
		{
			Normalized.push_back("--" + Body + "=" + Argv[I + 1]);   // --key value
			++I;
		}
		else
		{
			Normalized.push_back("--" + Body + "=true");   // bare flag -> true
		}
	}

	CLI::App App;
	App.allow_extras();

	// Discover unique option names so we can declare one CLI11 option per key.
	// CLI11 does the real tokenization / quoted-value work.
	std::vector<std::string> Keys;
	for (const std::string& Arg : Normalized)
	{
		if (Arg.size() < 2 || Arg[0] != '-')
		{
			continue;
		}
		std::string Sig = Arg[1] == '-' ? Arg.substr(2) : Arg.substr(1);
		const std::size_t Eq = Sig.find('=');
		if (Eq != std::string::npos)
		{
			Sig = Sig.substr(0, Eq);
		}
		Keys.push_back(Sig);
	}
	std::sort(Keys.begin(), Keys.end());
	Keys.erase(std::unique(Keys.begin(), Keys.end()), Keys.end());

	for (const std::string& Key : Keys)
	{
		// every discovered flag now carries a value (=true for bare flags), so a
		// single expected value binding is deterministic.
		App.add_option("--" + Key)->expected(1);
	}

	try
	{
		App.parse(Argc, Argv);
	}
	catch (const CLI::ParseError& E)
	{
		// Parse errors are non-fatal: log + continue with whatever was parsed.
		(void)E;
	}

	// Read the parsed results back into the KV store.
	for (const std::string& Key : Keys)
	{
		CLI::Option* Opt = App.get_option_no_throw("--" + Key);
		if (Opt && Opt->count() > 0 && !Opt->results().empty())
		{
			Store[Key] = Opt->results().front();
		}
	}
}

int FEngineBase::Main()
{
	// Merge the layers installed in PreMain into Pipelines. Their Initialize is
	// driven by the InitGraph below (do NOT call FlushPendingUpdatePipelines here,
	// which would init them twice).
	for (FLayerBase* P : PendingAdded)
	{
		Pipelines.push_back(P);
	}
	PendingAdded.clear();

	// Init pipeline: drive once with a temporary graph; layers implementing the
	// init stage interfaces Initialize in dependency order.
	using FInitStages = TTypeList<IPreInit, IInit, IPostInit>;
	FLayerTaskGraph<FInitStages, FEngineBase> InitGraph(Pool, *this);
	InitGraph.Init(Select<IPreInit, IInit, IPostInit>());
	if (!InitGraph.Compile())
	{
		ReportFatal("FEngineBase::Main: init pipeline Compile failed (missing dependency or cycle)");
	}
	InitGraph.Execute();
	InitGraph.Flush();

	// Tick pipeline: the main loop.
	using FTickStages = TTypeList<IBeginFrame, ITick, IEndFrame, IExit>;
	FLayerTaskGraph<FTickStages, FEngineBase> EngineGraph(Pool, *this);
	while (true)
	{
		EngineGraph.Flush();
		FlushPendingUpdatePipelines<IPreInit, IInit, IPostInit>();

		EngineGraph.Init(Select<IBeginFrame, ITick, IEndFrame, IExit>());
		if (!EngineGraph.Compile())
		{
			ReportFatal("FEngineBase::Main: tick pipeline Compile failed (missing dependency or cycle)");
		}
		EngineGraph.Execute();

		// An input layer (e.g. GameInputLayer) calls RequestExit inside Tick;
		// this frame's Execute has already finished, so check the exit flag here safely.
		if (bIsShuttingDown.load(std::memory_order_acquire))
		{
			EngineGraph.Flush();
			break;
		}
	}

	// Shutdown pipeline: drive once with a temporary graph; layers implementing
	// the shutdown stage interfaces Shutdown in dependency order.
	using FShutdownStages = TTypeList<IPreShutdown, IShutdown, IPostShutdown>;
	FLayerTaskGraph<FShutdownStages, FEngineBase> ShutdownGraph(Pool, *this);
	ShutdownGraph.Init(Select<IPreShutdown, IShutdown, IPostShutdown>());
	if (!ShutdownGraph.Compile())
	{
		ReportFatal("FEngineBase::Main: shutdown pipeline Compile failed (missing dependency or cycle)");
	}
	ShutdownGraph.Execute();
	ShutdownGraph.Flush();

	// Delete feature instances first (virtual dtors live in their own DLLs), then release the DLLs.
	Features.clear();
	Modules.clear();

	return 0;
}

void FEngineBase::RequestExit()
{
	bIsShuttingDown.store(true, std::memory_order_release);
}

bool FEngineBase::Has(std::string_view Key) const
{
	return Store.find(std::string(Key)) != Store.end();
}

std::string FEngineBase::Get(std::string_view Key) const
{
	auto It = Store.find(std::string(Key));
	return It != Store.end() ? It->second : std::string{};
}

bool FEngineBase::GetBool(std::string_view Key) const
{
	const std::string Value = Get(Key);
	return Value == "true" || Value == "1" || Value == "yes" || Value == "on";
}

int FEngineBase::GetInt(std::string_view Key) const
{
	const std::string Value = Get(Key);
	if (Value.empty())
	{
		return 0;
	}
	try
	{
		return std::stoi(Value);
	}
	catch (...)
	{
		return 0;
	}
}

} // namespace Maho
