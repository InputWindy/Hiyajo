#pragma once

#include <Core/Assembly.h>
#include <Core/Delegate.h>
#include <Core/Fatal.h>
#include <Engine/Layer.h>
#include <Engine/LayerTaskGraph.h>
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

// ── FLayerCollector: layer-collection management base ─────────────────────

/**
 * Owns + schedules a set of anonymous FLayerBase instances. Install/Uninstall
 * are recorded into pending sets and applied at the FlushPendingUpdatePipelines
 * safe point; unload is dependency-safe (min-heap greedy). The init/tick/
 * shutdown stage lists are caller-supplied (FEngineBase uses the engine stages,
 * a domain subsystem like FRender uses its own).
 *
 * TContext is the scheduling context passed to every stage method (FEngineBase
 * for the engine, FRender for the render subsystem). It also supplies the
 * FQuery data source (GetQueryData -> Pipelines).
 */
template <typename TContext>
class FLayerCollector : public virtual FQuery<FLayerBase>
{
public:

	/** EVERY terminal state of an install / uninstall / reload operation. Grouped by
	 *  direction so a consumer can switch once (see OnLayerStatus). */
	enum class ELayerStatus : std::uint8_t
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
	 *  unloaded immediately after the call, and the Name pool may already be gone during
	 *  teardown -- nothing here points into either. */
	struct FLayerStatusInfo
	{
		ELayerStatus Status = ELayerStatus::InstallQueued;
		std::string  Name;     // the layer's name as stored by the collector (pool-free)
		std::string  Path;     // the DLL path it was loaded from / would be loaded from
		std::string  Detail;   // why it was refused, who depends on it, which dep failed...
	};

	/** Broadcast whenever the active layer set changes at a safe point. The host
	 *  binds this to re-expand its cached task graph (push, not poll). */
	TMulticastEvent<void()> OnLayersChanged;

	/** Broadcast for EVERY terminal state above -- refused installs, cancelled installs,
	 *  refused uninstalls, run teardown, the lot. Nothing is silent any more.
	 *  OBSERVE ONLY: never call Install / Uninstall / Reload from a handler (ordering
	 *  across modules is the task graph's job, and re-entering a flush is a bug). */
	TMulticastEvent<void(const FLayerStatusInfo&)> OnLayerStatus;

	/** Broadcast ONCE when the collector flips to closing (RequestExit reached it): the
	 *  moment before teardown, while every layer is still alive. Observe only. */
	TMulticastEvent<void()> OnClosing;

	/** Current sizes, for tools / tests / panels (a query -- do not poll it per frame
	 *  from a broadcast). */
	struct FLayerStats
	{
		std::size_t Active = 0;          // layers in Pipelines
		std::size_t PendingAdds = 0;
		std::size_t PendingRemoves = 0;
		std::size_t PendingReloads = 0;
		std::size_t Modules = 0;         // module slots held (includes released ones)
	};

