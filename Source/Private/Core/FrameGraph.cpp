#include <Core/FrameGraph.h>

#include <Core/Fatal.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

namespace Maho
{

namespace
{
	/** "no node" sentinel for a batch-local index. */
	constexpr std::size_t NoNode = static_cast<std::size_t>(-1);

	/**
	 * Env-gated stage trace (MAHO_TRACE_STAGES=1). Read once, so it costs nothing when off and
	 * can be switched on without a rebuild.
	 *
	 * It exists for ONE failure mode: a hard crash (0xC0000005) has no stack and raises nothing
	 * C++ can catch, so the last stage ENTERED is the only thing that names the culprit. The
	 * body is bracketed (enter/exit) rather than merely logged at dispatch, which makes "the
	 * last enter with no exit" the answer. Note it is not a substitute for a stack -- it names
	 * the stage, not the line -- but it narrows a 34-DLL program to one call site.
	 */
	bool StageTraceEnabled()
	{
		static const bool bOn = (std::getenv("MAHO_TRACE_STAGES") != nullptr);
		return bOn;
	}

	void TraceStage(const char* What, const FTaskKey& Key)
	{
		std::fprintf(stderr, "[fg] %s %.*s::%s@%d\n", What,
			static_cast<int>(Key.Name.size()), Key.Name.data(), Key.Stage.name(), Key.Phase);
		std::fflush(stderr);
	}

	/** The phase index IS the slot index now: the space is exactly the ring [0, K). */
	std::int32_t SlotIndexOf(std::int32_t Phase)
	{
		return Phase;
	}

