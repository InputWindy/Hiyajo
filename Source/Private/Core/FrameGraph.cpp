#include <Core/FrameGraph.h>

#include <Core/Fatal.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <set>
#include <string>
#include <utility>

namespace Maho
{

namespace
{
	/**
	 * Env-gated stage trace (MAHO_TRACE_STAGES=1). Read once, so it costs nothing when off and
	 * can be switched on without a rebuild.
	 *
	 * It exists for ONE failure mode: a hard crash (0xC0000005) has no stack and raises nothing
	 * C++ can catch, so the last stage ENTERED is the only thing that names the culprit. The
	 * body is bracketed (enter/exit) rather than merely logged at dispatch, which makes "the
	 * last enter with no exit" the answer. Note it is not a substitute for a stack -- it names
	 * the stage, not the line -- but it narrows a 34-DLL program to one call site.
	 *
	 * The switch is the configuration's, not the environment's: it belongs to the trace facility,
	 * so `MAHO_WITH_TRACE == 0` (Shipping) compiles the whole thing out -- there is no debugging
	 * the release build needs to bracket for. The call sites stay put: in Shipping they read a
	 * constant and call nothing, which the optimizer removes.
	 */
#if MAHO_WITH_TRACE

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

#else

	/** Shipping: not compiled, not switchable. */
	bool StageTraceEnabled()
	{
		return false;
	}

	/** Shipping: not compiled. */
	void TraceStage(const char*, const FTaskKey&)
	{
	}

#endif // MAHO_WITH_TRACE

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

// ── the pipeline depth K (one process-wide value, read by every graph) ────────

namespace
{
	/** The READER K comes from: installed by the layer that owns the `r.FramesInFlight` CVar (see
	 *  FrameGraph.h), called ONCE. A function pointer rather than a value because Core must not
	 *  depend on the CVar plugin; null in a process with no such layer (the test projects). */
	std::atomic<std::int32_t (*)()> GFramesInFlightSource{ nullptr };