	[[nodiscard]] FLayerStats GetStats() const
	{
		return FLayerStats{ Pipelines.size(), PendingAdded.size(), PendingRemoveRequests.size(),
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
	bool Install(std::string_view DllPath, const char* FactorySymbol = "CreateLayer")
	{
		const std::string Path(DllPath);

		// Refused once the collection is closing: loading a module then is never right
		// (its stages would never run and its DLL would outlive the teardown order), so
		// it is refused LOUDLY instead of being silently dropped later.
		if (IsClosing())
		{
			const std::string Detail = "the collection is closing";
			ReportError((std::string("Install refused: ") + Detail + " (" + Path + ")").c_str());
			EmitStatus(ELayerStatus::InstallRefused, {}, Path, Detail);
			return false;
		}

		auto Asm = std::make_unique<FAssembly>(DllPath);
		if (!Asm->IsLoaded())
		{
			const std::string Detail = "failed to load the module";
			ReportError((std::string("Install refused: ") + Detail + ": " + Path).c_str());
			EmitStatus(ELayerStatus::InstallRefused, {}, Path, Detail);
			return false;
		}

		using CreateFn = FLayerBase * (*)();
		auto Create = Asm->GetProcAs<CreateFn>(FactorySymbol);
		if (Create == nullptr)
		{
			const std::string Detail = std::string("module exports no '") + FactorySymbol + "'";
			ReportError((std::string("Install refused: ") + Detail + ": " + Path).c_str());
			EmitStatus(ELayerStatus::InstallRefused, {}, Path, Detail);
			return false;
		}

		// A plugin factory is plugin code: it may throw (a bad ctor, a failed global init).
		// Nothing is pushed into the parallel vectors before this point, so a throw here
		// leaves the collector exactly as it was -- report it as a refusal instead of
		// letting it escape into whatever drove the install.
		FLayerBase* Raw = nullptr;
		try
		{
			Raw = Create();
		}
		catch (const std::exception& E)
		{
			const std::string Detail = std::string("factory threw: ") + E.what();
			ReportError((std::string("Install refused: ") + Detail + ": " + Path).c_str());
			EmitStatus(ELayerStatus::InstallRefused, {}, Path, Detail);
			return false;
		}
		catch (...)
		{
			const std::string Detail = "factory threw an unknown exception";
			ReportError((std::string("Install refused: ") + Detail + ": " + Path).c_str());
			EmitStatus(ELayerStatus::InstallRefused, {}, Path, Detail);
			return false;
		}

		auto Layer = std::unique_ptr<FLayerBase>(Raw);
		if (!Layer)
		{
			const std::string Detail = "factory returned null";
			ReportError((std::string("Install refused: ") + Detail + ": " + Path).c_str());
			EmitStatus(ELayerStatus::InstallRefused, {}, Path, Detail);
			return false;
		}

		// Copy the name ONCE, into collector-owned storage: from here on the collector
		// never asks the Name pool for it again (the pool is shut down before the
		// collector finishes matching / reporting during teardown).
		const std::string Name(Layer->GetName());

		// One instance per name -- a duplicate would silently shadow the old one.
		if (HasLayerName(Name))
		{
			const std::string Detail = "a layer with this name is already active or pending";
			ReportError((std::string("Install refused: ") + Detail + ": '" + Name + "'").c_str());
			EmitStatus(ELayerStatus::InstallRefused, Name, Path, Detail);
			return false;
		}

		// No fail-fast dependency refusal here: features may legitimately reference
		// each other cross-stage in ANY install order (e.g. Scene.Present depends on
		// DrawTriangle.Render while DrawTriangle.Render depends on Scene.Render).
		// A per-name check cannot see that the cycle is valid across stages, so it
		// would refuse valid mutual deps. The stage-aware graph Compile validates
		// missing deps / real cycles instead (and reports once, non-fatal).

		PendingAdded.push_back(Layer.get());
		Modules.push_back(std::move(Asm));
		Features.push_back(std::move(Layer));
		ModulePaths.push_back(Path);
		LayerNames.push_back(Name);
		NameToSlot[Name] = Features.size() - 1;
		EmitStatus(ELayerStatus::InstallQueued, Name, Path);
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
			EmitStatus(ELayerStatus::ReloadRefused, Query, {}, Detail);
			return;
		}

		for (FLayerBase* L : Pipelines)
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
					EmitStatus(ELayerStatus::ReloadRefused, Query, Path, Detail);
					return;
				}
				PendingReloads.emplace_back(Query, Path);
				RequestUninstall(L);
				EmitStatus(ELayerStatus::ReloadQueued, Query, Path);
				return;
			}
			const std::string Detail = "layer has no module to reload";
			ReportError((std::string("Reload refused: ") + Detail + ": " + Query).c_str());
			EmitStatus(ELayerStatus::ReloadRefused, Query, {}, Detail);
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
		EmitStatus(ELayerStatus::ReloadRefused, Query, {}, Detail);
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

		// 1) Exact layer name -- matched through the collector's stored (pool-free) copy:
		//    callers like GameWorld/Render pass a layer's name and must keep working.
		for (FLayerBase* L : Pipelines)
		{
			if (StoredName(L) == Query)
			{
				RequestUninstall(L);
				EmitStatus(ELayerStatus::UninstallQueued, StoredName(L), Path);
				return;
			}
		}
		// 2) A layer whose install is still PENDING is addressable by name too -- this is
		//    how a caller takes back an install it just requested (the flush cancels it
		//    before the init batch runs; see FlushPendingUpdatePipelines).
		for (FLayerBase* L : PendingAdded)
		{
			if (StoredName(L) == Query)
			{
				RequestUninstall(L);
				EmitStatus(ELayerStatus::UninstallQueued, StoredName(L), Path);
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
				EmitStatus(ELayerStatus::UninstallQueued, StoredName(Features[I].get()), Path);
				return;
			}
		}