	bool PhaseIsKnown(std::int32_t Phase)
	{
		return IsValidPhase(Phase);
	}
}

std::size_t FTaskKeyHash::operator()(const FTaskKey& Key) const noexcept
{
	std::size_t H = std::hash<std::string_view>{}(Key.Name);
	H ^= std::hash<std::type_index>{}(Key.Stage) + 0x9e3779b9u + (H << 6) + (H >> 2);
	H ^= std::hash<std::int32_t>{}(Key.Phase) + 0x9e3779b9u + (H << 6) + (H >> 2);
	return H;
}

FFrameGraph::FFrameGraph(FThreadPool& InPool)
	: Pool(InPool)
{
	for (auto& Count : SlotInFlight)
	{
		Count.store(0, std::memory_order_relaxed);
	}
}

FFrameGraph::~FFrameGraph()
{
	// Stop the scheduler thread HERE, while our members are still alive: leaving it to
	// ~FThreadedServer would stop it only after Nodes/Registry/EventStates were destroyed,
	// and the worker could still be touching them. (The host is required to
	// DrainForShutdown first; this is the backstop, and Shutdown is idempotent.)
	FThreadedServer::Shutdown();
}

// ── submission (host thread) ──────────────────────────────────────────────────

bool FFrameGraph::Submit(std::vector<FTask> Tasks, std::string* OutReason)
{
	if (!IsRunning())
	{
		if (OutReason != nullptr) { *OutReason = "Submit before Initialize()"; }
		return false;
	}

	std::string Reason;
	if (!Validate(Tasks, Reason))
	{
		if (OutReason != nullptr) { *OutReason = std::move(Reason); }
		return false;
	}

	// Admission: block the CALLER until every phase this batch names has drained. This is
	// the ring-reuse rule, and it lives here rather than in the caller so that the caller
	// only ever needs Submit and Wait. The scheduler thread must never perform this wait --
	// it would stop seeing the very completions the wait depends on.
	//
	// Safe only because of the contract: one submit per phase per occupancy (see Submit).
	WaitPhasesIdle(Tasks);

	// The fence count rises BEFORE the command is enqueued, so a Wait() issued right after this
	// Submit cannot miss the work: were it raised by the scheduler at DISPATCH time, the window
	// between the enqueue and the dispatch would read as "nothing outstanding" and the caller
	// would sail on while its own batch was still queued -- two frames dispatched side by side.
	{
		std::lock_guard<std::mutex> Lock(SlotMutex);
		AwaitingCompletion.fetch_add(static_cast<std::uint32_t>(Tasks.size()),
			std::memory_order_acq_rel);
	}

	// C1: qualified call -- this class's own Submit would otherwise hide the server's.
	FThreadedServer::Submit([this, Batch = std::move(Tasks)]() mutable
	{
		ApplySubmit(std::move(Batch));
	});
	return true;
}

bool FFrameGraph::Validate(const std::vector<FTask>& Tasks, std::string& OutReason) const
{
	// Pass 1: structural checks on the declarations themselves.
	std::unordered_map<FTaskKey, std::size_t, FTaskKeyHash> ByKey;
	for (std::size_t I = 0; I < Tasks.size(); ++I)
	{
		const FTask& Task = Tasks[I];

		if (!PhaseIsKnown(Task.Key.Phase))
		{
			OutReason = "phase index out of range on '" + std::string(Task.Key.Name) + "'";
			return false;
		}
		if (!ByKey.emplace(Task.Key, I).second)
		{
			OutReason = "the same node is declared twice in one batch: "
				+ std::string(Task.Key.Name);
			return false;
		}

		for (const FDependency& Dep : Task.Dependencies)
		{
			if (!PhaseIsKnown(Dep.Target.Phase))
			{
				OutReason = "dependency phase out of range on '"
					+ std::string(Task.Key.Name) + "'";
				return false;
			}
		}
		for (const FDependency& Dep : Task.Blocks)
		{
			if (!PhaseIsKnown(Dep.Target.Phase))
			{
				OutReason = "block phase out of range on '" + std::string(Task.Key.Name) + "'";
				return false;
			}
		}
	}

	// Pass 2: cycles. Only edges inside this batch can close a cycle: an edge to a node
	// that does not exist YET disappears (see the header, caveat B), so a dependency on a
	// later submission can never come back around. That makes this check both necessary
	// and sufficient.
	{
		std::unordered_map<FTaskKey, int, FTaskKeyHash> Color;   // 0 white, 1 gray, 2 black

		std::function<bool(const FTaskKey&)> Visit = [&](const FTaskKey& Key) -> bool
		{
			Color[Key] = 1;
			const auto It = ByKey.find(Key);
			if (It != ByKey.end())
			{
				for (const FDependency& Dep : Tasks[It->second].Dependencies)
				{
					if (ByKey.find(Dep.Target) == ByKey.end())
					{
						continue;   // outside this batch
					}
					const int C = Color[Dep.Target];
					if (C == 1) { return true; }
					if (C == 0 && Visit(Dep.Target)) { return true; }
				}
			}
			Color[Key] = 2;
			return false;
		};

		for (const FTask& Task : Tasks)
		{
			if (Color[Task.Key] == 0 && Visit(Task.Key))
			{
				OutReason = "dependency cycle involving '" + std::string(Task.Key.Name) + "'";
				return false;
			}
		}
	}

	return true;
}

// ── scheduler thread ──────────────────────────────────────────────────────────

FNodeId FFrameGraph::GetOrCreateNodeId(const FTaskKey& Key)
{
	// The registry maps the EXACT key, so two different triples can never share an Id --
	// the "hash collision" class of bug does not exist here (a raw hash would have it).
	const auto It = Registry.find(Key);
	if (It != Registry.end())
	{
		return It->second;
	}

	const FNodeId Id = static_cast<FNodeId>(Nodes.size());
	Nodes.emplace_back();   // in place: FTaskNode is not movable
	Registry.emplace(Key, Id);

	// Floor the event table at this node's phases. A deque, because growing never moves
	// elements (std::atomic_bool is neither copyable nor movable).
	const std::size_t Needed = (static_cast<std::size_t>(Id) + 1)
		* static_cast<std::size_t>(kPhaseCount);
	while (EventStates.size() < Needed)
	{
		EventStates.emplace_back(false);
	}
	return Id;
}

FNodeId FFrameGraph::ResolveTarget(const FTaskKey& Target)
{
	const auto It = Registry.find(Target);
	if (It == Registry.end())
	{
		// The target instance does not exist, so the EDGE does not exist: an edge needs both
		// endpoints. The waiting node is simply one dependency lighter and does not need to
		// know anything about why (caveat B in the header).
		return InvalidNodeId;
	}
	return It->second;
}

void FFrameGraph::WireNode(FTaskNode& Node, const FTask& Decl)
{
	// The node's identity IS its index, and the registry is the only place that mapping
	// lives (which is why FTaskNode stores no Id). Pass 1 has just registered this key, so
	// the lookup cannot fail -- at() throws rather than reading a stray iterator.
	const FNodeId MyId = Registry.at(Decl.Key);

	auto AddEdge = [&](const FDependency& Dep)
	{
		const FNodeId Target = ResolveTarget(Dep.Target);
		if (Target == InvalidNodeId)
		{
			return;   // dangling edge: disappears
		}

		const FEvent Event = Detail::EventOf(Target, Nodes[Target].Key.Phase);

		// Dedup by target event: a double declaration would otherwise count twice while the
		// event is set once -- a permanent wait.
		if (std::find(Node.Dependencies.begin(), Node.Dependencies.end(), Event)
			!= Node.Dependencies.end())
		{
			return;
		}

		Node.Dependencies.push_back(Event);

		// Only an event that is still unset needs to be waited on. An already-set event
		// (the target finished earlier, possibly in an earlier batch or generation) is
		// satisfied by definition -- this is not "interpreting execution history", it is
		// simply reading the flag that the synchronization is built on.
		if (!EventStates[Event].load(std::memory_order_acquire))
		{
			Nodes[Target].Successors.push_back(MyId);
			++Node.UnmetCount;
		}
	};

	for (const FDependency& Dep : Decl.Dependencies)
	{
		AddEdge(Dep);
	}
}

void FFrameGraph::ApplySubmit(std::vector<FTask> Tasks)
{
	// Pass 1: create/reset every instance FIRST, so resolution sees the whole batch
	// regardless of declaration order -- and because Nodes may reallocate, no references
	// are held across this loop.
	std::vector<FNodeId> Ids;
	Ids.reserve(Tasks.size());
	for (const FTask& Task : Tasks)
	{
		const FNodeId Id = GetOrCreateNodeId(Task.Key);
		Ids.push_back(Id);

		FTaskNode& Node = Nodes[Id];
		Node.Key = Task.Key;
		Node.Dependencies.clear();
		Node.Successors.clear();
		Node.UnmetCount = 0;
		Node.CompletionEvent = Detail::EventOf(Id, Task.Key.Phase);
		Node.Closure = Task.Closure;
		Node.bDispatched.store(false, std::memory_order_relaxed);

		// Reset this phase's completion flag: the instance is being re-entered, so the flag
		// describes the PREVIOUS occupancy and must not leak into this one. It is the
		// caller's job to have made sure the previous occupancy is over (WaitPhaseIdle).
		EventStates[Node.CompletionEvent].store(false, std::memory_order_release);
	}

	// Pass 2: wire each node's own dependencies (reverse tables + unmet counts).
	for (std::size_t I = 0; I < Tasks.size(); ++I)
	{
		WireNode(Nodes[Ids[I]], Tasks[I]);
	}

	// Pass 2b: reverse declarations. "I block X" means X must wait for ME, so my completion
	// event becomes one of X's dependencies. A separate pass because it writes to OTHER
	// nodes: it must run after everyone's own dependencies are wired (pass 2) and before
	// anything is dispatched (pass 3).
	for (std::size_t I = 0; I < Tasks.size(); ++I)
	{
		FTaskNode& Me = Nodes[Ids[I]];
		for (const FDependency& Blocked : Tasks[I].Blocks)
		{
			const FNodeId Target = ResolveTarget(Blocked.Target);
			if (Target == InvalidNodeId)
			{
				continue;
			}

			FTaskNode& Other = Nodes[Target];

			// A target that is already running or done cannot honour a new inbound edge:
			// re-opening it would mean re-running it. Skipped (the edge needs both endpoints
			// to be available), not reported -- see caveat B.
			if (Other.bDispatched.load(std::memory_order_acquire))
			{
				continue;
			}

			// Cross-node dedup: if the target already depends on me (it declared the edge
			// from its own side), do not count it twice.
			if (std::find(Other.Dependencies.begin(), Other.Dependencies.end(),
				Me.CompletionEvent) != Other.Dependencies.end())
			{
				continue;
			}

			Other.Dependencies.push_back(Me.CompletionEvent);
			if (!EventStates[Me.CompletionEvent].load(std::memory_order_acquire))
			{
				Me.Successors.push_back(Target);
				++Other.UnmetCount;
			}
		}
	}

	// Pass 3: dispatch everything already satisfied.
	for (FNodeId Id : Ids)
	{
		if (!Nodes[Id].bDispatched.load(std::memory_order_relaxed)
			&& Nodes[Id].UnmetCount == 0)
		{
			Dispatch(Id);
		}
	}
}

void FFrameGraph::Dispatch(FNodeId Id)
{
	FTaskNode& Node = Nodes[Id];
	Node.bDispatched.store(true, std::memory_order_release);

	// C3: the in-flight count rises BEFORE the work is handed to the pool, and falls when
	// the scheduler thread records the completion. WaitPhaseIdle and DrainForShutdown both
	// depend on exactly this ordering -- otherwise they could observe zero while a node was
	// still about to run, and "the phase is idle" would be a lie.
	SlotInFlight[SlotIndexOf(Node.Key.Phase)].fetch_add(1, std::memory_order_acq_rel);

	Pool.Submit([this, Id]()
	{
		// A hard crash (0xC0000005) has no stack and no exception to catch, so the last stage
		// ENTERED is the only thing that names the culprit -- that is what this trace exists for,
		// and why it brackets the body rather than merely logging the dispatch.
		const bool bTrace = StageTraceEnabled();
		if (bTrace)
		{
			TraceStage("enter", Nodes[Id].Key);
		}

		// Run the payload. A throwing body must NOT stop the graph: report it and still
		// complete the node, or every waiter downstream waits forever.
		try
		{
			Nodes[Id].Closure();
		}
		catch (const std::exception& E)
		{
			ReportError((std::string("node threw: ") + E.what()).c_str());
		}
		catch (...)
		{
			ReportError("node threw unknown exception");
		}

		if (bTrace)
		{
			TraceStage("exit ", Nodes[Id].Key);
		}

		// Completion bookkeeping happens ON THE SCHEDULER THREAD (state is single-owned).
		// The body never touches the graph: it cannot even reach its own completion event.
		FThreadedServer::Submit([this, Id]() { OnNodeCompleted(Id); });
	});
}

void FFrameGraph::OnNodeCompleted(FNodeId Id)
{
	FTaskNode& Node = Nodes[Id];

	// Set the completion event, then release every waiter that is now satisfied.
	EventStates[Node.CompletionEvent].store(true, std::memory_order_release);

	std::vector<FNodeId> BecameReady;
	for (FNodeId Waiter : Node.Successors)
	{
		FTaskNode& W = Nodes[Waiter];
		if (W.UnmetCount > 0 && --W.UnmetCount == 0
			&& !W.bDispatched.load(std::memory_order_relaxed))
		{
			BecameReady.push_back(Waiter);
		}
	}
	for (FNodeId Ready : BecameReady)
	{
		Dispatch(Ready);
	}

	// Release the phase's in-flight reference and the submission-fence count, then wake anyone
	// waiting on either.
	{
		std::lock_guard<std::mutex> Lock(SlotMutex);
		SlotInFlight[SlotIndexOf(Node.Key.Phase)].fetch_sub(1, std::memory_order_acq_rel);
		AwaitingCompletion.fetch_sub(1, std::memory_order_acq_rel);
	}
	SlotCv.notify_all();

	TryReap(Id);
}

void FFrameGraph::TryReap(FNodeId Id)
{
	FTaskNode& Node = Nodes[Id];

	// Reapable when the body has run, its completion is published, and nobody is still
	// waiting on it. Note this is best-effort: a node that still has Successors is NOT
	// reaped here, and is instead reset when it is next submitted.
	if (!Node.bDispatched.load(std::memory_order_relaxed)
		|| !EventStates[Node.CompletionEvent].load(std::memory_order_acquire)
		|| !Node.Successors.empty())
	{
		return;
	}

	Node.Dependencies.clear();
	Node.Successors.clear();
	Node.UnmetCount = 0;
	Node.Closure = nullptr;
	Node.bDispatched.store(false, std::memory_order_relaxed);
}

// ── the ring (host thread) ────────────────────────────────────────────────────

void FFrameGraph::WaitPhasesIdle(const std::vector<FTask>& Tasks)
{
	// Only the phases a task NAMES matter: a task's own phase is what gets (re)occupied.
	std::array<bool, kPhaseCount> Wanted{};
	for (const FTask& Task : Tasks)
	{
		if (PhaseIsKnown(Task.Key.Phase))
		{
			Wanted[static_cast<std::size_t>(SlotIndexOf(Task.Key.Phase))] = true;
		}
	}

	// The waits are sequential under one lock; each wait releases it. Waiters are woken by
	// every completion (SlotCv.notify_all in OnNodeCompleted).
	std::unique_lock<std::mutex> Lock(SlotMutex);
	for (std::size_t I = 0; I < Wanted.size(); ++I)
	{
		if (!Wanted[I])
		{
			continue;
		}
		SlotCv.wait(Lock, [this, I]()
		{
			return SlotInFlight[I].load(std::memory_order_acquire) == 0;
		});
	}
}

bool FFrameGraph::IsPhaseIdle(std::int32_t Phase) const
{
	if (!PhaseIsKnown(Phase))
	{
		return true;
	}
	return SlotInFlight[SlotIndexOf(Phase)].load(std::memory_order_acquire) == 0;
}

void FFrameGraph::Wait()
{
	// A submission FENCE: wait until every node submitted so far has completed. The awaited
	// count is raised by the CALLER in Submit (before the command is even enqueued), so there
	// is no window in which this could see "nothing outstanding" while a batch of ours is
	// still queued -- which is exactly what waiting on the phase in-flight counts would do.
	std::unique_lock<std::mutex> Lock(SlotMutex);
	SlotCv.wait(Lock, [this]()
	{
		return AwaitingCompletion.load(std::memory_order_acquire) == 0;
	});
}

std::size_t FFrameGraph::GetLiveNodeCount() const
{
	std::size_t Total = 0;
	for (const auto& Count : SlotInFlight)
	{
		Total += Count.load(std::memory_order_acquire);
	}
	return Total;
}

// ── the bridge (builds a batch; never touches the graph) ──────────────────────

std::int32_t FFrameBridge::PhaseOf(std::int32_t Frame) noexcept
{
	return ((Frame % kPhaseCount) + kPhaseCount) % kPhaseCount;
}

FFrameBridge::FResult FFrameBridge::Build(std::span<FFrameExtension* const> Frames,
	std::span<const std::type_index> Stages,
	std::int32_t Frame,
	IDispatch& Dispatch)
{
	FResult Result;

	// Pass 1: one node per (frame, stage) the frame actually IMPLEMENTS. This is the whole
	// empty-node pruning policy -- a frame that does not implement a stage contributes nothing
	// to the stage sequence, so it costs nothing to schedule.
	//
	// The same pass builds the STAGE CHAIN: a stage sequence means ORDERED stages, so each
	// emitted node waits for the previously emitted node of the SAME frame. Note that "previous"
	// is the previous EMITTED stage, not the previous stage in the list -- a skipped
	// (unimplemented) stage must not cut the chain into two independent halves, which is exactly
	// what an empty node would have been doing before (a no-op node whose whole job was to carry
	// the chain).
	std::unordered_map<FTaskKey, std::size_t, FTaskKeyHash> Index;
	for (FFrameExtension* F : Frames)
	{
		if (F == nullptr)
		{
			continue;
		}

		std::size_t Previous = NoNode;
		for (const std::type_index Stage : Stages)
		{
			if (!Dispatch.Implements(*F, Stage))
			{
				continue;
			}

			FTask Task;
			Task.Key = FTaskKey{ F->GetName(), Stage, PhaseOf(Frame) };
			Task.Closure = Dispatch.MakeClosure(*F, Stage);

			// STRUCTURAL EDGE 1 -- the stage SEQUENCE within this frame: each emitted node waits
			// for the previously emitted node. Chained over the EMITTED nodes, so a skipped
			// (unimplemented) stage cannot cut one frame's chain in two.
			if (Previous != NoNode)
			{
				Task.Dependencies.push_back(FDependency{ Result.Tasks[Previous].Key });
			}

			// STRUCTURAL EDGE 2 -- the stage IDENTITY over time: this stage waits for ITSELF one
			// frame earlier. The same stage of the same frame extension has the same job every
			// frame, so two of its instances running at once are two writers of one stage's state.
			//
			// It is deliberately PER STAGE, not per frame: a frame extension that owns per-frame
			// state of its own is expected to keep MAHO_FRAMES_IN_FLIGHT copies of it, which is
			// what lets frame N+1 overlap frame N. Serializing a whole frame against its previous
			// frame here would hide the absence of those copies inside the scheduler instead of
			// making it the render layer's job -- and that absence IS measurable: with a single
			// frame fence / a single acquired swapchain index, per-stage edges let S1@N+1 run
			// while S3@N is still submitting, and Vulkan validation reports exactly that
			// (VkFence "simultaneously used in vkQueueSubmit and vkWaitForFences").
			//
			// The target may not exist (frame 0, or a phase never occupied) -- the graph then
			// makes the edge disappear, exactly as it does for a declared one (R2).
			Task.Dependencies.push_back(FDependency{
				FTaskKey{ F->GetName(), Stage, PhaseOf(Frame - 1) } });

			Index.emplace(Task.Key, Result.Tasks.size());
			Previous = Result.Tasks.size();
			Result.Tasks.push_back(std::move(Task));
		}
	}

	// (name, stage, relative offset) -> the node it names, or nullptr when this batch has none.
	// Pointers, not indices: no element is added to Result.Tasks after pass 1, so they stay valid.
	auto Find = [&](std::string_view Name, std::type_index Stage, std::int32_t Offset) -> FTask*
	{
		const auto It = Index.find(FTaskKey{ Name, Stage, PhaseOf(Frame + Offset) });
		return It == Index.end() ? nullptr : &Result.Tasks[It->second];
	};

	// The frame set by name: the only thing a target can be checked against. Why only this --
	// whether the target's PHASE is in this batch is not a property of the declaration. A target
	// one frame back was built by the PREVIOUS Build call, and a target at this phase may come
	// from an earlier batch (a one-shot node keeps its identity for the rest of the run). Their
	// absence from THIS batch is expected, so reporting it would drown the author in noise on
	// every cross-frame edge and every install-time dependency.
	std::unordered_map<std::string_view, FFrameExtension*> ByName;
	for (FFrameExtension* F : Frames)
	{
		if (F != nullptr)
		{
			ByName.emplace(F->GetName(), F);
		}
	}

	/** null when the target is a plausible node, otherwise why it can never be one. */
	auto CheckTarget = [&](const FFrameExtension::FEdge& Edge) -> const char*
	{
		const auto It = ByName.find(Edge.TargetName);
		if (It == ByName.end())
		{
			return "target frame is not in this frame set";
		}
		if (std::find(Stages.begin(), Stages.end(), Edge.TargetStage) == Stages.end())
		{
			return "target stage is not in the stage sequence";
		}
		if (!Dispatch.Implements(*It->second, Edge.TargetStage))
		{
			return "target frame does not implement that stage";
		}
		return nullptr;
	};

	// A frame declares ALL of its edges ONCE, but it is expanded into several batches over the
	// run (the install batch, the tick batch, the unload batch). A declaration attached to a
	// stage that is not part of THIS batch's stage sequence therefore has no node here, and that
	// is not a problem: the batch whose sequence contains that stage is the one that processes
	// it, and reports it if the frame does not implement it. Staying silent here is what keeps
	// one frame's declarations from being reported once per batch.
	auto InSequence = [&](std::type_index Stage)
	{
		return std::find(Stages.begin(), Stages.end(), Stage) != Stages.end();
	};

	// Pass 2: forward declarations -- they hang on the DECLARER's node.
	for (FFrameExtension* F : Frames)
	{
		if (F == nullptr)
		{
			continue;
		}
		for (const auto& [MyStage, Edges] : F->GetDependencies())
		{
			if (!InSequence(MyStage))
			{
				continue;   // belongs to another batch of the same frame
			}

			FTask* Mine = Find(F->GetName(), MyStage, 0);
			if (Mine == nullptr)
			{
				// In this sequence, yet the frame got no node for it => it does not implement
				// that stage. Reported, never silently dropped.
				Result.Diagnostics.push_back({ "stage not implemented by this frame",
					F->GetName(), MyStage, {}, typeid(void), 0, false });
				continue;
			}

			for (const FFrameExtension::FEdge& Edge : Edges)
			{
				if (const char* Why = CheckTarget(Edge))
				{
					Result.Diagnostics.push_back({ Why, F->GetName(), MyStage, Edge.TargetName,
						Edge.TargetStage, Edge.FrameOffset, false });
				}

				// Emitted either way: the target may already be in the graph, and the graph
				// makes the edge disappear if it truly is not. See the edge policy in the header.
				Mine->Dependencies.push_back(FDependency{
					FTaskKey{ Edge.TargetName, Edge.TargetStage, PhaseOf(Frame + Edge.FrameOffset) } });
			}
		}
	}

	// Pass 3: reverse declarations. FTask::Blocks reads "these nodes are blocked BY me" (the
	// graph turns every entry into THEIR dependency on me), so the entry belongs on the
	// DECLARER's node and names the target -- not the other way around.
	for (FFrameExtension* F : Frames)
	{
		if (F == nullptr)
		{
			continue;
		}
		for (const auto& [MyStage, Edge] : F->GetDependents())
		{
			if (!InSequence(MyStage))
			{
				continue;   // belongs to another batch of the same frame
			}

			FTask* Mine = Find(F->GetName(), MyStage, 0);
			if (Mine == nullptr)
			{
				Result.Diagnostics.push_back({ "stage not implemented by this frame",
					F->GetName(), MyStage, {}, typeid(void), 0, true });
				continue;
			}

			if (const char* Why = CheckTarget(Edge))
			{
				Result.Diagnostics.push_back({ Why, F->GetName(), MyStage, Edge.TargetName,
					Edge.TargetStage, Edge.FrameOffset, true });
			}

			FTask* Target = Find(Edge.TargetName, Edge.TargetStage, Edge.FrameOffset);
			if (Target == nullptr)
			{
				// A reverse edge needs a node to land on, and this batch has none: the edge
				// vanishes -- the same outcome the graph would reach on its own. Not an extra
				// report: phase absence is expected (see CheckTarget's note).
				continue;
			}

			Mine->Blocks.push_back(FDependency{ Target->Key });
		}
	}

	return Result;
}

} // namespace Maho