	/** The resolved value: 0 == "not read yet". Written once (idempotent -- two threads racing here
	 *  compute the SAME answer), read by every later caller, and never moved again: the depth sizes
	 *  structures that cannot be re-sized while the process runs, which is why the CVar is ReadOnly.
	 *  Memoizing here is also what lets every consumer (the graph's tables, the RHI's slots, the
	 *  resource pool's buckets, the input ring) read ONE number instead of re-asking the config. */
	std::atomic<std::int32_t> GFramesInFlight{ 0 };
}

void SetFramesInFlightSource(std::int32_t (*InSource)()) noexcept
{
	GFramesInFlightSource.store(InSource, std::memory_order_release);
}

std::int32_t GetFramesInFlight() noexcept
{
	const std::int32_t Cached = GFramesInFlight.load(std::memory_order_acquire);
	if (Cached != 0)
	{
		return Cached;
	}

	// First read: ask the configuration. `MAHO_FRAMES_IN_FLIGHT` is the answer for a process that
	// never installed a reader; a non-positive answer is clamped here (a depth of 0 would make the
	// ring arithmetic divide by zero -- the CVar's own registered default is positive).
	// NOTE the `const` placement: it qualifies the POINTER, not the return type. `const std::int32_t
	// (*Source)()` would be "a pointer to a function returning `const std::int32_t`" -- a different
	// type from the one the reader was stored as, and therefore not convertible back out.
	std::int32_t (*const Source)() = GFramesInFlightSource.load(std::memory_order_acquire);
	std::int32_t Depth = Source != nullptr ? (*Source)() : MAHO_FRAMES_IN_FLIGHT;
	if (Depth <= 0)
	{
		Depth = 1;
	}
	GFramesInFlight.store(Depth, std::memory_order_release);
	return Depth;
}

bool FTaskKey::operator==(const FTaskKey& Other) const
{
	return Name == Other.Name && Stage == Other.Stage && Phase == Other.Phase;
}

std::size_t FTaskKeyHash::operator()(const FTaskKey& Key) const noexcept
{
	std::size_t H = std::hash<std::string_view>{}(Key.Name);
	H ^= std::hash<std::type_index>{}(Key.Stage) + 0x9e3779b9u + (H << 6) + (H >> 2);
	H ^= std::hash<std::int32_t>{}(Key.Phase) + 0x9e3779b9u + (H << 6) + (H >> 2);
	return H;
}

void FFrameExtension::AddDependency(std::type_index MyStage, FEdge Edge)
{
	Dependencies[MyStage].push_back(Edge);
}

void FFrameExtension::AddDependent(std::type_index MyStage, FEdge Edge)
{
	Dependents.emplace_back(MyStage, Edge);
}

FFrameGraph::FFrameGraph(FThreadPool& InPool, FThreadPool::FLane InLane)
	: Pool(InPool)
	, Lane(InLane)
{
	// K is NOT read here: the graph is a MEMBER of the frame that drives it, so it is constructed
	// before the plugins -- and therefore the CVar that owns K -- are loaded. It is resolved on
	// first use instead (EnsureDepth), which is also the moment the ring's tables can be sized.
}

/** Resolve K, once. Called by Submit on the CALLER's thread, before anything is enqueued, so the
 *  scheduler thread reads a published value and the whole ring is sized before its first node. */
void FFrameGraph::EnsureDepth()
{
	if (Depth != 0)
	{
		return;
	}

	Depth = GetFramesInFlight();
	MAHO_CHECKF(Depth > 0, "FFrameGraph: the pipeline depth resolved to %d -- the depth is a "
		"process-wide configuration value (r.FramesInFlight) and may never be zero", Depth);

	// The slot counters are the graph's half of "K frames in flight" (the admission wait reads
	// them); the event table's phase stride is the other half and is sized where it grows.
	// EXACTLY K slots: the phase index IS the slot index (`SlotIndexOf`), and both the admission
	// scan and the event-table stride index this vector BY PHASE.
	SlotInFlight = std::vector<std::atomic<std::uint32_t>>(static_cast<std::size_t>(Depth));
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

const char* FFrameGraph::GetThreadName() const
{
	return "FrameGraph";
}

// ── submission (host thread) ──────────────────────────────────────────────────


bool FFrameGraph::Submit(std::vector<FTask> Tasks, std::uint64_t Frame, std::string* OutReason)
{
	if (!IsRunning())
	{
		if (OutReason != nullptr) { *OutReason = "Submit before Initialize()"; }
		return false;
	}

	// THE PHASE IS OURS. The caller hands a ROUND; `Frame % K` is the ring arithmetic and it is
	// stamped here, onto every task's own key, so the batch's node identities belong to the slot
	// this submission occupies no matter what the caller put in them. (The bridge already computes
	// the same value for the resolution it does internally -- this is the authoritative stamp, not
	// a second opinion.) Dependency TARGETS are left alone on purpose: they name OTHER rounds, and
	// their phases were resolved one edge at a time by the bridge.
	//
	// K IS READ HERE, on the caller's thread, and only the first time (see EnsureDepth): it sizes
	// every phase-space table this graph owns, so it must be known before the batch is touched.
	EnsureDepth();

	const std::int32_t Phase = PhaseOfFrame(Frame);
	for (FTask& Task : Tasks)
	{
		Task.Key.Phase = Phase;
	}

	std::string Reason;
	if (!Validate(Tasks, Reason))
	{
		if (OutReason != nullptr) { *OutReason = std::move(Reason); }
		return false;
	}

	// Admission: block the CALLER until the slot this batch occupies has drained. This is
	// the ring-reuse rule AND the window (see the header): it is what makes "at most K frames in
	// flight" true, and -- symmetrically -- what PROVES that every frame at or before
	// `Frame - K` has completed (its slot could not have been free otherwise). It lives here
	// rather than in the caller so that the caller only ever needs Submit and Wait. The scheduler
	// thread must never perform this wait -- it would stop seeing the very completions the wait
	// depends on.
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
	// elements (std::atomic_bool is neither copyable nor movable). The stride is THIS graph's
	// depth (read once -- see EnsureDepth).
	const std::size_t Needed = (static_cast<std::size_t>(Id) + 1)
		* static_cast<std::size_t>(Depth);
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

		// The phase space is a runtime value (this graph's own depth), so it is passed in rather
		// than read from a constant.
		const FEvent Event = Detail::EventOf(Target, Nodes[Target].Key.Phase, Depth);

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
		Node.CompletionEvent = Detail::EventOf(Id, Task.Key.Phase, Depth);
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

	// The pool receives the node body as a plain closure: it runs work, it does not know what the
	// work IS. Any instrumentation is hand-written at the call site that knows it -- a stage body
	// opens its own bar (MAHO_TRACE_STAGE), not the graph and not the pool.
	Pool.Submit(Lane, [this, Id]()
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
	// Only the phases a task NAMES matter: a task's own phase is what gets (re)occupied. After
	// Submit's stamp they are all the batch's own phase, but the scan stays generic -- it is the
	// ADMISSION that must be right, not the shape of a batch.
	std::vector<char> Wanted(static_cast<std::size_t>(Depth), 0);
	for (const FTask& Task : Tasks)
	{
		if (PhaseIsKnown(Task.Key.Phase))
		{
			Wanted[static_cast<std::size_t>(SlotIndexOf(Task.Key.Phase))] = 1;
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
	// `SlotInFlight` is sized at first use (see EnsureDepth), so a graph that has never been given a
	// batch has no phase in flight by definition -- and the phase-space check alone would read past
	// an empty vector.
	if (SlotInFlight.empty() || !PhaseIsKnown(Phase))
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

std::int32_t FFrameBridge::PhaseOf(std::uint64_t Frame) noexcept
{
	// One forwarder, on purpose: the graph's Submit and this resolution must agree bit for bit,
	// so neither of them may own a private copy of `% K` (see PhaseOfFrame).
	return PhaseOfFrame(Frame);
}

FFrameBridge::FResult FFrameBridge::Build(std::span<FFrameExtension* const> Frames,
	std::span<const std::type_index> Stages,
	std::uint64_t Frame,
	IDispatch& Dispatch)
{
	FResult Result;

	// Pass 1: one node per (frame, stage) the frame actually IMPLEMENTS. This is the whole
	// empty-node pruning policy -- a frame that does not implement a stage contributes nothing
	// to the stage sequence, so it costs nothing to schedule.
	//
	// NO STRUCTURAL EDGE IS EMITTED HERE. The two that used to be invented at this point -- the
	// in-frame stage chain and the per-stage cross-frame self edge -- are now DECLARED by the frame
	// itself (MAHO_FRAME_STAGE_CHAIN / MAHO_FRAME_SELF_EDGES, installed by MAHO_DECLARE_FRAME in
	// Engine/Frame.h) and arrive through pass 2 like every other edge. They are properties of a
	// frame's OWN stage list, which only the frame has; a batch is not where ordering should appear
	// from nowhere. The graph keeps exactly three jobs: emit, resolve, admit.
	std::unordered_map<FTaskKey, std::size_t, FTaskKeyHash> Index;
	for (FFrameExtension* F : Frames)
	{
		if (F == nullptr)
		{
			continue;
		}

		for (const std::type_index Stage : Stages)
		{
			if (!Dispatch.Implements(*F, Stage))
			{
				continue;
			}

			FTask Task;
			Task.Key = FTaskKey{ F->GetName(), Stage, PhaseOf(Frame) };
			// The ROUND is handed to the closure, not merely to the node key: the node's own frame
			// number must reach the stage when the node RUNS, and `Frame % K` says nothing about how
			// many rounds later that is. Somebody mounting a counter per phase instead ("stamp the
			// round onto the slot this batch occupies") gets it wrong by construction -- measured:
			// a stage's own round advanced +4 then -2 across two runs of itself, because the slot
			// had been refilled by a later submission of the same phase before the node executed.
			Task.Closure = Dispatch.MakeClosure(*F, Stage, Frame);

			Index.emplace(Task.Key, Result.Tasks.size());
			Result.Tasks.push_back(std::move(Task));
		}
	}

	// (name, stage, relative offset) -> the node it names, or nullptr when this batch has none.
	// Pointers, not indices: no element is added to Result.Tasks after pass 1, so they stay valid.
	auto Find = [&](std::string_view Name, std::type_index Stage, std::int32_t Offset) -> FTask*
	{
		const auto It = Index.find(FTaskKey{ Name, Stage, PhaseOfShiftedFrame(Frame, Offset) });
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

	/** null when the target is a plausible node, otherwise why it can never be one.
	 *
	 *  Only what THIS batch can prove is judged here: the frame set it was given, and the stage
	 *  sequence it drives. A target stage that is not part of this sequence never reaches this lambda
	 *  any more -- it is skipped as another batch's declaration (see IsOtherBatchDeclaration), because
	 *  this batch has no node for it and cannot say whether the batch that owns that stage can bind it.
	 *  What is left is the authoring error this batch CAN prove: the named stage IS in this sequence,
	 *  so a node for it had to exist, and the named frame does not mount it. */
	auto CheckTarget = [&](const FFrameExtension::FEdge& Edge) -> const char*
	{
		const auto It = ByName.find(Edge.TargetName);
		if (It == ByName.end())
		{
			return "target frame is not in this frame set";
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

	/** True when this declaration belongs to ANOTHER batch of the same frame -- the exact mirror of
	 *  the `InSequence(MyStage)` skip the two passes below already do on the DECLARER's side.
	 *
	 *  ONE frame's stage list routinely spans several batches: the engine's own frames mount init,
	 *  tick AND shutdown stages, and a world system mounts IProcessInput in one sequence and IUpdate
	 *  in another. A declaration whose TARGET stage is not in THIS batch's sequence therefore has no
	 *  node to land on here, and it is the batch whose sequence contains that stage that processes
	 *  it. Reporting it would fire on every frame of every pipeline (`target stage is not in the
	 *  stage sequence`), and emitting it would put a dependency on a node this batch cannot even
	 *  emit into every batch -- which is why the STRUCTURAL declarations the frames now carry
	 *  depend on this rule to stay exactly as expensive as the edges they replaced.
	 *
	 *  WHY THERE IS NO `Implements` TEST HERE: `IDispatch::Implements` is scoped to THIS BATCH'S STAGE
	 *  SEQUENCE (`TFrameDispatch::ImplementsImpl` walks its own `FStages`), so for a target stage that
	 *  is not in the sequence it answers false BY CONSTRUCTION. Guarding the skip with it therefore
	 *  made the rule unreachable -- it never fired once -- and every legal cross-batch declaration was
	 *  reported as an authoring error (measured: 9 per run, e.g. "FLog waits for FLog at IInit at
	 *  ITick", which is the pair (IInit, ITick) of FLog's OWN list seen from the TICK batch, whose
	 *  sequence simply does not contain IInit).
	 *
	 *  The honest question is the only one asked now: "is the target stage one of MINE?" A stage that
	 *  is not is not this batch's to judge -- whether the batch that does own it can bind the edge is
	 *  that batch's business (and the frame's own stage list is what makes "another batch owns it" true
	 *  in the first place). What stays reported is what this batch can actually prove: a target stage
	 *  that IS in this sequence and still produces no node -- see CheckTarget. */
	auto IsOtherBatchDeclaration = [&](const FFrameExtension::FEdge& Edge) -> bool
	{
		return !InSequence(Edge.TargetStage);
	};

	// Pass 2: forward declarations -- they hang on the DECLARER's node.
	std::size_t DeclaredEdges = 0;   // for the Debug self-check at the bottom of this function
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
				// The legal offset domain is `<= 0` (see FFrameExtension::FEdge). A FORWARD offset
				// cannot be honoured: there is no such round to wait for, and resolving it anyway
				// would silently land on some other round's slot. Reported, and no edge emitted.
				if (Edge.FrameOffset > 0)
				{
					Result.Diagnostics.push_back({ "frame offset must be <= 0 (a wait points backwards)",
						F->GetName(), MyStage, Edge.TargetName, Edge.TargetStage, Edge.FrameOffset, false });
					continue;
				}

				// Another batch of this frame owns the target stage: not this batch's business
				// (see IsOtherBatchDeclaration). Silent, and no edge -- the edge belongs to the
				// batch whose sequence holds that stage.
				if (IsOtherBatchDeclaration(Edge))
				{
					continue;
				}

				if (const char* Why = CheckTarget(Edge))
				{
					Result.Diagnostics.push_back({ Why, F->GetName(), MyStage, Edge.TargetName,
						Edge.TargetStage, Edge.FrameOffset, false });
				}

				// Outside the WINDOW (`Offset <= -K`): that target frame is PROVEN complete -- its
				// slot had to drain before THIS frame could even be submitted -- so the wait is
				// satisfied by definition and NO edge is emitted. Emitting the phase-space edge
				// instead would point at the slot of the frame K rounds LATER (in the ring, `-K` and
				// `0` are the same index), i.e. at the wrong round entirely. This one comparison is
				// what makes any offset expressible without a watermark or a per-frame ledger.
				if (!IsFrameInsideWindow(Edge.FrameOffset))
				{
					continue;
				}

				// Emitted either way: the target may already be in the graph, and the graph
				// makes the edge disappear if it truly is not. See the edge policy in the header.
				Mine->Dependencies.push_back(FDependency{
					FTaskKey{ Edge.TargetName, Edge.TargetStage,
						PhaseOfShiftedFrame(Frame, Edge.FrameOffset) } });
				++DeclaredEdges;
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

			if (Edge.FrameOffset > 0)
			{
				Result.Diagnostics.push_back({ "frame offset must be <= 0 (a wait points backwards)",
					F->GetName(), MyStage, Edge.TargetName, Edge.TargetStage, Edge.FrameOffset, true });
				continue;
			}

			// Another batch of this frame owns the target stage: a reverse edge needs a node to
			// land on, and this batch has none -- so the declaration is dropped here and processed
			// by the batch whose sequence contains that stage (see IsOtherBatchDeclaration).
			if (IsOtherBatchDeclaration(Edge))
			{
				continue;
			}

			if (const char* Why = CheckTarget(Edge))
			{
				Result.Diagnostics.push_back({ Why, F->GetName(), MyStage, Edge.TargetName,
					Edge.TargetStage, Edge.FrameOffset, true });
			}

			// Outside the window: already satisfied, and there is no node in this batch to land on
			// anyway (see the forward pass for why the edge must not be phase-resolved instead).
			if (!IsFrameInsideWindow(Edge.FrameOffset))
			{
				continue;
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

#if MAHO_DO_CHECK
	// ── the STRUCTURAL SELF-CHECK: "the batch is what the old emitter would have produced" ───────
	//
	// The two structural edges now arrive as DECLARATIONS (MAHO_FRAME_STAGE_CHAIN /
	// MAHO_FRAME_SELF_EDGES, installed by MAHO_DECLARE_FRAME) instead of being invented here. The
	// claim that this is an equivalence -- not a change of schedule -- is checkable from inside the
	// batch, so it IS checked here, in diagnostics builds:
	//
	//   * the node set is untouched by the move (pass 1 never changed), and
	//   * every node still carries exactly the two edges the old emitter added (the chain edge to
	//     the previous EMITTED node of its own frame, and the self edge one frame back), and
	//   * the batch carries no dependency beyond (declared + those two) -- a frame whose declaration
	//     COINCIDES with one of them is fine (the count is of pushes, and the graph deduplicates),
	//     an undeclared extra would mean somebody is still inventing edges.
	//
	// A frame that forgets its structural declarations cannot exist (the macro installs them), but
	// a DECLARATION THAT DOES NOT BIND can: this is where it becomes visible, once per batch shape.
	//
	// THE EXPECTATION IS COMPUTED AT OFFSET 0 -- against the batch's OWN FRAME -- and never against
	// `PhaseOf(Frame)`. `Find`'s third argument is a FRAME OFFSET, so handing it the phase asks for the
	// node of frame `Frame + phase`: for every phase but 0 that is not this batch's node, the lookup
	// misses EVERY node, `ExpectedStructural` and `Missing` both stay 0 and the guard silently passes.
	// Measured cost of that mistake: with the default K=3 exactly one frame in three was checked.
	{
		std::size_t ExpectedStructural = 0;
		std::size_t Missing = 0;
		std::size_t Actual = 0;
		for (const FTask& Task : Result.Tasks)
		{
			Actual += Task.Dependencies.size();
		}

		auto HasDependency = [](const FTask& Task, const FTaskKey& Target)
		{
			for (const FDependency& D : Task.Dependencies)
			{
				if (D.Target == Target)
				{
					return true;
				}
			}
			return false;
		};

		/** The first edge that did not reach its node, spelled as "<frame>@<stage> <- <edge>", so the
		 *  message below NAMES the frame and the edge instead of only counting them. */
		char        FirstMissing[256] = { 0 };
		const auto  NoteMissing = [&FirstMissing](const FFrameExtension& F, std::type_index Stage,
			const char* Which)
		{
			if (FirstMissing[0] == 0)
			{
				std::snprintf(FirstMissing, sizeof(FirstMissing), "%.*s@%s <- %s",
					static_cast<int>(F.GetName().size()), F.GetName().data(), Stage.name(), Which);
			}
		};

		// The self edge points one round back, so it is declared only while that round is still inside
		// the window -- the SAME rule pass 2 drops the edge with (`IsFrameInsideWindow(-1)`; at K=1 the
		// previous round is already complete and no edge is emitted). Expecting it there would invent a
		// failure out of a correct omission.
		const bool bExpectSelfEdge = IsFrameInsideWindow(-1);

		for (FFrameExtension* F : Frames)
		{
			if (F == nullptr)
			{
				continue;
			}
			const FTask* PreviousEmitted = nullptr;
			for (const std::type_index Stage : Stages)
			{
				FTask* Node = Find(F->GetName(), Stage, 0);
				if (Node == nullptr)
				{
					continue;   // this frame does not implement this stage: no node, no edges
				}
				if (PreviousEmitted != nullptr)
				{
					++ExpectedStructural;
					if (!HasDependency(*Node, PreviousEmitted->Key))
					{
						++Missing;
						NoteMissing(*F, Stage, "the in-frame stage chain");
					}
				}
				if (bExpectSelfEdge)
				{
					++ExpectedStructural;
					if (!HasDependency(*Node,
						FTaskKey{ F->GetName(), Stage, PhaseOfShiftedFrame(Frame, -1) }))
					{
						++Missing;
						NoteMissing(*F, Stage, "the cross-frame self edge");
					}
				}
				PreviousEmitted = Node;
			}
		}

		MAHO_CHECKF(Missing == 0,
			"FFrameBridge::Build: %zu structural edge(s) missing from the batch (frame %llu, %zu "
			"node(s), first: %s) -- a frame's stage chain / self edge declaration did not reach its node",
			Missing, static_cast<unsigned long long>(Frame), Result.Tasks.size(), FirstMissing);
		// The batch's push count must be EXACTLY the number of declarations the two passes emitted --
		// and the structural edges are DECLARATIONS too now (MAHO_FRAME_STAGE_CHAIN /
		// MAHO_FRAME_SELF_EDGES expand to AddDependency calls, so they are already INSIDE
		// `DeclaredEdges`). Adding `ExpectedStructural` on top would therefore count them twice: the
		// structural expectation is the claim `Missing == 0` above (the edges that MUST be present),
		// and this comparison is the other half -- nothing was pushed that no declaration asked for,
		// and no in-window declaration failed to become a push. (Measured on the install batch: 72
		// pushes against `declared=72`, of which `structural=59`.)
		MAHO_CHECKF(Actual == DeclaredEdges,
			"FFrameBridge::Build: batch carries %zu dependency(ies) but %zu declaration(s) were "
			"emitted (%zu of them structural, frame %llu) -- a dependency was pushed by something "
			"other than a declaration, or an in-window declaration produced no push",
			Actual, DeclaredEdges, ExpectedStructural, static_cast<unsigned long long>(Frame));

		// One line per DISTINCT batch SHAPE (node count + per-node dependency counts + declared
		// edge count, independent of the frame number), so a run states once what it builds every
		// frame -- the evidence that the relocation did not change the schedule.
		{
			std::string Digest = "nodes=" + std::to_string(Result.Tasks.size())
				+ " structural=" + std::to_string(ExpectedStructural)
				+ " declared=" + std::to_string(DeclaredEdges) + " deps=[";
			std::size_t I = 0;
			for (const FTask& Task : Result.Tasks)
			{
				Digest += (I++ == 0 ? "" : ",");
				Digest += std::to_string(Task.Dependencies.size());
			}
			Digest += "]";

			static std::mutex           DigestMutex;
			static std::set<std::string> SeenDigests;
			static bool                  bStopLogging = false;
			std::lock_guard<std::mutex> Lock(DigestMutex);
			if (SeenDigests.size() > 64)
			{
				bStopLogging = true;
			}
			if (!bStopLogging && SeenDigests.insert(Digest).second)
			{
				std::fprintf(stderr, "[fgbatch] %s\n", Digest.c_str());
				std::fflush(stderr);
			}
		}
	}
#endif

	return Result;
}

} // namespace Maho
