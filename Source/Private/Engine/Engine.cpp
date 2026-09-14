#include <Engine/Engine.h>

#include <CLI/CLI.hpp>

#include <algorithm>
#include <string>
#include <vector>

#ifndef NDEBUG
#	include <fstream>
#endif

namespace Maho
{

FEngineBase::FEngineBase() = default;

FEngineBase::~FEngineBase()
{
	// TEMP: what is still owned when the engine object starts dying (its Features /
	// Modules members die right after this body).
	std::size_t Live = 0;
	for (const auto& Feature : Features)
	{
		if (Feature)
		{
			Live += 1;
		}
	}
	TraceTeardown((std::string("~FEngineBase: live features=") + std::to_string(Live)
		+ " module slots=" + std::to_string(Modules.size())).c_str());
}

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

void FEngineBase::PostMain()
{
	TraceTeardown("PostMain enter");
	// Teardown never loads: Install / Reload are refused from here on (the collector's
	// closing flag), so a shutdown stage cannot pull a module in while the engine goes
	// down -- and the loop below can only ever see removals. Main already set the flag
	// through the exit request that ended its loop; setting it again keeps PostMain
	// correct on its own.
	bClosing.store(true, std::memory_order_release);

	// A load/reload queued by the last frame is dropped: release the instance AND the
	// module it already loaded (the load itself happened back in Install()).
	DropPendingInstalls();
	PendingReloads.clear();

	// Uninstall EVERY live layer -- not just the catalog's TopLevel list: sub-plugins
	// come and go at runtime, and a layer left behind keeps its DLL loaded, which the
	// process teardown would then free under a live module. (The catalog is still
	// loaded from PreMain, and the sub-plugin lookup below needs it.)
	for (FLayerBase* Layer : Pipelines)
	{
		TryUninstall(Layer->GetName());
	}
	TraceTeardown((std::string("PostMain: uninstalls requested pipelines=") + std::to_string(Pipelines.size())
		+ " requests=" + std::to_string(PendingRemoveRequests.size())).c_str());

	// Apply the shutdown stages, then repeat while they request more (a parent's
	// Shutdown uninstalls its sub-plugins). Bounded: a dependency cycle must not spin.
	for (int Pass = 0; Pass < 8 && !PendingRemoveRequests.empty(); ++Pass)
	{
		TraceTeardown("PostMain: flush pass");
		FlushPendingUpdatePipelines<
			TTypeList<IPreInit, IInit, IPostInit>,
			TTypeList<IPreShutdown, IShutdown, IPostShutdown>
		>();
	}

	// No plugin code may still be running when the host frees the DLLs: stage methods
	// (the shutdown stages included) may have submitted work to the pool themselves.
	TraceTeardown("PostMain: Pool.Flush in");
	Pool.Flush();
	TraceTeardown("PostMain: done");

	if (!PendingRemoveRequests.empty() || !Pipelines.empty())
	{
		ReportError((std::string("PostMain: layers left alive after teardown (pending=")
			+ std::to_string(PendingRemoveRequests.size())
			+ ", active=" + std::to_string(Pipelines.size())
			+ ") -- a shutdown dependency edge is missing").c_str());
	}
}

int FEngineBase::Main()
{
	// PreMain already installed AND initialized every layer (its
	// FlushPendingUpdatePipelines drives the init stages), so Main only schedules the
	// tick pipeline -- an init graph here would Initialize every layer twice.
	//
	// The tick graph is CACHED across frames (re-expanded only when OnLayersChanged
	// fires) and frames PIPELINE: SubmitFrame overlaps up to MAHO_FRAMES_IN_FLIGHT
	// frames instead of draining the pool every frame.
	using FTickStages = TTypeList<IBeginFrame, ITick, IEndFrame, IExit>;
	FLayerTaskGraph<FTickStages, FEngineBase> EngineGraph(Pool, *this);
	std::string LastCompileErrorNode;
	OnLayersChanged.Bind([this]() { bLayersDirty = true; });
	while (true)
	{
		// Frame submission is NOT a barrier: SubmitFrame waits only for the ring slot it
		// is about to reuse, so up to MAHO_FRAMES_IN_FLIGHT frames are in flight. Cross-
		// frame safety comes from the per-layer gate in the graph (a layer never overlaps
		// its own previous frame), which also tolerates a stray sink report by advancing
		// the chain instead of wedging the layer.

		// A queued install/uninstall/reload is a TOPOLOGY change: the rebuild below
		// replaces the graph's node storage, so it may only run with the graph idle.
		if (!PendingAdded.empty() || !PendingRemoveRequests.empty() || !PendingReloads.empty())
		{
			FlushPendingUpdatePipelines<TTypeList<IPreInit, IInit, IPostInit>, TTypeList<IPreShutdown, IShutdown, IPostShutdown>>();
		}

		if (bLayersDirty)
		{
			bLayersDirty = false;
			EngineGraph.WaitAll();   // the rebuild replaces the node storage
			EngineGraph.Init(Select<IBeginFrame, ITick, IEndFrame, IExit>());
			if (!EngineGraph.Compile())
			{
				// Report ONCE per distinct breakage (no per-frame spam) and leave a
				// known-empty graph so SubmitFrame() is a no-op. The broken layer is not
				// scheduled until the topology changes (retried then).
				const std::string Bad = EngineGraph.GetCompileErrorNode();
				if (Bad != LastCompileErrorNode)
				{
					LastCompileErrorNode = Bad;
					ReportError((std::string("layer graph compile failed (missing dependency or cycle); "
						"layer '") + Bad + "' is not scheduled until the topology is fixed").c_str());
				}
				EngineGraph.Init({});   // empty graph -> SubmitFrame() is a no-op
			}
		}

		// NOT a barrier: it waits only for the ring slot it is about to reuse, so up to
		// K frames are in flight. Cross-frame safety comes from the per-layer gate in
		// the graph (a layer never overlaps its own previous frame), not from here.
		EngineGraph.SubmitFrame();

		// An input layer (e.g. GameInputLayer) calls RequestExit inside Tick. The
		// current frame may still be executing -- the tail WaitAll() drains it.
		if (ShouldExit())
		{
			break;
		}
	}

	// Quiescence before teardown, and it has to cover BOTH:
	//  - the graph: every in-flight frame must finish (PostMain uninstalls layers,
	//    which must not race a stage method still executing);
	//  - the pool: stage methods submit their own async work (nested graph work,
	//    asset/RHI helpers) that no frame fence tracks. PostMain is about to free
	//    plugin DLLs -- a module must never be unloaded under running code.
	EngineGraph.WaitAll();
	Pool.Flush();

#ifndef NDEBUG
	// Debug-only evidence that frames really overlap (look next to the executable).
	{
		std::ofstream Out("TaskGraphStats.txt", std::ios::app);
		Out << "tick graph: frames=" << EngineGraph.GetSubmittedFrames()
			<< " max in flight = " << EngineGraph.GetMaxFramesInFlight()
			<< " (ring depth " << EngineGraph.GetRingDepth() << ")\n";
	}
#endif

	return 0;
}

void FEngineBase::RequestExit()
{
	bClosing.store(true, std::memory_order_release);
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
