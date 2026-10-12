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

void FEngineBase::PostMain()
{
	// The graph this teardown drains is THIS host's (a member -- see the header). Ensuring it here is
	// idempotent and it covers the host that never ticked (a build/test that installs and immediately
	// tears down): the shutdown sweep below drives one-shot batches, and those need a graph. The
	// reference is what every driving call below is handed -- the builder stores no graph of its own.
	FFrameGraph& HostGraph = EnsureGraph();

	// Teardown never loads: Install / Reload are refused from here on (the collector's
	// closing flag). Main already flipped it through the exit request that ended its
	// loop; closing again is idempotent (and OnClosing only ever broadcasts once).
	CloseForLoads();

	// A load/reload queued by the last frame is dropped: release the instance AND the
	// module it already loaded (the load itself happened back in Install()).
	DropPendingInstalls();
	CancelPendingReloads();

	// Uninstall EVERY live frame -- not just the catalog's TopLevel list: sub-plugins come and
	// go at runtime, and a frame left behind keeps its DLL loaded, which the process teardown
	// would then free under a live module. (The catalog is still loaded from PreMain, and the
	// sub-plugin lookup below needs it.) By NAME, because that is what survives a module.
	// The count is deliberately dropped (`(void)`): this sweep uninstalls EVERYTHING, so the number
	// answers nothing the caller could act on -- but it is `[[nodiscard]]` on purpose, because a
	// caller that expected a smaller sweep should notice (C4834 is that notice).
	(void)UninstallAll();

	// Apply the shutdown stages, then repeat while they request more (a parent's Shutdown
	// uninstalls its sub-plugins). Bounded: a dependency cycle must not spin.
	for (int Pass = 0; Pass < 8 && GetStats().PendingRemoves != 0; ++Pass)
	{
		FlushPendingUpdates<FInitStages, FShutdownStages>(HostGraph);
	}

	const FFrameStats Stats = GetStats();
	if (Stats.PendingRemoves != 0 || Stats.Active != 0)
	{
		ReportError((std::string("PostMain: frames left alive after teardown (pending=")
			+ std::to_string(Stats.PendingRemoves)
			+ ", active=" + std::to_string(Stats.Active)
			+ ") -- a shutdown dependency edge is missing").c_str());
	}

	// THE OWNER'S DRAIN -- and it is the host's, not the builder's: ~FFrameBuilder only flushes the
	// pool now. The invariant "the graph AND the pool are quiet before any instance or module is
	// released" is exactly this call: every frame above was uninstalled through batches that drained
	// on the way, so what is left is this fence.
	//
	// ONE call, not the old `DrainGraph(); ShutdownGraph();` pair: the graph is a MEMBER here, so
	// stopping its scheduler thread is no longer a separate step -- `~FFrameGraph` does it (it calls
	// FThreadedServer::Shutdown while its own node table is still alive), and that destructor runs
	// BEFORE the pool it was built with, because members die before base classes. There is nothing
	// left for this layer to release explicitly.
	if (bGraphReady)
	{
		Graph->Wait();
	}

	// (Any diagnostic tail -- e.g. flushing the CPU trace file -- is the OWNER's business: the Log
	// plugin flushes its own sinks in FLog::Shutdown, which the shutdown stages order before the
	// engine's teardown finishes.)
}