		// Nothing matched. Still broadcast: a caller that asked for an unload deserves to
		// know it hit nothing (PostMain's sweep only asks for names it just listed, so
		// this stays quiet at teardown).
		EmitStatus(ELayerStatus::UninstallNotFound, {}, Path, "no active layer matches this name or module path");
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
	 *    collector: InstallChildrenOf(GetName())   // FLayerBase::GetName()
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
	 *  Shutdown stages). Broadcasts OnLayersChanged when anything changed so the
	 *  host knows to re-expand its cached graph. */
	// TInitStages / TShutdownStages are TTypeList<> stage lists: the first drives
	// the install-init graph, the second the unload-shutdown graph. A layer's
	// install and teardown stages are DIFFERENT interfaces, so passing one pack to
	// both would re-run init methods during unload.
	template <typename TInitStages, typename TShutdownStages>
	void FlushPendingUpdatePipelines()
	{
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
			ReportError("FlushPendingUpdatePipelines refused: a flush is already running "
				"(install/uninstall from a stage is fine -- flushing from one is not)");
			return;
		}
		FFlushGuard Guard(bFlushing);

		// A fatal error from plugin code (GetDependencies is the realistic candidate) must
		// not corrupt the collection or kill the host: report it and return with the
		// pending sets as they are, so the next flush simply retries them.
		try
		{
			FlushPendingUpdatePipelinesImpl<TInitStages, TShutdownStages>();
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

	/** The flush body (see the wrapper above for the guard + fatal-error handling). */
	template <typename TInitStages, typename TShutdownStages>
	void FlushPendingUpdatePipelinesImpl()
	{
		bool bChanged = false;
		if (!PendingAdded.empty())
		{
			bChanged = true;

			// FIRST, before the init batch: honor any removal requested for a layer whose
			// install is still pending. Order matters -- running the batch first would
			// install the layer and unload it again in the same flush, so a caller that
			// changed its mind would watch it go active anyway.
			std::vector<FLayerBase*> Cancelled;
			for (FLayerBase* P : PendingAdded)
			{
				if (PendingRemoveRequests.count(P))
				{
					Cancelled.push_back(P);
				}
			}
			for (FLayerBase* P : Cancelled)
			{
				const std::string Name(StoredName(P));
				const std::string Path = ModulePathOf(P);
				PendingAdded.erase(std::remove(PendingAdded.begin(), PendingAdded.end(), P), PendingAdded.end());
				PendingRemoveRequests.erase(P);
				DeleteUnloaded(P);   // instance + module released; never initialized
				EmitStatus(ELayerStatus::InstallCancelled, Name, Path, "an uninstall request arrived first");
			}

			std::vector<FLayerBase*> NewLayers;
			NewLayers.reserve(PendingAdded.size());
			for (FLayerBase* P : PendingAdded)
			{
				Pipelines.push_back(P);
				NewLayers.push_back(P);
			}
			PendingAdded.clear();

			FLayerTaskGraph<TInitStages, TContext> InitGraph(Pool, GetContext());
			InitGraph.Init(NewLayers);
			if (!InitGraph.Compile())
			{
				// Compile failed: report it, broadcast it, and RELEASE the batch. No silent
				// retry: leaving these pending would re-Compile (and re-report) on every
				// flush, and keeping only their instances would make the next Install()
				// create a second one. Released-and-reported means the caller sees
				// InstallCompileFailed (Detail names the offending layer) and, when it has
				// fixed the order/dependency, simply calls Install() again.
				const std::string BadDep = InitGraph.GetCompileErrorNode();
				ReportError((std::string("install init graph compile failed (layer '")
					+ BadDep + "' has a bad dependency); the batch was released").c_str());
				for (FLayerBase* P : NewLayers)
				{
					const std::string Name(StoredName(P));
					const std::string Path = ModulePathOf(P);
					Pipelines.erase(std::remove(Pipelines.begin(), Pipelines.end(), P), Pipelines.end());
					DeleteUnloaded(P);
					EmitStatus(ELayerStatus::InstallCompileFailed, Name, Path, BadDep);
				}
			}
			else
			{
				InitGraph.Execute();
				InitGraph.Flush();
				for (FLayerBase* P : NewLayers)
				{
					EmitStatus(ELayerStatus::Installed, StoredName(P), {});
				}
			}
		}

		if (FlushUnload<TShutdownStages>())
		{
			bChanged = true;
		}

		if (bChanged)
		{
			BroadcastIsolated(OnLayersChanged);
		}
	}

	/** Discard installs that were queued but never applied. Install() loads the module
	 *  and builds the instance RIGHT AWAY (only the init stages are deferred), so
	 *  dropping the queue is not enough: the instance and its module have to be
	 *  released too -- otherwise they survive to ~FLayerCollector, i.e. past every
	 *  other layer's unload, and freeing a module whose dependencies are already gone
	 *  is exactly where a detach crash lives. */
	void DropPendingInstalls()
	{
		for (FLayerBase* Layer : PendingAdded)
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
	std::vector<FLayerBase*>& GetQueryData() override { return Pipelines; }

public:
	const std::vector<FLayerBase*>& GetQueryData() const override { return Pipelines; }

private:

	/** The layer's name as stored by the collector (a plain string copied at Install) --
	 *  NEVER through the Name pool. The pool is shut down during teardown while the
	 *  collector still matches names, feeds status payloads and writes traces, and
	 *  `FName::ToString()` on a cleared pool reads freed storage. Empty when unknown. */
	[[nodiscard]] std::string_view StoredName(const FLayerBase* Layer) const
	{
		const std::size_t Slot = SlotOf(Layer);
		return Slot < LayerNames.size() ? std::string_view(LayerNames[Slot]) : std::string_view{};
	}

	/** Slot of a layer's instance (== its index in Features/Modules/ModulePaths/LayerNames),
	 *  or npos when unknown. */
	[[nodiscard]] std::size_t SlotOf(const FLayerBase* Layer) const
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
	[[nodiscard]] bool IsOwned(const FLayerBase* Layer) const
	{
		return Layer != nullptr && SlotOf(Layer) != NPos;
	}

	/** The DLL path this layer was loaded from (parallel storage, same lifetime rules
	 *  as StoredName). Empty when unknown. */
	[[nodiscard]] std::string ModulePathOf(const FLayerBase* Layer) const
	{
		const std::size_t Slot = SlotOf(Layer);
		return Slot < ModulePaths.size() ? ModulePaths[Slot] : std::string{};
	}

	/** Broadcast one terminal state, with every string copied into the payload.
	 *  ISOLATED: a subscriber that throws must not be able to abort a teardown halfway
	 *  (EmitStatus runs INSIDE the unload loop, between releasing one layer and the
	 *  next) -- so the exception is reported and swallowed here. Handlers AFTER the
	 *  throwing one in the same broadcast are skipped; the collector's own state stays
	 *  consistent, which is what matters. */
	void EmitStatus(ELayerStatus Status, std::string_view Name, std::string_view Path, std::string Detail = {})
	{
		FLayerStatusInfo Info;
		Info.Status = Status;
		Info.Name.assign(Name);
		Info.Path.assign(Path);
		Info.Detail = std::move(Detail);
		BroadcastIsolated(OnLayerStatus, Info);
	}

	/** Same isolation for a no-payload event (OnLayersChanged / OnClosing). */
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

	/** Rebuild the reverse dependency count: layer name -> depended-on count. */
	void RebuildReverseDeps()
	{
		ReverseDepCount.clear();

		for (FLayerBase* L : Pipelines)
		{
			ReverseDepCount[std::string(StoredName(L))] = 0;
		}
		for (FLayerBase* L : PendingAdded)
		{
			ReverseDepCount[std::string(StoredName(L))] = 0;
		}

		for (FLayerBase* L : Pipelines)
		{
			for (const auto& [Stage, Deps] : L->GetDependencies())
			{
				(void)Stage;
				for (const auto& Dep : Deps)
				{
					ReverseDepCount[Dep.Name] += 1;
				}
			}
		}
		for (FLayerBase* L : PendingAdded)
		{
			for (const auto& [Stage, Deps] : L->GetDependencies())
			{
				(void)Stage;
				for (const auto& Dep : Deps)
				{
					ReverseDepCount[Dep.Name] += 1;
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

		std::map<std::string, FLayerBase*> ByName;
		for (FLayerBase* L : Pipelines)
		{
			ByName[std::string(StoredName(L))] = L;
		}

		using HeapEntry = std::pair<int, std::string>;
		auto Cmp = [](const HeapEntry& A, const HeapEntry& B) { return A.first > B.first; };
		std::priority_queue<HeapEntry, std::vector<HeapEntry>, decltype(Cmp)> Heap(Cmp);
		for (FLayerBase* L : PendingRemoveRequests)
		{
			const std::string Name(StoredName(L));
			Heap.push({ ReverseDepCount[Name], Name });
		}

		std::vector<FLayerBase*> ToUnload;
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
			FLayerBase* Layer = It->second;
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
					const int NewCount = ReverseDepCount[Dep.Name] - 1;
					ReverseDepCount[Dep.Name] = NewCount;
					Heap.push({ NewCount, Dep.Name });
				}
			}
		}

		// (2) Whatever the greedy could not take is still depended on: report it WITH the
		//     dependents and broadcast. This used to be a silent clear(), so a caller's
		//     uninstall request simply vanished.
		if (!PendingRemoveRequests.empty())
		{
			for (FLayerBase* L : PendingRemoveRequests)
			{
				const std::string Name(StoredName(L));
				std::set<std::string> Dependents;
				for (FLayerBase* Other : Pipelines)
				{
					for (const auto& [Stage, Deps] : Other->GetDependencies())
					{
						(void)Stage;
						for (const auto& Dep : Deps)
						{
							if (Dep.Name == Name)
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
				EmitStatus(ELayerStatus::UninstallRefused, Name, {}, Detail);
			}
		}
		PendingRemoveRequests.clear();

		if (ToUnload.empty())
		{
			return false;
		}

		FLayerTaskGraph<TShutdownStages, TContext> ShutdownGraph(Pool, GetContext());
		// Copy, do NOT move: ToUnload is still needed below to erase the layers from
		// Pipelines and to release their instances + modules. Moving it here left the
		// loop below iterating an empty vector -- the Shutdown stages ran, but nothing
		// was ever destroyed, so every layer's DLL was released last (after its own
		// dependencies were already unloaded).
		ShutdownGraph.Init(ToUnload);
		if (!ShutdownGraph.Compile())
		{
			// NEVER erase or destroy these: their Shutdown stages have not run, and a layer
			// destroyed without its teardown is where "resources still cataloged / threads
			// still alive" turns into a crash later. Report, broadcast, and leave them
			// ALIVE -- the host's teardown sweep reports them again.
			const std::string BadDep = ShutdownGraph.GetCompileErrorNode();
			ReportError((std::string("unload shutdown graph compile failed (layer '") + BadDep
				+ "' has a bad dependency); keeping the batch alive, nothing destroyed").c_str());
			for (FLayerBase* L : ToUnload)
			{
				EmitStatus(ELayerStatus::UninstallCompileFailed, StoredName(L), {}, BadDep);
			}
			return false;   // the active set did not change
		}

		ShutdownGraph.Execute();
		ShutdownGraph.Flush();

		std::set<std::string> UnloadedNames;   // collected BEFORE DeleteUnloaded clears the stored names
		for (FLayerBase* L : ToUnload)
		{
			const std::string Name(StoredName(L));
			const std::string Path = ModulePathOf(L);
			UnloadedNames.insert(Name);
			Pipelines.erase(std::remove(Pipelines.begin(), Pipelines.end(), L), Pipelines.end());
			DeleteUnloaded(L);
			EmitStatus(ELayerStatus::Uninstalled, Name, Path);
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
					EmitStatus(ELayerStatus::ReloadRefused, Name, Path, Detail);
				}
			}
			PendingReloads.clear();
		}

		return true;
	}

	/** Request a layer unload (unconditionally recorded, no immediate validation). */
	void RequestUninstall(FLayerBase* Pipeline)
	{
		if (Pipeline != nullptr)
		{
			PendingRemoveRequests.insert(Pipeline);
		}
	}

	/** The engine owns feature instances + DLLs; on unload it deletes + FreeLibrary them together. */
	void DeleteUnloaded(FLayerBase* Layer)
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

protected:
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

	/** Active layers. INVARIANT: a layer is erased from here BEFORE its instance and module
	 *  are released (FlushUnload erases, then DeleteUnloaded frees), so "in Pipelines" ==
	 *  alive -- which is what makes an instance pointer checkable (IsOwned) instead of
	 *  something to dereference blind, and what the query audit relies on. */
	std::vector<FLayerBase*> Pipelines;               // active layers (anonymous)
	std::vector<FLayerBase*> PendingAdded;            // pending installs
	std::set<FLayerBase*>    PendingRemoveRequests;   // pending uninstall requests
	std::vector<std::pair<std::string, std::string>> PendingReloads;  // (name, dll path)
	std::map<std::string, int> ReverseDepCount;       // layer name -> depended-on count
	std::vector<std::unique_ptr<FAssembly>> Modules;  // DLL keep-alive (move-only)
	std::vector<std::string> ModulePaths;             // parallel to Modules/Features: DLL path per layer
	std::vector<std::string> LayerNames;              // parallel too: the layer's name, copied at Install
	                                                  // (pool-free: teardown matches / reports through this)

	/** name -> slot in the parallel vectors. The one O(log n) lookup behind HasLayerName /
	 *  StoredName, kept in sync by Install (insert) and DeleteUnloaded (erase). */
	std::map<std::string, std::size_t> NameToSlot;
	std::vector<std::unique_ptr<FLayerBase>> Features; // layer instance ownership
	FThreadPool Pool;                                 // task execution
};

} // namespace Maho
