#pragma once

#include <Core/Assembly.h>
#include <Core/Delegate.h>
#include <Core/Fatal.h>
#include <Core/FrameGraph.h>
#include <Engine/Frame.h>
#include <Engine/PluginManager.h>
#include <Engine/Query.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Maho
{

namespace Detail
{
	/**
	 * Format + report the declarations the bridge could not resolve. Free rather than a member
	 * because it is STATELESS: every collection member that is not a template still reads the
	 * collection's own state, so it cannot be defined out of line without the class template
	 * being explicitly instantiated -- and that is impossible for plugin contexts, since the
	 * engine module must not include plugin headers. Stateless ones move here instead.
	 */
	void ReportBridgeDiagnostics(const std::vector<FFrameBridge::FDiagnostic>& Diagnostics);
}

/**
 * A frame that is ITSELF a collector: it drives a set of sub-frames through its own FFrameBuilder,
 * so it needs its own task LANE even when it shares the parent's pool.
 *
 * Why an interface rather than a base class: install is generic. The parent creates the child
 * through the DLL factory and only ever holds it as an FFrameExtension, yet a collector child has
 * to be handed the shared pool before its first Execute. A side cast to this interface is that
 * hand-off -- and it is the ONLY thing that has to know which frames are collectors.
 *
 * The design rule this encodes, stated once: a collector's stage bodies are dispatched by its
 * PARENT's graph, hence they run on the parent's lane, while its Wait() flushes ITS OWN lane. So
 * "the caller's task is not in the count it is waiting for" holds by construction -- which is what
 * a barrier called from inside a pool task requires, and what one pool per collector used to buy
 * by duplicating threads instead.
 */
class MAHO_API IFrameCollector
{
public:
	virtual ~IFrameCollector();

	/** Take the given pool as this collector's task pool, plus a lane of its own within it.
	 *  Called by the installing parent; never after the collector's first Execute (the graph that
	 *  holds the pool by reference does not exist yet, and that is the point of doing it here). */
	virtual void UseSharedPool(FThreadPool& InPool) = 0;
};

// ── FFrameBuilder: layer-collection management base ─────────────────────

/**
 * Owns + schedules a set of anonymous FFrameExtension instances. Install/Uninstall
 * are recorded into pending sets and applied at the FlushPendingUpdates
 * safe point; unload is dependency-safe (min-heap greedy). The init/tick/
 * shutdown stage lists are caller-supplied (FEngineBase uses the engine stages,
 * a domain subsystem like FRender uses its own).
 *
 * TContext is the scheduling context passed to every stage method (FEngineBase
 * for the engine, FRender for the render subsystem). It also supplies the
 * FQuery data source (GetQueryData -> Pipelines).
 */
template <typename TContext>
class FFrameBuilder
	: public virtual FQuery<FFrameExtension>
	, public IFrameCollector
{
public:

	/** Quiescence before anything is torn down. Order matters and it is the whole reason this
	 *  destructor body exists: Wait() stops the frame work (graph AND pool) while every member is
	 *  still alive, then the members die in reverse order -- Graph (which stops the scheduler
	 *  thread), then Features (destroys the frame instances, i.e. runs plugin destructors), then
	 *  Pool (joins the workers), then Modules (frees the DLLs). Features before Pool is not
	 *  cosmetic: a COLLECTOR child is destroyed there and its destructor flushes -- and releases
	 *  its lane in -- the shared pool, so that pool must still be alive. See the member note. */
	virtual ~FFrameBuilder()
	{
		Wait();

		// Hand the lane back so a collector installed/uninstalled repeatedly (reload) does not
		// leak one per cycle. Safe unconditionally because a shared pool is always outlived by the
		// instances that share it (member order, above).
		if (Pool != &OwnedPool)
		{
			Pool->DestroyLane(Lane);
		}
	}

	/** IFrameCollector: share the installing parent's task pool and take a lane of our own in it.
	 *  Called from Install, i.e. before this collector exists as far as the graph is concerned. */
	void UseSharedPool(FThreadPool& InPool) override
	{
		// The parent's pool replaces ours entirely -- OwnedPool simply never gets workers (the
		// pool is lazy), so nothing is doubled by having the member.
		Pool = &InPool;
		Lane = InPool.CreateLane();
	}

	/** EVERY terminal state of an install / uninstall / reload operation. Grouped by
	 *  direction so a consumer can switch once (see OnFrameStatus). */
	enum class EFrameStatus : std::uint8_t
	{
		// -- install --
		InstallQueued,          // accepted into PendingAdded; Init runs at the next safe point
		InstallRefused,         // not accepted -- Detail says why (closing / load / factory / duplicate name)
		InstallCompileFailed,   // Init graph Compile failed; the batch was RELEASED (instance +
		                        // module) and reported -- the caller decides whether to retry by
		                        // calling Install() again (nothing is retried automatically, and
		                        // nothing is left half-alive)
		Installed,              // Init stages ran; the layer is active
		InstallCancelled,       // an uninstall request arrived first: the loaded module was released
		                        // without ever being initialized
		// -- uninstall --
		UninstallQueued,        // recorded; applied at the next safe point
		UninstallNotFound,      // the query matched nothing (neither active name nor module path)
		UninstallRefused,       // still depended on -- Detail lists the dependents
		UninstallCompileFailed, // Shutdown graph Compile failed; the layers stay ALIVE (a layer is
		                        // never destroyed without its Shutdown stages having run)
		Uninstalled,            // Shutdown stages ran; instance + module released
		// -- reload --
		ReloadQueued,           // uninstall + fresh install queued
		ReloadRefused,          // Detail says why (no module path / still depended on / absent)
	};

	/** Payload of one status broadcast. All strings are COPIES: a layer's module may be
	 *  unloaded immediately after the call, so nothing here may point into it. */
	struct FFrameStatusInfo
	{
		EFrameStatus Status = EFrameStatus::InstallQueued;
		std::string  Name;     // the layer's name as stored by the collector (no vtable call)
		std::string  Path;     // the DLL path it was loaded from / would be loaded from
		std::string  Detail;   // why it was refused, who depends on it, which dep failed...
	};

	/** Broadcast whenever the active layer set changes at a safe point. The host
	 *  binds this to re-expand its cached task graph (push, not poll). */
	TMulticastEvent<void()> OnFramesChanged;

	/** Broadcast for EVERY terminal state above -- refused installs, cancelled installs,
	 *  refused uninstalls, run teardown, the lot. Nothing is silent any more.
	 *  OBSERVE ONLY: never call Install / Uninstall / Reload from a handler (ordering
	 *  across modules is the task graph's job, and re-entering a flush is a bug). */
	TMulticastEvent<void(const FFrameStatusInfo&)> OnFrameStatus;

	/** Broadcast ONCE when the collector flips to closing (RequestExit reached it): the
	 *  moment before teardown, while every layer is still alive. Observe only. */
	TMulticastEvent<void()> OnClosing;

	/** Current sizes, for tools / tests / panels (a query -- do not poll it per frame
	 *  from a broadcast). */
	struct FFrameStats
	{
		std::size_t Active = 0;          // layers in Pipelines
		std::size_t PendingAdds = 0;
		std::size_t PendingRemoves = 0;
		std::size_t PendingReloads = 0;
		std::size_t Modules = 0;         // module slots held (includes released ones)
	};

	[[nodiscard]] FFrameStats GetStats() const
	{
		return FFrameStats{ Pipelines.size(), PendingAdded.size(), PendingRemoveRequests.size(),
			PendingReloads.size(), Modules.size() };
	}

protected:
	/** Typed install: install a plugin by its layer type, resolving the DLL path
	 *  from T::GetModulePath() (the layer type knows its own module). Equivalent
	 *  to Install("T's dll"). Use this wherever the type is visible. */
	template <typename T>
	bool Install()
	{
		return Install(std::string(T::GetModulePath()));
	}

	/** Dynamically load a layer DLL via FAssembly and install it (next safe point).
	 *  Layers are ALWAYS loaded by name (anonymous-loading convention) -- there is
	 *  no raw-pointer install. Refuses, with a non-fatal error:
	 *    - a duplicate layer name (one instance per name);
	 *    - a declared dependency on a layer that is not already installed/pending
	 *      (deps first -- a failed install propagates to its dependents).
	 *  Load / symbol / factory failures are REPORTED, never silent.
	 *  Returns true on success. */
	bool Install(std::string_view DllPath, const char* FactorySymbol = "CreateFrame")
	{
		const std::string Path(DllPath);

		// Refused once the collection is closing: loading a module then is never right
		// (its stages would never run and its DLL would outlive the teardown order), so
		// it is refused LOUDLY instead of being silently dropped later.
		if (IsClosing())
		{
			const std::string Detail = "the collection is closing";
			ReportError((std::string("Install refused: ") + Detail + " (" + Path + ")").c_str());
			EmitStatus(EFrameStatus::InstallRefused, {}, Path, Detail);
			return false;
		}

		auto Asm = std::make_unique<FAssembly>(DllPath);
		if (!Asm->IsLoaded())
		{
			const std::string Detail = "failed to load the module";
			ReportError((std::string("Install refused: ") + Detail + ": " + Path).c_str());
			EmitStatus(EFrameStatus::InstallRefused, {}, Path, Detail);
			return false;
		}

		using CreateFn = FFrameExtension * (*)();
		auto Create = Asm->GetProcAs<CreateFn>(FactorySymbol);
		if (Create == nullptr)
		{
			const std::string Detail = std::string("module exports no '") + FactorySymbol + "'";
			ReportError((std::string("Install refused: ") + Detail + ": " + Path).c_str());
			EmitStatus(EFrameStatus::InstallRefused, {}, Path, Detail);
			return false;
		}

		// A plugin factory is plugin code: it may throw (a bad ctor, a failed global init).
		// Nothing is pushed into the parallel vectors before this point, so a throw here
		// leaves the collector exactly as it was -- report it as a refusal instead of
		// letting it escape into whatever drove the install.
		FFrameExtension* Raw = nullptr;
		try
		{
			Raw = Create();
		}
		catch (const std::exception& E)
		{
			const std::string Detail = std::string("factory threw: ") + E.what();
			ReportError((std::string("Install refused: ") + Detail + ": " + Path).c_str());
			EmitStatus(EFrameStatus::InstallRefused, {}, Path, Detail);
			return false;
		}
		catch (...)
		{
			const std::string Detail = "factory threw an unknown exception";
			ReportError((std::string("Install refused: ") + Detail + ": " + Path).c_str());
			EmitStatus(EFrameStatus::InstallRefused, {}, Path, Detail);
			return false;
		}

		auto Layer = std::unique_ptr<FFrameExtension>(Raw);
		if (!Layer)
		{
			const std::string Detail = "factory returned null";
			ReportError((std::string("Install refused: ") + Detail + ": " + Path).c_str());
			EmitStatus(EFrameStatus::InstallRefused, {}, Path, Detail);
			return false;
		}

		// Copy the name ONCE, into collector-owned storage: from here on the collector can
		// match / report this layer without a virtual call into its module -- which may be
		// released long before the parallel slot vectors are.
		const std::string Name(Layer->GetName());

		// One instance per name -- a duplicate would silently shadow the old one.
		if (HasLayerName(Name))
		{
			const std::string Detail = "a layer with this name is already active or pending";
			ReportError((std::string("Install refused: ") + Detail + ": '" + Name + "'").c_str());
			EmitStatus(EFrameStatus::InstallRefused, Name, Path, Detail);
			return false;
		}

		// No fail-fast dependency refusal here: features may legitimately reference
		// each other cross-stage in ANY install order (e.g. Scene.Present depends on
		// DrawTriangle.Render while DrawTriangle.Render depends on Scene.Render).
		// A per-name check cannot see that the cycle is valid across stages, so it
		// would refuse valid mutual deps. The stage-aware graph Compile validates
		// missing deps / real cycles instead (and reports once, non-fatal).

		// A child that is ITSELF a collector is handed OUR pool and takes its own lane in it, so
		// the whole tree runs on one worker set while each collector keeps an isolated barrier.
		// Install is generic and only knows the child as an FFrameExtension, so this side cast is
		// the one place that asks "are you a collector too?". Both ends are this same class:
		// whichever collector installs a collector hands down whatever pool it was given, so the
		// outermost builder's workers are the ones used all the way down.
		//
		// BEFORE the pushes, i.e. only once the child is definitely accepted: a refused install
		// must leave the caller with nothing to clean up.
		if (auto* Child = dynamic_cast<IFrameCollector*>(Layer.get()))
		{
			Child->UseSharedPool(*Pool);
		}

		PendingAdded.push_back(Layer.get());
		Modules.push_back(std::move(Asm));
		Features.push_back(std::move(Layer));
		ModulePaths.push_back(Path);
		LayerNames.push_back(Name);
		NameToSlot[Name] = Features.size() - 1;
		EmitStatus(EFrameStatus::InstallQueued, Name, Path);
		return true;
	}

	/** Hot reload: uninstall a layer (by name, dependency-safe) at the next safe
	 *  point, then re-install a fresh copy of its DLL the frame after (old
	 *  module freed before the new one loads). Refused + reported when the layer
	 *  is still depended on. */
	void Reload(std::string_view LayerName)
	{
		const std::string Query(LayerName);

		if (IsClosing())
		{
			const std::string Detail = "the collection is closing";
			ReportError((std::string("Reload refused: ") + Detail + " (" + Query + ")").c_str());
			EmitStatus(EFrameStatus::ReloadRefused, Query, {}, Detail);
			return;
		}

		for (FFrameExtension* L : Pipelines)
		{
			const std::string_view Name = StoredName(L);
			if (Name != LayerName)
			{
				continue;
			}
			// The collector owns the load lifecycle, so it stored the path at Install.
			for (std::size_t I = 0; I < Features.size(); ++I)
			{
				if (Features[I].get() != L)
				{
					continue;
				}
				const std::string Path = (I < ModulePaths.size()) ? ModulePaths[I] : std::string{};
				if (Path.empty())
				{
					const std::string Detail = "no module path recorded for this layer";
					ReportError((std::string("Reload refused: ") + Detail + ": " + Query).c_str());
					EmitStatus(EFrameStatus::ReloadRefused, Query, Path, Detail);
					return;
				}
				PendingReloads.emplace_back(Query, Path);
				RequestUninstall(L);
				EmitStatus(EFrameStatus::ReloadQueued, Query, Path);
				return;
			}
			const std::string Detail = "layer has no module to reload";
			ReportError((std::string("Reload refused: ") + Detail + ": " + Query).c_str());
			EmitStatus(EFrameStatus::ReloadRefused, Query, {}, Detail);
			return;
		}
		// Only ACTIVE layers can be reloaded: a layer whose install is still pending has no
		// old instance to unload, so the caller cancels it (TryUninstall) and installs
		// again if that is what it meant.
		std::string Detail = "no ACTIVE layer with that name";
		if (HasLayerName(Query))
		{
			Detail = "the layer's install is still PENDING; cancel it with TryUninstall and install again";
		}
		ReportError((std::string("Reload refused: ") + Detail + ": " + Query).c_str());
		EmitStatus(EFrameStatus::ReloadRefused, Query, {}, Detail);
	}

	/** Anonymous unload of ONE layer. Accepts a query identifying it, matching the FIRST
	 *  layer whose GetName() equals it (e.g. "FScene") OR whose installed DLL path
	 *  equals it (e.g. "EditorConsole.dll") -- the latter is symmetric with
	 *  Install("...dll"). A pointer-installed layer has no DLL path, so it matches only
	 *  by name. Ignored when absent (no error).
	 *
	 *  DIRECT layers only, mirroring InstallChildrenOf: each layer installs its own
	 *  children into its own collector and uninstalls them in its own Shutdown, so
	 *  uninstalling a parent must NOT walk the catalog tree below it. Doing that looked
	 *  up child names in THIS collector's active set, where a parent's children never
	 *  live (they live in the parent's collector) -- dead work at best, and a
	 *  name-collision hazard across collectors at worst (two instances of one layer type
	 *  in different collectors are legal here). */
	void TryUninstall(std::string_view Query)
	{
		const std::string Path(Query);

		// 1) Exact layer name -- matched through the collector's stored copy, so matching
		//    never calls into the layer's module.
		//    callers like GameWorld/Render pass a layer's name and must keep working.
		for (FFrameExtension* L : Pipelines)
		{
			if (StoredName(L) == Query)
			{
				RequestUninstall(L);
				EmitStatus(EFrameStatus::UninstallQueued, StoredName(L), Path);
				return;
			}
		}
		// 2) A layer whose install is still PENDING is addressable by name too -- this is
		//    how a caller takes back an install it just requested (the flush cancels it
		//    before the init batch runs; see FlushPendingUpdates).
		for (FFrameExtension* L : PendingAdded)
		{
			if (StoredName(L) == Query)
			{
				RequestUninstall(L);
				EmitStatus(EFrameStatus::UninstallQueued, StoredName(L), Path);
				return;
			}
		}
		// 3) DLL/module path (symmetry with Install("...dll")); ModulePaths is parallel to
		//    Features/Names and holds the exact string passed to Install. Index-based, so
		//    it reaches active and pending layers alike.
		for (std::size_t I = 0; I < Features.size(); ++I)
		{
			if (Features[I] && I < ModulePaths.size() && ModulePaths[I] == Query)
			{
				RequestUninstall(Features[I].get());
				EmitStatus(EFrameStatus::UninstallQueued, StoredName(Features[I].get()), Path);
				return;
			}
		}

		// Nothing matched. Still broadcast: a caller that asked for an unload deserves to
		// know it hit nothing (PostMain's sweep only asks for names it just listed, so
		// this stays quiet at teardown).
		EmitStatus(EFrameStatus::UninstallNotFound, {}, Path, "no active layer matches this name or module path");
	}

	/** Result of one InstallChildrenOf pass: a parent can tell "3 of 6 children" instead
	 *  of only seeing the per-child errors. */
	struct FInstallSummary
	{
		std::size_t Requested = 0;
		std::size_t Queued    = 0;
		std::size_t Refused   = 0;
	};

	/** Install the catalog's DIRECT children of a node into THIS collector. This is
	 *  the one call the host and every collector layer share -- identical install
	 *  code, because each node already knows its own name:
	 *    host:      InstallChildrenOf(GetName())   // MAHO_DECLARE_ENGINE's GetName()
	 *    collector: InstallChildrenOf(GetName())   // FFrameExtension::GetName()
	 *  Child DLLs are loaded by module base name via Install(DllPath) -- never linked,
	 *  always runtime-loaded into this collector. No-op when the node has no children.
	 *
	 *  Direct children only, deliberately: a child that has children of its own
	 *  installs them itself (same call, its own collector), so no level has to know
	 *  about the one below it and nothing can be installed twice. */
	FInstallSummary InstallChildrenOf(std::string_view ParentLayer)
	{
		FInstallSummary Summary;
		for (const std::string& Child : FPluginManager::Get().GetChildren(ParentLayer))
		{
			Summary.Requested += 1;
			if (Install(ApplyModuleExtension(Child)))
			{
				Summary.Queued += 1;
			}
			else
			{
				Summary.Refused += 1;
			}
		}
		if (Summary.Refused > 0)
		{
			ReportError((std::string("InstallChildrenOf '") + std::string(ParentLayer) + "': "
				+ std::to_string(Summary.Queued) + " of " + std::to_string(Summary.Requested)
				+ " children accepted, " + std::to_string(Summary.Refused)
				+ " refused (see the per-child errors above)").c_str());
		}
		return Summary;
	}

	/** Apply pending installs (driving Init stages) + pending uninstalls (driving
	 *  Shutdown stages). Broadcasts OnFramesChanged when anything changed so the
	 *  host knows to re-expand its cached graph. */
	// TInitStages / TShutdownStages are TTypeList<> stage lists: the first drives
	// the install-init graph, the second the unload-shutdown graph. A layer's
	// install and teardown stages are DIFFERENT interfaces, so passing one pack to
	// both would re-run init methods during unload.
	template <typename TInitStages, typename TShutdownStages>
	void FlushPendingUpdates()
	{
		if (!HasPendingUpdates())
		{
			return;
		}

		// REENTRANCY GUARD: a stage is free to install / uninstall (that is the normal way
		// children appear), but it must not run a flush itself -- a nested flush would
		// build a second graph and mutate Pipelines while the outer graph is still
		// running. Refused loudly instead of left to chance; the flag is released by RAII
		// so a thrown exception cannot leave the collector permanently "flushing".
		struct FFlushGuard
		{
			bool& Flag;
			explicit FFlushGuard(bool& InFlag) : Flag(InFlag) { Flag = true; }
			~FFlushGuard() { Flag = false; }
		};
		if (bFlushing)
		{
			ReportError("FlushPendingUpdates refused: a flush is already running "
				"(install/uninstall from a stage is fine -- flushing from one is not)");
			return;
		}
		FFlushGuard Guard(bFlushing);

		// A fatal error from plugin code (GetDependencies is the realistic candidate) must
		// not corrupt the collection or kill the host: report it and return with the
		// pending sets as they are, so the next flush simply retries them.
		try
		{
			FlushPendingUpdatesImpl<TInitStages, TShutdownStages>();
		}
		catch (const std::exception& E)
		{
			ReportError((std::string("flush of pending layer updates threw: ") + E.what()).c_str());
		}
		catch (...)
		{
			ReportError("flush of pending layer updates threw an unknown exception");
		}
	}

	/**
	 * ONE frame of the host's loop: re-expand the frame set if the topology changed, then build
	 * this frame's batch and submit it.
	 *
	 * The host's loop is this plus its own exit check, and the split is deliberate: applying
	 * queued topology changes is FlushPendingUpdates, running a frame is THIS, and the host
	 * decides the order between them.
	 *
	 * NOT named Invoke: `Maho::Invoke<Stage, Context>` is the STAGE DISPATCH protocol, and a
	 * member of that name would hide it inside every FFrameBuilder-derived scope.
	 *
	 * This is the host's ONLY view of the scheduler. FFrameGraph, FFrameBridge and
	 * TFrameDispatch are this class's private implementation and never enter the host's
	 * vocabulary.
	 *
	 * TLoopStages is the frame sequence. The frame set is the frames implementing ANY of its
	 * stages -- the LINQ query is DERIVED from that list, so the two cannot disagree.
	 *
	 * Each SEQUENCE has its own generation counter (see below), because a host may drive more
	 * than one sequence from a single builder -- a world drives input, N fixed steps, and
	 * post-update, all in one tick.
	 *
	 * Frames are NOT drained here: they PIPELINE. What makes that safe is the batch builder's two
	 * STRUCTURAL edges -- a frame's own stages in order, and each stage against its OWN previous
	 * frame (see FFrameBridge). A host that wants one frame in flight calls Wait() after this.
	 */
	template <typename TLoopStages>
	void Execute()
	{
		ExpandLoopFrames(TLoopStages{});

		// One counter PER SEQUENCE, and that is load bearing rather than bookkeeping: the
		// cross-frame self edge binds generation N to N-1, so the number handed to the graph must
		// advance by exactly one per dispatch of THAT sequence. A single counter shared by several
		// sequences would leave each sequence's previous generation at some other offset -- the
		// self edge would then point at a node that does not exist (the edge disappears, and the
		// sequence silently loses its cross-frame exclusion) or at an unrelated older generation.
		std::int32_t& SequenceFrame = LoopFrameNumbers[std::type_index(typeid(TLoopStages))];

		// ONE read of the counter feeds BOTH the batch's frame number and the frame-context slot:
		// the ring arithmetic goes through FFrameBridge::PhaseOf either way, so the slot the stages
		// read cannot drift from the phase the bridge stamps into the batch.
		const std::int32_t Frame = SequenceFrame++;

		TFrameDispatch<TLoopStages, TContext> Dispatch(GetContext(),
			GetContext(FFrameBridge::PhaseOf(Frame)));
		FFrameBridge::FResult Built = FFrameBridge::Build(LoopFrames,
			TFrameDispatch<TLoopStages, TContext>::StageIndices(), Frame, Dispatch);

		// Report the declarations that cannot bind ONCE per frame set, not once per frame: the
		// same typo would otherwise be logged on every single frame.
		if (bLoopJustExpanded)
		{
			bLoopJustExpanded = false;
			ReportDiagnostics(Built.Diagnostics);
		}

		FFrameGraph& Loop = GetGraph();
		std::string Reason;
		if (!Loop.Submit(std::move(Built.Tasks), &Reason) && Reason != LastLoopRejectReason)
		{
			LastLoopRejectReason = Reason;
			ReportError((std::string("frame batch rejected (") + Reason + ")").c_str());
		}
	}

	/** Quiescence for teardown: wait until everything this builder has submitted has finished --
	 *  the graph's nodes AND the pool.
	 *
	 *  Both halves matter and they are NOT the same set: Submit's fence covers the nodes, while a
	 *  stage body may submit its own async work to the pool (nested graph work, asset / RHI
	 *  helpers) that no node fence tracks. The host MUST call this before anything frees a plugin
	 *  module -- a module must never be unloaded under running code -- and the destructor repeats
	 *  it as a backstop.
	 *
	 *  The frame loop does NOT call this per frame: frames PIPELINE (see Execute). Calling it per
	 *  frame would serialize them AND pay a full pool barrier every frame; a host that wants that
	 *  anyway can ask for it. Note the one-shot paths (FlushPendingUpdates) are already safe
	 *  points ON THEIR OWN for the graph -- each batch drains before and after -- which is what
	 *  lets them share the loop's graph. */
	void Wait()
	{
		if (Graph)
		{
			Graph->Wait();
		}
		Pool->Flush(Lane);
	}

	/** Ask every ACTIVE frame to uninstall (the teardown sweep). Returns how many were asked --
	 *  a host that still sees frames alive afterwards has a missing shutdown dependency edge.
	 *  By NAME, not by pointer: a name is what survives its module, and TryUninstall is the one
	 *  entry point that already knows how to match an instance it owns. */
	[[nodiscard]] std::size_t UninstallAll()
	{
		std::vector<std::string> Names;
		Names.reserve(Pipelines.size());
		for (const FFrameExtension* Layer : Pipelines)
		{
			Names.emplace_back(StoredName(Layer));
		}
		for (const std::string& Name : Names)
		{
			TryUninstall(Name);
		}
		return Names.size();
	}

	/** Forget reloads that were queued but never applied (teardown: nothing may reload now). */
	void CancelPendingReloads()
	{
		PendingReloads.clear();
	}

	/** Destroy EVERY instance still held, without driving teardown stages, and drop their
	 *  modules: the host's last resort during teardown.
	 *
	 *  Why it is needed: the normal path (UninstallAll + FlushPendingUpdates) can legitimately
	 *  REFUSE an uninstall -- a frame still depended on is kept alive on purpose -- yet a host may
	 *  have to destroy the instances before some resource they depend on goes away (a GPU device,
	 *  a context). Call it only after the stages were attempted: it runs no plugin stage.
	 *
	 *  It keeps the collector's invariants, which a bare instance-container clear() would not: an
	 *  instance leaves the active set BEFORE it is freed, so "in Pipelines == alive" still holds,
	 *  the per-slot name/module bookkeeping is cleared with it, and nothing dangles. */
	void ReleaseAll()
	{
		std::vector<FFrameExtension*> All(Pipelines.begin(), Pipelines.end());
		for (FFrameExtension* F : All)
		{
			if (F != nullptr)
			{
				Pipelines.erase(std::remove(Pipelines.begin(), Pipelines.end(), F), Pipelines.end());
				DeleteUnloaded(F);
			}
		}
		// Requests and pending installs also name instances; both are moot once nothing is alive.
		PendingRemoveRequests.clear();
		DropPendingInstalls();
	}

	/** The flush body (see the wrapper above for the guard + fatal-error handling). */
	template <typename TInitStages, typename TShutdownStages>
	void FlushPendingUpdatesImpl()
	{
		bool bChanged = false;
		if (!PendingAdded.empty())
		{
			bChanged = true;

			// FIRST, before the init batch: honor any removal requested for a layer whose
			// install is still pending. Order matters -- running the batch first would
			// install the layer and unload it again in the same flush, so a caller that
			// changed its mind would watch it go active anyway.
			std::vector<FFrameExtension*> Cancelled;
			for (FFrameExtension* P : PendingAdded)
			{
				if (PendingRemoveRequests.count(P))
				{
					Cancelled.push_back(P);
				}
			}
			for (FFrameExtension* P : Cancelled)
			{
				const std::string Name(StoredName(P));
				const std::string Path = ModulePathOf(P);
				PendingAdded.erase(std::remove(PendingAdded.begin(), PendingAdded.end(), P), PendingAdded.end());
				PendingRemoveRequests.erase(P);
				DeleteUnloaded(P);   // instance + module released; never initialized
				EmitStatus(EFrameStatus::InstallCancelled, Name, Path, "an uninstall request arrived first");
			}

			std::vector<FFrameExtension*> NewLayers;
			NewLayers.reserve(PendingAdded.size());
			for (FFrameExtension* P : PendingAdded)
			{
				Pipelines.push_back(P);
				NewLayers.push_back(P);
			}
			PendingAdded.clear();

			// One SHOT batch through the shared graph: build the declarations, then submit and
			// drain. The drain BEFORE the submit is what makes this correct -- the frame loop is
			// using the same graph and the same ring, so a one-shot batch may only start from a
			// quiescent ring (ring-reuse rule). With that, an install/uninstall is simply a
			// batch that happens at a safe point.
			//
			// The drain is the GRAPH's fence, NOT the builder's Wait(): a builder Wait() also flushes
			// the thread pool, and this flush runs INSIDE a graph node (FRender::Tick drives its own
			// feature installs) -- so a pool barrier here waits for the pool to drain including the
			// very task calling it. That is a self-deadlock: guaranteed with a single pool worker
			// (MAHO_PARALLELISM=1), intermittent with many (measured as the startup white screen).
			FFrameGraph& Update = GetGraph();
			// A one-shot batch runs at frame number 0 (below), so its slot is PhaseOf(0) == 0 -- and
			// these batches are drained before and after, so slot 0 is never shared with the loop.
			TFrameDispatch<TInitStages, TContext> Dispatch(GetContext(), GetContext(0));
			Update.Wait();
			FFrameBridge::FResult Built = FFrameBridge::Build(NewLayers,
				TFrameDispatch<TInitStages, TContext>::StageIndices(), 0, Dispatch);
			ReportDiagnostics(Built.Diagnostics);

			std::string Reason;
			if (!Update.Submit(std::move(Built.Tasks), &Reason))
			{
				// Structurally broken (the only structural failure a batch can have is a cycle).
				// Report it, broadcast it, and RELEASE the batch. No silent retry: leaving these
				// pending would re-submit (and re-report) on every flush, and keeping only their
				// instances would make the next Install() create a second one. Released-and-
				// reported means the caller sees InstallCompileFailed (Detail names the cause)
				// and, when it has fixed the order/dependency, simply calls Install() again.
				ReportError((std::string("install init batch rejected (") + Reason
					+ "); the batch was released").c_str());
				for (FFrameExtension* P : NewLayers)
				{
					const std::string Name(StoredName(P));
					const std::string Path = ModulePathOf(P);
					Pipelines.erase(std::remove(Pipelines.begin(), Pipelines.end(), P), Pipelines.end());
					DeleteUnloaded(P);
					EmitStatus(EFrameStatus::InstallCompileFailed, Name, Path, Reason);
				}
			}
			else
			{
				Wait();
				for (FFrameExtension* P : NewLayers)
				{
					EmitStatus(EFrameStatus::Installed, StoredName(P), {});
				}
			}
		}

		if (FlushUnload<TShutdownStages>())
		{
			bChanged = true;
		}

		if (bChanged)
		{
			// The frame set the loop cached is now stale. Set HERE, where the change happens,
			// rather than by the host: then the host holds no cache to invalidate, and the
			// expansion below is skipped exactly when it is still valid.
			bLoopDirty = true;
			BroadcastIsolated(OnFramesChanged);
		}
	}

	/** Discard installs that were queued but never applied. Install() loads the module
	 *  and builds the instance RIGHT AWAY (only the init stages are deferred), so
	 *  dropping the queue is not enough: the instance and its module have to be
	 *  released too -- otherwise they survive to ~FFrameBuilder, i.e. past every
	 *  other layer's unload, and freeing a module whose dependencies are already gone
	 *  is exactly where a detach crash lives. */
	void DropPendingInstalls()
	{
		for (FFrameExtension* Layer : PendingAdded)
		{
			DeleteUnloaded(Layer);
		}
		PendingAdded.clear();
	}

private:

	// -- FQuery data source --
	// The NON-const form is the collector's own write path and stays private: handing out
	// a mutable reference would let a caller push instances in, bypassing every check
	// Install performs (module ownership, name uniqueness, the pending queue).
	std::vector<FFrameExtension*>& GetQueryData() override { return Pipelines; }

public:
	const std::vector<FFrameExtension*>& GetQueryData() const override { return Pipelines; }

private:

	/** The layer's name as stored by the collector (a plain string copied at Install):
	 *  naming a layer this way needs NO call into its module. That matters because the
	 *  parallel slot vectors outlive the module of a released layer -- DeleteUnloaded
	 *  clears the name right next to resetting the instance, so matching / reporting
	 *  during teardown never runs plugin code. Empty when unknown. */
	[[nodiscard]] std::string_view StoredName(const FFrameExtension* Layer) const
	{
		const std::size_t Slot = SlotOf(Layer);
		return Slot < LayerNames.size() ? std::string_view(LayerNames[Slot]) : std::string_view{};
	}

	/** Slot of a layer's instance (== its index in Features/Modules/ModulePaths/LayerNames),
	 *  or npos when unknown. */
	[[nodiscard]] std::size_t SlotOf(const FFrameExtension* Layer) const
	{
		for (std::size_t I = 0; I < Features.size(); ++I)
		{
			if (Features[I].get() == Layer)
			{
				return I;
			}
		}
		return NPos;
	}

	/** True when a layer with this name is active or pending. O(log n). */
	[[nodiscard]] bool HasLayerName(std::string_view Name) const
	{
		return NameToSlot.find(std::string(Name)) != NameToSlot.end();
	}

	/** Is this layer still OWNED by the collector (i.e. alive)? The single liveness
	 *  predicate: unload erases from Pipelines BEFORE releasing, so "owned" is the
	 *  authoritative answer and any pointer the collector handed out can be checked
	 *  against it instead of being dereferenced blind. */
	[[nodiscard]] bool IsOwned(const FFrameExtension* Layer) const
	{
		return Layer != nullptr && SlotOf(Layer) != NPos;
	}

	/** The DLL path this layer was loaded from (parallel storage, same lifetime rules
	 *  as StoredName). Empty when unknown. */
	[[nodiscard]] std::string ModulePathOf(const FFrameExtension* Layer) const
	{
		const std::size_t Slot = SlotOf(Layer);
		return Slot < ModulePaths.size() ? ModulePaths[Slot] : std::string{};
	}

	/** The ONE-SHOT update graph: init / shutdown batches. Created on first use, drained before
	 *  and after every batch. Deliberately a different object from whatever drives the frame
	 *  loop: the node table and the ring belong to the graph, so the loop's cross-frame history
	 *  must not share a table with a batch that is created and drained ad hoc. */
	/** THE graph. ONE object serves both the frame loop and the one-shot batches, and that is
	 *  safe for exactly two reasons, both of which are disciplines this class enforces rather
	 *  than properties of the graph:
	 *
	 *   1. every one-shot batch is drained BEFORE it is submitted (see the init/unload paths),
	 *      so it always starts from a quiescent ring -- which is the ring-reuse rule;
	 *   2. a batch's identities are distinct by STAGE, so the three batches a frame takes part in
	 *      over its life never collide: (FLog, IInit, phase), (FLog, ITick, phase) and
	 *      (FLog, IShutdown, phase) are three different nodes.
	 *
	 *  Sharing also buys something a pair of graphs could not: an install-time node stays
	 *  addressable from the loop, because identity survives the batch that created it. Created
	 *  on first use. */
	FFrameGraph& GetGraph()
	{
		if (!Graph)
		{
			// The lane goes in with the pool: it is the identity of "work this graph submitted",
			// which is exactly what this graph's Wait() has to drain and nothing else's.
			Graph = std::make_unique<FFrameGraph>(*Pool, Lane);

			// A collector IS an FFrameExtension (FRender, FGameWorld, FExampleEditor), so it can
			// name itself for the diagnostics that group a collector's stages. The host is NOT a
			// frame (it only owns this builder), so the cast fails there and the owner stays empty.
			if (auto* Self = dynamic_cast<FFrameExtension*>(this))
			{
				Graph->SetOwnerName(Self->GetName().data());
			}

			Graph->Initialize();
		}
		return *Graph;
	}

	/** Any queued topology change? (install / uninstall / reload) */
	[[nodiscard]] bool HasPendingUpdates() const
	{
		return !PendingAdded.empty() || !PendingRemoveRequests.empty() || !PendingReloads.empty();
	}

	/** Re-run the frame-set query when the topology changed. Takes the stage list as a
	 *  TTypeList so the pack expands into Select<...>, which takes the stages directly. */
	template <typename... TStages>
	void ExpandLoopFrames(TTypeList<TStages...>)
	{
		if (!bLoopDirty)
		{
			return;
		}
		bLoopDirty = false;
		bLoopJustExpanded = true;
		if (Graph)
		{
			Graph->Wait();   // the frame set is about to change: nothing may be in flight
		}
		LoopFrames = Select<TStages...>();
	}

	/** Report what the bridge could not resolve. NON-FATAL, deliberately: a target that does not
	 *  exist makes the EDGE disappear (an edge needs both ends), and "the producer is an optional
	 *  plugin that is not installed" is a first-class case, not an error. What the bridge CAN
	 *  tell is that the declaration is WRONG -- a name that is not in this frame set, a stage
	 *  that is not in this sequence, a frame that does not implement the stage it is named at --
	 *  and that is what is reported here, once, at the batch that contains the typo.
	 *
	 *  protected because the host drives its own frame batches and owes the author the same
	 *  report. */
protected:
	void ReportDiagnostics(const std::vector<FFrameBridge::FDiagnostic>& Diagnostics)
	{
		// The implementation lives in Source/Private/Engine/FrameBuilder.cpp: this one is
		// STATELESS (it only formats and reports), so it needs neither TContext nor the
		// collection -- and a member of a class template cannot be defined out of line without
		// an explicit instantiation, which the engine module cannot provide for plugin
		// contexts (it must not include plugin headers).
		Detail::ReportBridgeDiagnostics(Diagnostics);
	}

private:
	/** Broadcast one terminal state, with every string copied into the payload.
	 *  ISOLATED: a subscriber that throws must not be able to abort a teardown halfway
	 *  (EmitStatus runs INSIDE the unload loop, between releasing one layer and the
	 *  next) -- so the exception is reported and swallowed here. Handlers AFTER the
	 *  throwing one in the same broadcast are skipped; the collector's own state stays
	 *  consistent, which is what matters. */
	void EmitStatus(EFrameStatus Status, std::string_view Name, std::string_view Path, std::string Detail = {})
	{
		FFrameStatusInfo Info;
		Info.Status = Status;
		Info.Name.assign(Name);
		Info.Path.assign(Path);
		Info.Detail = std::move(Detail);
		BroadcastIsolated(OnFrameStatus, Info);
	}

	/** Same isolation for a no-payload event (OnFramesChanged / OnClosing). */
	template <typename TEvent, typename... TArgs>
	void BroadcastIsolated(TEvent& Event, const TArgs&... Args) noexcept
	{
		try
		{
			Event.Broadcast(Args...);
		}
		catch (const std::exception& E)
		{
			ReportError((std::string("layer event handler threw: ") + E.what()).c_str());
		}
		catch (...)
		{
			ReportError("layer event handler threw an unknown exception");
		}
	}

	TContext& GetContext() { return *static_cast<TContext*>(this); }

	/** The frame context of a ring SLOT, TYPE-ERASED. Implemented by the scheduler, which owns the
	 *  storage and is the only party that knows the nested type:
	 *
	 *      void* GetContext(int Slot) override { return &Slots[Slot]; }
	 *
	 *  The base never names that type: it carries the pointer through TFrameDispatch as a `void*`,
	 *  and the per-stage dispatch specialization -- expanded in the scheduler's own header, where the
	 *  type is complete -- turns it back into a reference. That is the whole reason FFrameBuilder
	 *  needs no extra template parameter and no change to its entry signatures.
	 *
	 *  Every slot must be a LIVE object: all stages take a reference, so returning nullptr would be
	 *  undefined behaviour downstream.
	 *
	 *  The SLOT is the base's to choose, not the caller's: it is the ring index of the batch being
	 *  built (`FFrameBridge::PhaseOf`), so the phase is computed in exactly one place and cannot
	 *  drift from the phase the bridge puts in the batch. */
	[[nodiscard]] virtual void* GetContext(int Slot) = 0;

	/** Rebuild the reverse dependency count: layer name -> depended-on count. */
	void RebuildReverseDeps()
	{
		ReverseDepCount.clear();

		for (FFrameExtension* L : Pipelines)
		{
			ReverseDepCount[std::string(StoredName(L))] = 0;
		}
		for (FFrameExtension* L : PendingAdded)
		{
			ReverseDepCount[std::string(StoredName(L))] = 0;
		}

		for (FFrameExtension* L : Pipelines)
		{
			for (const auto& [Stage, Deps] : L->GetDependencies())
			{
				(void)Stage;
				for (const auto& Dep : Deps)
				{
					// A layer's declaration against ITSELF is intra-layer ordering, never a
					// reason to wait for another layer: `WaitFor<Self>().OnLastFrameStage<...>()`
					// reads as "my next frame waits for my previous frame" (FRender's frame
					// isolation). Counting it made the layer depend on itself, so the greedy
					// below could never take it -- FRender stayed alive, its IShutdown never ran
					// and teardown fell back to the destructor path (measured: "uninstall
					// refused: layer 'FRender' (still depended on by: FRender)", active=7, then a
					// VMA leak assert under ~FRHI).
					if (Dep.TargetName == StoredName(L))
					{
						continue;
					}
					ReverseDepCount[std::string(Dep.TargetName)] += 1;
				}
			}
		}
		for (FFrameExtension* L : PendingAdded)
		{
			for (const auto& [Stage, Deps] : L->GetDependencies())
			{
				(void)Stage;
				for (const auto& Dep : Deps)
				{
					if (Dep.TargetName == StoredName(L))   // self-dependency: see above
					{
						continue;
					}
					ReverseDepCount[std::string(Dep.TargetName)] += 1;
				}
			}
		}
	}

	/** Min-heap greedy unload, then drive the Shutdown stages before delete.
	 *  Returns true when any layer was actually unloaded. TShutdownStages is a
	 *  TTypeList<> of teardown stage interfaces -- NOT the init stages. */
	template <typename TShutdownStages>
	bool FlushUnload()
	{
		if (PendingRemoveRequests.empty())
		{
			return false;
		}
		RebuildReverseDeps();

		std::map<std::string, FFrameExtension*> ByName;
		for (FFrameExtension* L : Pipelines)
		{
			ByName[std::string(StoredName(L))] = L;
		}

		using HeapEntry = std::pair<int, std::string>;
		auto Cmp = [](const HeapEntry& A, const HeapEntry& B) { return A.first > B.first; };
		std::priority_queue<HeapEntry, std::vector<HeapEntry>, decltype(Cmp)> Heap(Cmp);
		for (FFrameExtension* L : PendingRemoveRequests)
		{
			const std::string Name(StoredName(L));
			Heap.push({ ReverseDepCount[Name], Name });
		}

		std::vector<FFrameExtension*> ToUnload;
		while (!Heap.empty())
		{
			const auto [Count, Name] = Heap.top();
			Heap.pop();

			if (ReverseDepCount[Name] != Count)
			{
				continue;
			}
			auto It = ByName.find(Name);
			if (It == ByName.end())
			{
				continue;
			}
			FFrameExtension* Layer = It->second;
			if (!PendingRemoveRequests.count(Layer))
			{
				continue;
			}
			if (Count > 0)
			{
				break;
			}
			ByName.erase(Name);
			PendingRemoveRequests.erase(Layer);
			ToUnload.push_back(Layer);

			for (const auto& [Stage, Deps] : Layer->GetDependencies())
			{
				(void)Stage;
				for (const auto& Dep : Deps)
				{
					const int NewCount = ReverseDepCount[std::string(Dep.TargetName)] - 1;
					ReverseDepCount[std::string(Dep.TargetName)] = NewCount;
					Heap.push({ NewCount, std::string(Dep.TargetName) });
				}
			}
		}

		// (2) Whatever the greedy could not take is still depended on: report it WITH the
		//     dependents and broadcast. This used to be a silent clear(), so a caller's
		//     uninstall request simply vanished.
		if (!PendingRemoveRequests.empty())
		{
			for (FFrameExtension* L : PendingRemoveRequests)
			{
				const std::string Name(StoredName(L));
				std::set<std::string> Dependents;
				for (FFrameExtension* Other : Pipelines)
				{
					if (Other == L)
					{
						continue;   // a layer's own declaration is not "someone else still needs it"
					}
					for (const auto& [Stage, Deps] : Other->GetDependencies())
					{
						(void)Stage;
						for (const auto& Dep : Deps)
						{
							if (std::string(Dep.TargetName) == Name)
							{
								Dependents.insert(std::string(StoredName(Other)));
							}
						}
					}
				}
				std::string Detail = "still depended on by:";
				for (const std::string& D : Dependents)
				{
					Detail += " " + D;
				}
				ReportError((std::string("uninstall refused: layer '") + Name + "' (" + Detail + ")").c_str());
				EmitStatus(EFrameStatus::UninstallRefused, Name, {}, Detail);
			}
		}
		PendingRemoveRequests.clear();

		if (ToUnload.empty())
		{
			return false;
		}

		// One SHOT batch through the shared graph, exactly like the init path: the drain BEFORE
		// the submit is what keeps it out of a frame that is still in flight (see the init path).
		// Graph fence only, for the same reason as there: a builder Wait() flushes the pool, and this
		// runs inside a graph node.
		FFrameGraph& Update = GetGraph();
		TFrameDispatch<TShutdownStages, TContext> Dispatch(GetContext(), GetContext(0));   // frame 0 below, so slot 0
		Update.Wait();
		FFrameBridge::FResult Built = FFrameBridge::Build(ToUnload,
			TFrameDispatch<TShutdownStages, TContext>::StageIndices(), 0, Dispatch);
		ReportDiagnostics(Built.Diagnostics);

		std::string Reason;
		if (!Update.Submit(std::move(Built.Tasks), &Reason))
		{
			// NEVER erase or destroy these: their teardown stages have not run, and a frame
			// destroyed without its teardown is where "resources still cataloged / threads
			// still alive" turns into a crash later. Report, broadcast, and leave them
			// ALIVE -- the host's teardown sweep reports them again.
			ReportError((std::string("unload shutdown batch rejected (") + Reason
				+ "); keeping the batch alive, nothing destroyed").c_str());
			for (FFrameExtension* L : ToUnload)
			{
				EmitStatus(EFrameStatus::UninstallCompileFailed, StoredName(L), {}, Reason);
			}
			return false;   // the active set did not change
		}

		Update.Wait();

		std::set<std::string> UnloadedNames;   // collected BEFORE DeleteUnloaded clears the stored names
		for (FFrameExtension* L : ToUnload)
		{
			const std::string Name(StoredName(L));
			const std::string Path = ModulePathOf(L);
			UnloadedNames.insert(Name);
			Pipelines.erase(std::remove(Pipelines.begin(), Pipelines.end(), L), Pipelines.end());
			DeleteUnloaded(L);
			EmitStatus(EFrameStatus::Uninstalled, Name, Path);
		}

		// Hot reload: the old instance + module are now freed -- load a fresh
		// copy of each reloaded layer. Its Init runs at the next safe point.
		if (!PendingReloads.empty())
		{
			for (const auto& [Name, Path] : PendingReloads)
			{
				if (UnloadedNames.count(Name))
				{
					Install(Path);
				}
				else
				{
					const std::string Detail = "still depended on, or absent";
					ReportError((std::string("Reload refused: ") + Detail + ": " + Name).c_str());
					EmitStatus(EFrameStatus::ReloadRefused, Name, Path, Detail);
				}
			}
			PendingReloads.clear();
		}

		return true;
	}

	/** Request a layer unload (unconditionally recorded, no immediate validation). */
	void RequestUninstall(FFrameExtension* Pipeline)
	{
		if (Pipeline != nullptr)
		{
			PendingRemoveRequests.insert(Pipeline);
		}
	}

	/** The engine owns feature instances + DLLs; on unload it deletes + FreeLibrary them together. */
	void DeleteUnloaded(FFrameExtension* Layer)
	{
		for (std::size_t I = 0; I < Features.size(); ++I)
		{
			if (Features[I].get() == Layer)
			{
				const std::string MyName(I < LayerNames.size() ? LayerNames[I] : std::string());
				Features[I].reset();
				if (I < Modules.size())
				{
					Modules[I].reset();
				}
				if (I < ModulePaths.size())
				{
					ModulePaths[I].clear();
				}
				if (I < LayerNames.size())
				{
					LayerNames[I].clear();
				}
				if (!MyName.empty())
				{
					NameToSlot.erase(MyName);
				}
				return;
			}
		}
	}

private:
	/** "no slot" / "not found" sentinel for slot indices. */
	static constexpr std::size_t NPos = static_cast<std::size_t>(-1);

	/** The one "this collection is closing" flag, owned here because it answers for both
	 *  sides of it:
	 *    - Install / Reload REFUSE once it is set -- a module loaded while the collection
	 *      comes down would never have its stages run, and its DLL would outlive the
	 *      teardown order;
	 *    - the host derives its own vocabulary from it (an `IExit` stage calls the
	 *      engine's RequestExit, and the main loop reads the engine's ShouldExit).
	 *  Atomic: it is set from a stage (any thread) and read from the loop and the guards. */
	std::atomic<bool> bClosing{ false };

	/** Non-zero while a flush is applying pendings (reentrancy guard, RAII-managed). */
	bool bFlushing = false;

protected:
	[[nodiscard]] bool IsClosing() const noexcept { return bClosing.load(std::memory_order_acquire); }

	/** Flip to closing (idempotent). The collector owns the transition so OnClosing has
	 *  exactly one home: the host calls it from RequestExit (and teardown). */
	void CloseForLoads()
	{
		if (bClosing.exchange(true, std::memory_order_acq_rel))
		{
			return;   // already closing -- broadcast once
		}
		BroadcastIsolated(OnClosing);
	}

	// ── the collection, and the machinery that drives it ──────────────────────
	//
	// All PRIVATE: a host drives frames with Execute(), waits with Wait(), sweeps with
	// UninstallAll() and reads GetStats(). FFrameGraph / FFrameBridge / TFrameDispatch never
	// appear in the host's vocabulary, and neither does the frame set the loop caches.
private:

	/** Active layers. INVARIANT: a layer is erased from here BEFORE its instance and module
	 *  are released (FlushUnload erases, then DeleteUnloaded frees), so "in Pipelines" ==
	 *  alive -- which is what makes an instance pointer checkable (IsOwned) instead of
	 *  something to dereference blind, and what the query audit relies on. */
	std::vector<FFrameExtension*> Pipelines;               // active layers (anonymous)
	std::vector<FFrameExtension*> PendingAdded;            // pending installs
	std::set<FFrameExtension*>    PendingRemoveRequests;   // pending uninstall requests
	std::vector<std::pair<std::string, std::string>> PendingReloads;  // (name, dll path)
	std::map<std::string, int> ReverseDepCount;       // layer name -> depended-on count
	std::vector<std::unique_ptr<FAssembly>> Modules;  // DLL keep-alive (move-only)
	std::vector<std::string> ModulePaths;             // parallel to Modules/Features: DLL path per layer
	std::vector<std::string> LayerNames;              // parallel too: the layer's name, copied at Install
	                                                  // (teardown matches / reports without a vtable call)

	/** name -> slot in the parallel vectors. The one O(log n) lookup behind HasLayerName /
	 *  StoredName, kept in sync by Install (insert) and DeleteUnloaded (erase). */
	std::map<std::string, std::size_t> NameToSlot;

	/** The pool this collector drives its nodes in, and the lane of it that is OURS.
	 *
	 *  OwnedPool is the fallback -- a host, or a collector nobody handed a pool to, gets workers
	 *  of its own. UseSharedPool points Pool at a PARENT's instead and takes a lane, so nested
	 *  collectors (host -> FRender -> FExampleEditor) all end up on the outermost builder's worker
	 *  set while each keeps an isolated barrier. That is the whole point of lanes: the isolation
	 *  the engine needs is per COLLECTOR, and it used to be bought with one thread set per
	 *  collector (4 x hardware_concurrency threads to run ~2 nodes at a time). */
	FThreadPool  OwnedPool;
	FThreadPool* Pool = &OwnedPool;
	FThreadPool::FLane Lane = FThreadPool::DefaultLane;

	/** Frame INSTANCE ownership. Declared AFTER Pool, deliberately: members are destroyed in
	 *  reverse declaration order, and a collector child destroyed here flushes -- and releases its
	 *  lane in -- the very pool its parent may be sharing. Pool after Features is what guarantees
	 *  that pool is still alive; the reverse order would be a use-after-free during teardown. */
	std::vector<std::unique_ptr<FFrameExtension>> Features;

	/** Init/shutdown batches AND the frame loop (see GetGraph). Declared AFTER Pool so that it
	 *  is destroyed BEFORE it -- the graph holds Pool by reference. */
	std::unique_ptr<FFrameGraph> Graph;

	/** The loop's cache and the two report-once guards. */
	std::vector<FFrameExtension*>  LoopFrames;
	std::map<std::type_index, std::int32_t> LoopFrameNumbers;  // one generation counter per sequence
	bool                           bLoopDirty = true;         // the cached frame set is stale
	bool                           bLoopJustExpanded = false; // report diagnostics once per set
	std::string                    LastLoopRejectReason;
};

} // namespace Maho
