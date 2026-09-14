#include <Core/TaskGraph.h>

#include <Core/Fatal.h>

#include <chrono>
#include <fstream>   // TEMP
#include <functional>
#include <sstream>
#include <utility>

namespace Maho
{

void FTaskGraph::Init(std::vector<FNode*> Nodes)
{
	// The rebuild replaces the node storage that in-flight tasks index into.
	if (!IsIdle())
	{
		ReportError("task graph re-init while frames are in flight; draining first");
		WaitAll();
	}

	Tasks.clear();
	Lookup.clear();
	Gates.clear();
	Ring.clear();
	CompileErrorNode.clear();
	Tasks.reserve(Nodes.size());

	for (FNode* Node : Nodes)
	{
		if (Node == nullptr)
		{
			continue;
		}
		const std::size_t Index = Tasks.size();
		auto Task = std::make_unique<FTask>();
		Task->Node = Node;
		Tasks.push_back(std::move(Task));
		Lookup[{ Node->Name, Node->Stage }] = Index;
	}
}

bool FTaskGraph::Compile()
{
	CompileErrorNode.clear();
	if (Tasks.empty())
	{
		return true;
	}

	// Reset per-task graph state.
	for (const auto& Task : Tasks)
	{
		Task->Downstreams.clear();
		Task->InitPending = 0;
	}

	// Pass 1: wire edges + missing-dependency validation.
	for (std::size_t I = 0; I < Tasks.size(); ++I)
	{
		FTask& Task = *Tasks[I];
		for (const FTaskGraphDependency& Dep : Task.Node->Dependencies)
		{
			auto It = Lookup.find({ Dep.Name, Dep.Stage });
			if (It == Lookup.end())
			{
				CompileErrorNode = Task.Node->Name;   // the layer with the bad dep
				return false;                          // missing dependency
			}
			Tasks[It->second]->Downstreams.push_back(I);
			Task.InitPending += 1;
		}
	}

	// Pass 2: cycle detection (iterative DFS; a back-edge = cycle). A cycle has
	// no ready node and would otherwise hang Flush() forever. Iterative with an
	// explicit stack so a deep dependency chain cannot overflow the call stack.
	{
		std::vector<int> Color(Tasks.size(), 0);   // 0 = white, 1 = gray (in-stack), 2 = black
		std::vector<std::pair<std::size_t, std::size_t>> Stack;   // (task, next-dep-index)
		for (std::size_t V = 0; V < Tasks.size(); ++V)
		{
			if (Color[V] != 0)
			{
				continue;
			}
			Stack.emplace_back(V, 0);
			Color[V] = 1;
			while (!Stack.empty())
			{
				const std::size_t Cur = Stack.back().first;
				const auto& Deps = Tasks[Cur]->Node->Dependencies;
				if (Stack.back().second >= Deps.size())
				{
					Color[Cur] = 2;
					Stack.pop_back();
					continue;
				}
				const FTaskGraphDependency& Dep = Deps[Stack.back().second++];
				auto It = Lookup.find({ Dep.Name, Dep.Stage });
				if (It == Lookup.end())
				{
					continue;   // missing dep already reported in pass 1
				}
				const std::size_t U = It->second;
				if (Color[U] == 1)
				{
					CompileErrorNode = Tasks[Cur]->Node->Name;   // Cur is in the cycle
					break;
				}
				if (Color[U] == 0)
				{
					Stack.emplace_back(U, 0);
					Color[U] = 1;
				}
			}
			if (!CompileErrorNode.empty())
			{
				break;
			}
		}
		if (!CompileErrorNode.empty())
		{
			return false;   // cycle
		}
	}

	// Groups (node Name == the layer) + the cross-frame gate. A group must be a
	// CHAIN: exactly one root (no dependency on another node of the same group) and
	// exactly one sink (no same-group downstream). That is what makes the hand-over
	// count-free -- one claim at the root, one report at the sink -- so a malformed
	// group is reported instead of silently drifting (FLayerTaskGraph's stage chain
	// always satisfies it).
	std::map<std::string, std::uint32_t> GroupIndex;
	for (const auto& Task : Tasks)
	{
		const auto It = GroupIndex.try_emplace(Task->Node->Name, static_cast<std::uint32_t>(GroupIndex.size()));
		Task->Group = It.first->second;
	}
	Gates.clear();
	Gates.resize(GroupIndex.size());
	for (auto& Gate : Gates)
	{
		Gate = std::make_unique<FGroupGate>();
	}
	GroupNames.clear();
	GroupNames.resize(GroupIndex.size());
	for (const auto& [Name, Index] : GroupIndex)
	{
		GroupNames[Index] = Name;
	}
	std::vector<std::uint32_t> RootCount(GroupIndex.size(), 0);
	std::vector<std::uint32_t> SinkCount(GroupIndex.size(), 0);
	for (const auto& Task : Tasks)
	{
		bool bIntraGroupDep = false;
		for (const FTaskGraphDependency& Dep : Task->Node->Dependencies)
		{
			const auto It = Lookup.find({ Dep.Name, Dep.Stage });
			if (It != Lookup.end() && Tasks[It->second]->Group == Task->Group)
			{
				bIntraGroupDep = true;
				break;
			}
		}
		Task->bRoot = !bIntraGroupDep;
		Task->bSink = true;   // cleared below for every node that has a same-group successor
		if (Task->bRoot)
		{
			RootCount[Task->Group] += 1;
		}
	}
	for (const auto& Task : Tasks)
	{
		for (std::size_t Down : Task->Downstreams)
		{
			if (Tasks[Down]->Group == Task->Group)
			{
				Task->bSink = false;
				break;
			}
		}
		if (Task->bSink)
		{
			SinkCount[Task->Group] += 1;
		}
	}
	for (std::size_t G = 0; G < Gates.size(); ++G)
	{
		if (RootCount[G] != 1 || SinkCount[G] != 1)
		{
			for (const auto& Task : Tasks)
			{
				if (Task->Group == G)
				{
					CompileErrorNode = Task->Node->Name;
					break;
				}
			}
			ReportError((std::string("task graph: layer '") + CompileErrorNode
				+ "' is not a chain (roots=" + std::to_string(RootCount[G])
				+ ", sinks=" + std::to_string(SinkCount[G])
				+ "); the cross-frame gate needs exactly one of each").c_str());
			return false;
		}
	}

	// Frame ring: one independent state set per in-flight frame. Frame IDs stay
	// MONOTONIC across a rebuild (a reset made the idle guard blind to the previous
	// generation's live frames), and the rebuild RETIRES all history: it has just waited
	// for every recent frame (IsIdle/WaitAll above), so each slot's drained value can be
	// bumped straight to NextFrame - 1. Carrying the old values instead left a slot
	// "behind" and every later wait on it hung forever.
	const std::uint64_t Retired = NextFrame > 0 ? NextFrame - 1 : 0;
	RingDepth = MAHO_FRAMES_IN_FLIGHT > 0 ? MAHO_FRAMES_IN_FLIGHT : 1;
	Ring.clear();
	Ring.reserve(RingDepth);
	for (std::uint32_t I = 0; I < RingDepth; ++I)
	{
		auto Slot = std::make_unique<FFrameSlot>();
		Slot->Pending = std::make_unique<std::atomic<std::uint32_t>[]>(Tasks.size());
		Slot->Parked = std::make_unique<std::atomic<bool>[]>(Tasks.size());
		for (std::size_t T = 0; T < Tasks.size(); ++T)
		{
			Slot->Pending[T].store(0, std::memory_order_relaxed);
			Slot->Parked[T].store(false, std::memory_order_relaxed);
		}
		Slot->Serial.store(Retired, std::memory_order_release);
		Ring.push_back(std::move(Slot));
	}

	return true;
}

FTaskGraph::~FTaskGraph()
{
	// In-flight tasks dereference this graph (Tasks / Downstreams / Ring), so the
	// object must never die under them.
	if (!IsIdle())
	{
		ReportError("task graph destroyed with frames in flight; draining first");
		WaitAll();
	}
}

bool FTaskGraph::IsIdle() const noexcept
{
	if (Ring.empty())
	{
		return true;
	}
	// Only the last RingDepth frames can still be in flight, and they complete out of
	// order -- every one of them has to be drained, not just the newest.
	const std::uint64_t First = NextFrame > RingDepth ? NextFrame - RingDepth : 1;
	for (std::uint64_t Frame = First; Frame < NextFrame; ++Frame)
	{
		if (Ring[Frame % RingDepth]->Serial.load(std::memory_order_acquire) < Frame)
		{
			return false;
		}
	}
	return true;
}

void FTaskGraph::Execute()
{
	(void)SubmitFrame();
}

FFrameFence FTaskGraph::SubmitFrame()
{
	if (Tasks.empty() || Ring.empty())
	{
		return 0;
	}

	const std::uint64_t Frame = NextFrame++;
	FFrameSlot& Slot = *Ring[Frame % RingDepth];

	// Ring reuse: this slot belongs to frame - K. Waiting for THAT frame (and nothing
	// newer) is the whole point -- the frames in between keep executing.
	if (Frame > RingDepth)
	{
		WaitFence(Frame - RingDepth);
	}

	for (std::size_t I = 0; I < Tasks.size(); ++I)
	{
		Slot.Pending[I].store(Tasks[I]->InitPending, std::memory_order_relaxed);
		Slot.Parked[I].store(false, std::memory_order_relaxed);
	}
	Slot.Outstanding.store(static_cast<std::uint32_t>(Tasks.size()), std::memory_order_release);
	Slot.SubmitTickMs.store(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count(), std::memory_order_relaxed);
	Slot.bStallReported.store(false, std::memory_order_relaxed);

	// Gate the layer roots before dispatching: a root whose layer is still running an
	// earlier frame parks (and is then simply not dispatched) until that instance's
	// sink hands the layer over.
	for (std::size_t I = 0; I < Tasks.size(); ++I)
	{
		if (Tasks[I]->bRoot)
		{
			GateRegister(Tasks[I]->Group, I, Frame);
		}
	}

	for (std::size_t I = 0; I < Tasks.size(); ++I)
	{
		if (Slot.Pending[I].load(std::memory_order_relaxed) == 0
			&& !Slot.Parked[I].load(std::memory_order_relaxed))
		{
			SubmitTaskFor(I, Frame);
		}
	}

	// Frames in flight right now (this one included), for the high-water mark.
	std::uint32_t InFlight = 0;
	{
		const std::uint64_t FirstAlive = NextFrame > RingDepth ? NextFrame - RingDepth : 1;
		for (std::uint64_t F = FirstAlive; F <= Frame; ++F)
		{
			if (Ring[F % RingDepth]->Serial.load(std::memory_order_acquire) < F)
			{
				InFlight += 1;
			}
		}
	}
	if (InFlight > MaxInFlight.load(std::memory_order_relaxed))
	{
		MaxInFlight.store(InFlight, std::memory_order_relaxed);
	}

	return Frame;
}

void FTaskGraph::GateRegister(std::size_t Group, std::size_t Index, std::uint64_t Frame)
{
	FGroupGate& Gate = *Gates[Group];
	std::lock_guard<std::mutex> Lock(Gate.M);
	if (!Gate.bBusy)
	{
		Gate.bBusy = true;
		Gate.RunningFrame = Frame;
		return;
	}
	// The layer is still running an earlier frame: park this root. It is NOT
	// dispatched, and nothing is added to any pending count -- the parked entry IS
	// the dependency, and the running instance's sink pops exactly one of them.
	Gate.Waiting.emplace_back(Frame, Index);
	Ring[Frame % RingDepth]->Parked[Index].store(true, std::memory_order_relaxed);
}

void FTaskGraph::GateComplete(std::size_t Group, std::uint64_t Frame)
{
	FGroupGate& Gate = *Gates[Group];

	std::uint64_t ParkedFrame = 0;
	std::size_t   ParkedIndex = 0;
	bool          bHandOver = false;
	{
		std::lock_guard<std::mutex> Lock(Gate.M);
		if (!Gate.bBusy || Gate.RunningFrame != Frame)
		{
			// Bookkeeping hiccup: a stray/duplicate report, or a sink for an instance the
			// gate is not tracking. NEVER return here -- that would leave the layer busy
			// forever and park every later frame of it (a hiccup becomes a whole-graph
			// hang). Report and fall through to the normal hand-over instead: the FIFO
			// chain is still advanced exactly one step, every parked root is still
			// dispatched exactly once, and the worst case degrades to one extra overlap.
			ReportError((std::string("task graph: gate sink for layer '")
				+ (Group < GroupNames.size() ? GroupNames[Group] : std::string("?"))
				+ "' frame " + std::to_string(Frame)
				+ " reported against running frame " + std::to_string(Gate.RunningFrame)).c_str());
		}
		if (Gate.Waiting.empty())
		{
			Gate.bBusy = false;
			Gate.RunningFrame = 0;
			return;
		}
		// The instance finished: hand the layer over to exactly ONE parked root (FIFO).
		const auto Parked = Gate.Waiting.front();
		Gate.Waiting.pop_front();
		Gate.bBusy = true;
		Gate.RunningFrame = Parked.first;
		ParkedFrame = Parked.first;
		ParkedIndex = Parked.second;
		bHandOver = true;
	}
	if (bHandOver)
	{
		SubmitTaskFor(ParkedIndex, ParkedFrame);
	}
}

void FTaskGraph::WaitFence(FFrameFence Fence)
{
	if (Fence == 0 || Ring.empty())
	{
		return;
	}
	FFrameSlot& Slot = *Ring[Fence % RingDepth];
	std::unique_lock<std::mutex> Lock(Slot.M);
#ifndef NDEBUG
	// Stall audit (see MAHO_TASKGRAPH_STALL_MS): wake periodically and, once the frame
	// has been in flight way past a frame budget, say who is holding it -- then keep
	// waiting (reporting does not resolve it, it just makes the hang diagnosable).
	while (!Slot.Cv.wait_for(Lock, std::chrono::milliseconds(250), [&Slot, Fence]()
	{
		return Slot.Serial.load(std::memory_order_acquire) >= Fence;
	}))
	{
		const auto Now = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
		if (Now - Slot.SubmitTickMs.load(std::memory_order_relaxed) < MAHO_TASKGRAPH_STALL_MS)
		{
			continue;
		}
		if (Slot.bStallReported.exchange(true))
		{
			continue;   // report once per frame, then just keep waiting
		}
		ReportStall(Fence, Slot);
	}
#else
	Slot.Cv.wait(Lock, [&Slot, Fence]()
	{
		return Slot.Serial.load(std::memory_order_acquire) >= Fence;
	});
#endif
}

void FTaskGraph::ReportStall(std::uint64_t Frame, const FFrameSlot& Slot)
{
	std::ostringstream Out;
	Out << "task graph stall: waiting frame " << Frame << " NextFrame=" << NextFrame
		<< " depth=" << RingDepth << " (outstanding=" << Slot.Outstanding.load() << ")";
	const std::uint64_t First = NextFrame > RingDepth ? NextFrame - RingDepth : 1;
	for (std::uint64_t F = First; F < NextFrame; ++F)
	{
		const FFrameSlot& S = *Ring[F % RingDepth];
		Out << "\n  frame " << F << " serial=" << S.Serial.load()
			<< " outstanding=" << S.Outstanding.load() << " pendings:";
		for (std::size_t I = 0; I < Tasks.size(); ++I)
		{
			const std::uint32_t Pending = S.Pending[I].load();
			if (Pending == 0xFFFFFFFFu)
			{
				continue;   // finished
			}
			Out << " [" << I << "]" << Tasks[I]->Node->Name << "::" << Tasks[I]->Node->Stage.name()
				<< "=" << Pending << " deps=[";
			for (const FTaskGraphDependency& Dep : Tasks[I]->Node->Dependencies)
			{
				const auto It = Lookup.find({ Dep.Name, Dep.Stage });
				if (It != Lookup.end())
				{
					Out << It->second << ":" << Tasks[It->second]->Node->Name << " ";
				}
			}
			Out << "]";
			if (Pending == 0)
			{
				Out << "(RUNNING/BLOCKED)";
			}
		}
	}
	for (std::size_t G = 0; G < Gates.size(); ++G)
	{
		std::lock_guard<std::mutex> GateLock(Gates[G]->M);
		if (Gates[G]->bBusy || !Gates[G]->Waiting.empty())
		{
			Out << "\n  gate" << G << " runningFrame=" << Gates[G]->RunningFrame
				<< " busy=" << (Gates[G]->bBusy ? 1 : 0)
				<< " parked=" << Gates[G]->Waiting.size();
			for (const auto& Parked : Gates[G]->Waiting)
			{
				Out << " [parked f" << Parked.first << " idx" << Parked.second << "]";
			}
		}
	}
	ReportError(Out.str().c_str());
	// The log is buffered and a stalled process is usually killed: land the report on
	// disk too so the evidence survives.
	{
		std::ofstream File("TaskGraphStall.txt", std::ios::app);
		File << Out.str() << "\n";
		File.flush();
	}
}

void FTaskGraph::WaitAll()
{
	// The last RingDepth frames are the only ones that can be in flight; they finish
	// out of order, so drain each of them.
	const std::uint64_t First = NextFrame > RingDepth ? NextFrame - RingDepth : 1;
	for (std::uint64_t Frame = First; Frame < NextFrame; ++Frame)
	{
		WaitFence(Frame);
	}
}

void FTaskGraph::SubmitTaskFor(std::size_t Index, std::uint64_t Frame)
{
	Pool.Submit([this, Index, Frame]()
	{
		FTask& Task = *Tasks[Index];

		// Execute the node. A throwing stage method must not kill the host: report
		// it (non-fatal) and still release downstreams so the graph never hangs.
		try
		{
			ExecuteNodeFor(Index);
		}
		catch (const std::exception& E)
		{
			ReportError((std::string("layer stage threw: ") + E.what()
				+ " (node " + Task.Node->Name + ")").c_str());
		}
		catch (...)
		{
			ReportError((std::string("layer stage threw unknown exception (node ")
				+ Task.Node->Name + ")").c_str());
		}

		FFrameSlot& Slot = *Ring[Frame % RingDepth];

		// Report the layer's end before releasing the same-frame chain: the gate hands
		// the layer to the NEXT frame only once this instance's sink ran.
		if (Task.bSink)
		{
			GateComplete(Task.Group, Frame);
		}

		// Lock-free release: each predecessor fetch_sub's the downstream; the
		// LAST one (old value == 1) is the only one that submits it.
		std::vector<std::size_t> BecameReady;
		for (std::size_t Down : Task.Downstreams)
		{
			if (Slot.Pending[Down].fetch_sub(1, std::memory_order_acq_rel) == 1)
			{
				BecameReady.push_back(Down);
			}
		}
		for (std::size_t Down : BecameReady)
		{
			SubmitTaskFor(Down, Frame);
		}

		// Frame fence: the LAST node of the frame signals its slot.
		if (Slot.Outstanding.fetch_sub(1, std::memory_order_acq_rel) == 1)
		{
			std::lock_guard<std::mutex> Lock(Slot.M);
			Slot.Serial.store(Frame, std::memory_order_release);
			Slot.Cv.notify_all();
		}

#ifndef NDEBUG
		// Stall audit support: mark the node finished so a dump can tell "still
		// waiting" (pending > 0) from "dispatched but never came back" (pending == 0).
		Slot.Pending[Index].store(0xFFFFFFFFu, std::memory_order_relaxed);
#endif
	});
}

void FTaskGraph::ExecuteNodeFor(std::size_t Index)
{
	TraceTeardown((std::string("node ") + Tasks[Index]->Node->Name + "::"
		+ Tasks[Index]->Node->Stage.name()).c_str());
	ExecuteNode(Tasks[Index]->Node);
}

void FTaskGraph::Flush()
{
	WaitAll();
}

} // namespace Maho