int FEngineBase::Main()
{
	// PreMain already installed AND initialized every frame (its
	// FlushPendingUpdates drives the init stages), so the loop only runs frames. The host's
	// whole loop is Execute() + FlushPendingUpdates() + its own exit check: the scheduler, the
	// batch builder and the stage dispatch are FFrameBuilder's private implementation.
	//
	// FRAMES PIPELINE: nothing here drains a frame. That is safe because the batch builder emits
	// the two STRUCTURAL edges a stage sequence implies -- a frame's own stages in order, and
	// each stage against its OWN previous frame (same extension, same stage). The second one is
	// what used to be the per-layer gate, now derived from the node identity instead of enforced
	// by a scheduler mechanism, and it is strictly finer: it orders a stage against itself, not a
	// whole frame against itself, so different frames overlap wherever they do not share a stage.
	//
	// What is NOT covered by that edge, and must NOT be "fixed" by serializing frames here: the two
	// stages of one frame extension that share its frame state across a frame boundary (S1 of frame
	// N+1 vs S3 of frame N). A frame extension that owns per-frame state is expected to hold
	// MAHO_FRAMES_IN_FLIGHT copies of it -- that is the render layer's job, and doing it in the
	// scheduler instead would hide the missing copies (measured: the Vulkan validation errors in
	// design.md D9).
	// The graph this loop submits every frame to. `PreMain` (the host's override) is the usual first
	// caller -- it drives the install flush through it -- and this call is the one that has to hold
	// for a host that drives NO flush of its own. Idempotent either way, and the reference is handed to
	// every driving call below: the builder holds no graph, so "which graph this loop drives" is stated
	// here, once, instead of being stored inside the scheduler-adjacent machinery.
	FFrameGraph& TickGraph = GetGraph();

	// The stage-metadata table's one-shot dump (change add-resource-metadata-edges, step 2). HERE, and
	// not earlier: every row is registered at static-init time inside the plugin that owns the
	// declaration, so the table is complete only once those plugins are loaded -- and `PreMain` (the
	// host's override) is what loads them. The names in the rows are string literals inside those
	// modules, so the dump has to happen while they are mapped -- which is also why it is not left to
	// process exit. Off unless `MAHO_GRAPH_META` is set (the `MAHO_GRAPH_*` diagnostic family).
	DumpStageAccess();

	// The metadata store's one-shot dump (change add-resource-metadata-edges, task 2'6): every type it
	// holds -- the installed frames, the stage protocols, the declared resources -- on the same terms and
	// at the same point, after `PreMain` loaded the plugins, while their literal values are mapped. Off
	// unless `MAHO_METADATA` is set.
	GetMetaData().DumpOnce();

	while (!ShouldExit())
	{
		// ONE read of the frame number per frame, and the SAME value goes to every sequence this
		// frame drives: the tick batch below, and (in a host that overrides NextFrameNumber from
		// Platform's canonical counter) whatever else the frame publishes. Two reads could straddle
		// a frame boundary and split one frame across two rounds of the ring.
		const std::uint64_t Frame = NextFrameNumber();

		FlushPendingUpdates<FInitStages, FShutdownStages>(TickGraph);
		Execute<FTickStages>(TickGraph, Frame);
	}

	// Quiescence before teardown, and it has to cover BOTH the frames and the pool: stage methods
	// submit their own async work (nested graph work, asset/RHI helpers) that no frame fence
	// tracks, and PostMain is about to free plugin DLLs -- a module must never be unloaded under
	// running code. The graph comes from the owner (this), not from the builder: the builder holds
	// no graph (see FFrameBuilder). Two halves, spelled out: the graph's submission fence, then
	// this host's own pool lane.
	if (bGraphReady)
	{
		Graph->Wait();
	}
	DrainPool();

	return 0;
}

void FEngineBase::RequestExit()
{
	// Exit is the OTHER end of the loop, and it is reached from an `IExit` stage -- i.e. from inside
	// a node of this host's graph. Ensuring it here keeps the one invariant the teardown relies on
	// (`Graph` exists as soon as anything graph-shaped has been asked of this host) without any path
	// being able to skip it; on the normal path it is a no-op.
	EnsureGraph();

	// The collector owns the transition (and the OnClosing broadcast that goes with it).
	CloseForLoads();
}

std::uint64_t FEngineBase::NextFrameNumber()
{
	// The default frame source: this loop's own monotonic counter (see the header). A host with a
	// real frame source -- the project entry point, reading Platform's canonical index -- overrides
	// it and this counter is never read.
	return LoopFrame++;
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
