#include <Core/Task/TaskGraph.h>

#include <Core/Fatal.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Maho
{
namespace Core
{

namespace
{
	/** One node, as the graph stores it: an intrusive LINKED-LIST cell.
	 *
	 *  A cell is on exactly ONE of the graph's two chains at any time -- in flight, or free -- and a node
	 *  never moves in memory, so while it is in flight its address is stable. The address is the graph's
	 *  PRIVATE business: the outside world only ever names KEYS, and this cell is what a key resolves to.
	 *
	 *  `Generation` (which occupancy of this cell) and `bCompleted` are ATOMIC because a thread waiting on
	 *  a node reads them while the scheduler writes them. `Generation` is what makes that read meaningful:
	 *  the cell can be recycled and REUSED while a waiter sleeps, so a differing generation is how the
	 *  waiter learns that the occupancy it resolved is over -- which, since only a completed node is
	 *  recycled, means its node completed. */
	struct FNode
	{
		FNode*                            Next = nullptr;      // the chain we are on (or the free chain)
		FNode**                           PrevLink = nullptr;  // O(1) unlink: points at the link that owns us
		std::atomic<std::uint64_t>        Generation{ 0 };     // which occupancy; 0 = recycled (on the free chain)
		std::atomic<bool>                 bCompleted{ false };
		FTaskKey                          Key = INDEX_NONE;    // the identity the caller handed over
#if MAHO_DO_CHECK
		std::string_view                  DebugKey{};          // the name a dump or a refusal shows (diagnostics only)
#endif
		std::function<void()>             Body;
		std::uint32_t                     Unmet = 0;           // dependencies not yet completed
		std::vector<FNode*>               Unblocks;            // nodes whose counter I decrement when done
		std::uint32_t                     HeapPos = 0xFFFFFFFFu;
		std::uint64_t                     Sequence = 0;        // submission order: the tie-break
	};

	/** What a NAME means here: the node's name when diagnostic names are built in, its key otherwise -- so
	 *  every printer (a stall dump, a refusal) agrees on what to call a node.
	 *
	 *  The fallback formats the key into a per-thread buffer, so the pointer is valid until the next call
	 *  in the same thread. An EMPTY name is spelled out rather than passed on: a caller is free to leave
	 *  the diagnostic field alone, and `%s` on a null `data()` is not a diagnostic, it is undefined. */
	[[nodiscard]] const char* NameOf(const FNode* Node)
	{
#if MAHO_DO_CHECK
		if (Node->DebugKey.empty())
		{
			return "(unnamed)";
		}
		return Node->DebugKey.data();
#else
		thread_local char Buffer[24];
		std::snprintf(Buffer, sizeof(Buffer), "%016llx",
			static_cast<unsigned long long>(Node->Key));
		return Buffer;
#endif
	}

	/** The same policy for a description that has not become a node YET -- what a refusal names when the
	 *  submission it is refusing never made it into the graph. */
	[[nodiscard]] const char* NameOf(const FTaskDesc& Desc)
	{
#if MAHO_DO_CHECK
		if (Desc.DebugKey.empty())
		{
			return "(unnamed)";
		}
		return Desc.DebugKey.data();
#else
		thread_local char Buffer[24];
		std::snprintf(Buffer, sizeof(Buffer), "%016llx",
			static_cast<unsigned long long>(Desc.Key));
		return Buffer;
#endif
	}

	/** How long a wait may see no progress before it dumps what is outstanding. */
	constexpr int StallDumpSeconds = 3;

	constexpr std::uint32_t NoPos = 0xFFFFFFFFu;

	/** A node a waiter has RESOLVED, plus which occupancy it resolved. The waiter sleeps, and the cell may
	 *  be recycled and reused while it sleeps, so the occupancy is the signature that tells it "mine is
	 *  gone -- and since only a completed node is recycled, mine is done". */
	struct FWaitSlot
	{
		FNode*        Node = nullptr;
		std::uint64_t Occupancy = 0;
	};

	/** The reply to the one request whose answer the caller needs (resolving keys): it lives on the
	 *  CALLER's stack, and the caller outlives the wait by construction. */
	struct FResolveReply
	{
		void Answer(std::vector<FWaitSlot> InSlots)
		{
			{
				std::lock_guard Lock(Mutex);
				Slots = std::move(InSlots);
				bDone = true;
			}
			Cv.notify_all();
		}

		[[nodiscard]] std::vector<FWaitSlot> Await()
		{
			std::unique_lock Lock(Mutex);
			Cv.wait(Lock, [this] { return bDone; });
			return std::move(Slots);
		}

		std::mutex              Mutex;
		std::condition_variable Cv;
		std::vector<FWaitSlot>  Slots;
		bool                    bDone = false;
	};
}

struct FTaskGraph::FImpl
{
	FImpl(FTaskGraph& InOwner, FThreadPool& InPool, FThreadPool::FLane InLane)
		: Owner(InOwner)
		, Pool(InPool)
		, Lane(InLane)
	{
	}

	FTaskGraph&        Owner;
	FThreadPool&       Pool;
	FThreadPool::FLane Lane;

	// -- scheduler-owned state (touched ONLY by the scheduler thread) ------------

	FNode*                              Live = nullptr;  // in flight
	FNode*                              Free = nullptr;  // recycled storage (nothing is ever freed early)
	std::unordered_map<FTaskKey, FNode*> Table;          // the key → the node it names NOW
	std::uint64_t                       NextGeneration = 1;
	std::uint64_t                       NextSequence = 0;
	std::vector<FNode*>                 Heap;            // min-heap, key = (Unmet, Sequence)

	/** How many threads are inside `Wait` right now. `Wait` blocks, so one relaxed atomic is free there --
	 *  and it is what turns "destroyed under a waiter" from undefined behaviour into a caught bug (see the
	 *  destructor). */
	std::atomic<std::uint32_t> Waiters{ 0 };

	// -- the shared sink (the fence) --------------------------------------------

	mutable std::mutex      FenceMutex;
	std::condition_variable FenceCv;
	std::uint32_t           Outstanding = 0;

	// -- the chains (in flight / free) ------------------------------------------

	/** O(1) insertion: every chain is only ever pushed to its head. */
	void Link(FNode*& Chain, FNode* Node)
	{
		Node->Next = Chain;
		Node->PrevLink = &Chain;
		if (Chain != nullptr)
		{
			Chain->PrevLink = &Node->Next;
		}
		Chain = Node;
	}

	/** O(1) removal, without walking: the node knows the link that owns it (the intrusive "prev"). */
	void Unlink(FNode* Node)
	{
		if (Node->PrevLink == nullptr)
		{
			return;
		}
		*Node->PrevLink = Node->Next;
		if (Node->Next != nullptr)
		{
			Node->Next->PrevLink = Node->PrevLink;
		}
		Node->Next = nullptr;
		Node->PrevLink = nullptr;
	}

	/** A cell for a new node: a recycled one when there is one, else a fresh allocation. Either way the
	 *  storage is the graph's for the rest of its life, and the occupancy gets a new GENERATION. */
	FNode* AllocateNode()
	{
		FNode* Node = nullptr;
		if (Free != nullptr)
		{
			Node = Free;
			Unlink(Node);                              // off the free chain (O(1): PrevLink is its position)
			// Field by field: FNode holds atomics, so it has no assignment operator.
			Node->Next = nullptr;
			Node->PrevLink = nullptr;
			Node->bCompleted.store(false, std::memory_order_release);
			Node->Key = INDEX_NONE;
#if MAHO_DO_CHECK
			Node->DebugKey = {};
#endif
			Node->Body = nullptr;
			Node->Unmet = 0;
			Node->Unblocks.clear();
			Node->HeapPos = NoPos;
			Node->Sequence = 0;
		}
		else
		{
			Node = new FNode();
		}
		Node->Generation.store(NextGeneration++, std::memory_order_release);
		Node->Sequence = NextSequence++;
		Link(Live, Node);
		return Node;
	}

	/** The node is over: off the in-flight chain, onto the free chain. The generation goes to 0, which is
	 *  the signal a waiter reads ("the occupancy I resolved is gone"). The memory stays valid, so a waiter
	 *  that is still inside its loop can keep reading it safely. */
	void ReleaseNode(FNode* Node)
	{
		Node->Key = INDEX_NONE;
#if MAHO_DO_CHECK
		Node->DebugKey = {};
#endif
		Node->Body = nullptr;
		Node->Unblocks.clear();
		Node->HeapPos = NoPos;
		Node->Generation.store(0, std::memory_order_release);
		Node->bCompleted.store(false, std::memory_order_release);
		Unlink(Node);
		Link(Free, Node);
	}

	// -- heap (of nodes, keyed by the unmet counter then by submission order) -----

	[[nodiscard]] bool Less(FNode* Left, FNode* Right) const
	{
		if (Left->Unmet != Right->Unmet)
		{
			return Left->Unmet < Right->Unmet;
		}
		return Left->Sequence < Right->Sequence;
	}

	void Swap(std::uint32_t Left, std::uint32_t Right)
	{
		std::swap(Heap[Left], Heap[Right]);
		Heap[Left]->HeapPos = Left;
		Heap[Right]->HeapPos = Right;
	}

	void SiftUp(std::uint32_t Pos)
	{
		while (Pos > 0)
		{
			const std::uint32_t Parent = (Pos - 1) / 2;
			if (!Less(Heap[Pos], Heap[Parent]))
			{
				return;
			}
			Swap(Pos, Parent);
			Pos = Parent;
		}
	}

	void SiftDown(std::uint32_t Pos)
	{
		const std::uint32_t Count = static_cast<std::uint32_t>(Heap.size());
		for (;;)
		{
			const std::uint32_t Left = Pos * 2 + 1;
			const std::uint32_t Right = Left + 1;
			std::uint32_t Smallest = Pos;
			if (Left < Count && Less(Heap[Left], Heap[Smallest]))
			{
				Smallest = Left;
			}
			if (Right < Count && Less(Heap[Right], Heap[Smallest]))
			{
				Smallest = Right;
			}
			if (Smallest == Pos)
			{
				return;
			}
			Swap(Pos, Smallest);
			Pos = Smallest;
		}
	}

	void Push(FNode* Node)
	{
		Heap.push_back(Node);
		Node->HeapPos = static_cast<std::uint32_t>(Heap.size() - 1);
		SiftUp(Node->HeapPos);
	}

	/** The node's counter dropped: its key went DOWN, so it can only move towards the root. */
	void DecreaseKey(FNode* Node)
	{
		if (Node->HeapPos != NoPos)
		{
			SiftUp(Node->HeapPos);
		}
	}

	[[nodiscard]] FNode* Pop()
	{
		FNode* const Top = Heap.front();
		Top->HeapPos = NoPos;
		Heap.front() = Heap.back();
		Heap.pop_back();
		if (!Heap.empty())
		{
			Heap.front()->HeapPos = 0;
			SiftDown(0);
		}
		return Top;
	}

	// -- the scheduler thread's work -------------------------------------------

	/** Hands a ready node to the pool: the body runs on the WORK lane, and completion comes back as one
	 *  notification -- the worker never touches the table or the chains. */
	void Dispatch(FNode* Node)
	{
		Pool.Submit(Lane, [this, Node]() { RunBody(Node); });
	}

	/** Pops and dispatches everything whose dependencies are satisfied. Heap membership IS the "not yet
	 *  dispatched" state, so popping a node is exactly dispatching it. */
	void DispatchReady()
	{
		while (!Heap.empty() && Heap.front()->Unmet == 0)
		{
			Dispatch(Pop());
		}
	}

	// -- a pool worker's side --------------------------------------------------

	/** A node body threw. Reported WITH the node it was -- the name is the only thing that will point at
	 *  it -- and the node still counts as COMPLETED, so whatever waited for it keeps going. Both halves
	 *  matter: an escaping exception would take the pool's worker down with it (terminate), and a node that
	 *  threw but never completed would strand every successor.
	 *
	 *  `catch (...)` on purpose: a body is CALLER code, and a caller may throw something that is not a
	 *  `std::exception` (an int, a struct of its own). That is still "this node is over". */
	static void ReportNodeThrew(const FNode* Node, const char* What)
	{
		char Message[512];
		std::snprintf(Message, sizeof(Message), "FTaskGraph: node '%s' threw: %s", NameOf(Node), What);
		ReportError(Message);
	}

	void RunBody(FNode* Node)
	{
		std::function<void()> Body = Node->Body;

		try
		{
			Body();
		}
		catch (const std::exception& Error)
		{
			ReportNodeThrew(Node, Error.what());
		}
		catch (...)
		{
			ReportNodeThrew(Node, "a non-std::exception was thrown");
		}

		// ONE notification, no graph state touched: the scheduler does all of the bookkeeping.
		Owner.PostToLane([this, Node]()
		{
			try
			{
				OnBodyDone(Node);
			}
			catch (...)
			{
				// A completion's bookkeeping allocates too (a successor may have to enter the heap). Failing
				// half way through leaves the table and the heap disagreeing with no way to tell afterwards
				// -- and it is a state every later lookup reads. Fatal, not "reported and carried on".
				ReportFatal("FTaskGraph: a completion could not be applied (out of memory) -- the graph's "
					"state is no longer trustworthy");
			}
		});
	}

	// -- the scheduler's completion bookkeeping --------------------------------

	void OnBodyDone(FNode* Node)
	{
		// Publish BEFORE touching the graph: a thread waiting on this node reads these fields.
		Node->Body = nullptr;
		Node->bCompleted.store(true, std::memory_order_release);

		// The node leaves the GRAPH here, and both halves of that happen on the scheduler, so "the table
		// holds exactly the nodes in flight" is an invariant rather than a hope: its key is forgotten --
		// so a wait for it returns at once and a predecessor naming it is skipped -- and its storage is
		// recycled for the next submission.
		Table.erase(Node->Key);

		// Walk the successors BEFORE the cell is recycled (recycling clears the list).
		std::vector<FNode*>& Successors = Node->Unblocks;
		for (FNode* Successor : Successors)
		{
			if (Successor->Generation.load(std::memory_order_acquire) == 0
				|| Successor->bCompleted.load())
			{
				continue;                            // defensive: a successor of a pending node is pending
			}
			Successor->Unmet -= 1;
			if (Successor->Unmet == 0)
			{
				Push(Successor);
			}
			else
			{
				DecreaseKey(Successor);               // already in the heap: its key just went down
			}
		}

		ReleaseNode(Node);

		{
			std::lock_guard Lock(FenceMutex);
			--Outstanding;
		}
		FenceCv.notify_all();

		DispatchReady();
	}

	/** The scheduler's side of `Wait(keys)`: resolve each key to the node it names NOW, or drop it (it is
	 *  not in the graph, so it completed). */
	void ApplyResolve(std::span<const FTaskKey> Keys, FResolveReply& Reply)
	{
		std::vector<FWaitSlot> Slots;
		Slots.reserve(Keys.size());
		for (FTaskKey Key : Keys)
		{
			const auto It = Table.find(Key);
			if (It == Table.end())
			{
				continue;                            // already completed (or a key that never existed)
			}
			Slots.push_back(FWaitSlot{ It->second, It->second->Generation.load(std::memory_order_acquire) });
		}
		Reply.Answer(std::move(Slots));
	}

	void ApplySubmit(FTaskDesc Desc, std::span<const FTaskKey> Predecessors)
	{
		if (Desc.Key == INDEX_NONE)
		{
			ReportError("FTaskGraph::Submit: a description was submitted with INDEX_NONE -- its caller "
				"never computed a key (they would all share the one slot)");
			return;
		}
		if (Table.find(Desc.Key) != Table.end())
		{
			// A key that is STILL IN FLIGHT blocks the submission on the spot, not refuses it: the table
			// holds one node per key, so there is no right answer here -- merging drifts an edge's meaning,
			// overwriting loses a node's state, and dropping the submission would make an edge that SHOULD
			// exist silently disappear. All three hide the caller's naming bug; this shows it.
			//
			// What it is CALLED comes from `NameOf`, so the message says the same thing the graph says
			// everywhere: the caller's diagnostic name where those are built in, the key where they are not.
			char Message[224];
			std::snprintf(Message, sizeof(Message),
				"FTaskGraph::Submit (%s): '%s' is already in flight (a key names one live node)",
				__FILE__, NameOf(Desc));
			ReportFatal(Message);
		}

		FNode* Fresh = AllocateNode();
		Fresh->Key = Desc.Key;
#if MAHO_DO_CHECK
		Fresh->DebugKey = Desc.DebugKey;
#endif
		Fresh->Body = std::move(Desc.Body);

		// Resolve the edges BEFORE the key is in the table: a node can only be the "later" end of an edge,
		// so it must not be able to find itself (that is what makes the DAG acyclic by construction).
		for (FTaskKey Key : Predecessors)
		{
			const auto It = Table.find(Key);
			if (It == Table.end())
			{
				// Not in the graph: its node COMPLETED, so the constraint is already satisfied and an edge
				// to it would order nothing. Skipped silently, by design -- whether a key that never
				// existed at all was a mistake is the DECLARING layer's business.
				continue;
			}
			It->second->Unblocks.push_back(Fresh);
			Fresh->Unmet += 1;
		}

		// The table is the graph: everything it holds is in flight, and everything in flight is in it.
		Table.emplace(Fresh->Key, Fresh);

		{
			std::lock_guard Lock(FenceMutex);
			++Outstanding;
		}

		if (Fresh->Unmet == 0)
		{
			Push(Fresh);
		}
		DispatchReady();
	}

	/** Prints what is still in flight, with what each node is waiting for. Scheduler-side, so it walks the
	 *  in-flight chain without a lock -- and that chain is exactly the PENDING set (a completed node moves
	 *  to the free chain as it finishes). */
	void DumpOutstanding()
	{
		std::size_t Count = 0;
		for (FNode* Node = Live; Node != nullptr; Node = Node->Next)
		{
			++Count;
			std::fprintf(stderr, "[taskgraph]   %s blocked by:", NameOf(Node));
			bool bAny = false;
			for (FNode* Other = Live; Other != nullptr; Other = Other->Next)
			{
				if (Other == Node)
				{
					continue;
				}
				if (std::find(Other->Unblocks.begin(), Other->Unblocks.end(), Node)
					!= Other->Unblocks.end())
				{
					std::fprintf(stderr, " %s", NameOf(Other));
					bAny = true;
				}
			}
			std::fprintf(stderr, "%s\n", bAny ? "" : " (nothing -- a scheduling bug)");
		}
		std::fprintf(stderr, "[taskgraph] %zu node(s) outstanding\n", Count);
		std::fflush(stderr);
	}

	/** Frees every cell (the graph is going away, so nothing holds a key). Every cell is on exactly one of
	 *  the two chains. */
	void DeleteAll()
	{
		FNode* const Heads[2] = { Live, Free };
		for (FNode* Head : Heads)
		{
			while (Head != nullptr)
			{
				FNode* const Next = Head->Next;
				delete Head;
				Head = Next;
			}
		}
		Live = nullptr;
		Free = nullptr;
	}
};

FTaskGraph::FTaskGraph(FThreadPool& InPool, FThreadPool::FLane InLane)
	: Impl(std::make_unique<FImpl>(*this, InPool, InLane))
{
	BindPool(InPool);                     // the role lives on this pool (its own lane = the scheduler)
}

FTaskGraph::~FTaskGraph()
{
	// The graph must outlive its CALLERS (see the hardening list in the header): a thread inside `Wait`
	// holds a reference to the fence's mutex and CV, which die with this object -- undefined behaviour, and
	// the one case here that no layer can catch for us, so it is blocked rather than left to chance.
	if (const std::uint32_t WaiterCount = Impl->Waiters.load(std::memory_order_relaxed); WaiterCount != 0)
	{
		char Message[192];
		std::snprintf(Message, sizeof(Message),
			"FTaskGraph: destroyed while %u thread(s) were inside Wait -- the graph must outlive its callers",
			WaiterCount);
		ReportFatal(Message);
	}

	Shutdown();
	Impl->DeleteAll();
}

const char* FTaskGraph::GetThreadName() const
{
	return "TaskGraph";
}

void FTaskGraph::Shutdown()
{
	// Bodies run on the pool's WORK lane, so the fence comes before the role's own lane is released --
	// after that, a finishing body would have nowhere to report.
	Wait();
	Flush();
	FThreadedServer::Shutdown();
}

void FTaskGraph::Submit(FTaskDesc Desc, std::span<const FTaskKey> Predecessors)
{
	// The keys are copied into a vector AT CALL TIME: the caller's array is gone by the time the
	// scheduler looks at it.
	std::vector<FTaskKey> Keys(Predecessors.begin(), Predecessors.end());

	// Fire and forget: the scheduler applies it and nothing comes back -- the caller can name the node
	// again at any time, because a key is its own vocabulary.
	PostToLane([this, Desc = std::move(Desc), Keys = std::move(Keys)]() mutable
	{
		try
		{
			Impl->ApplySubmit(std::move(Desc), Keys);
		}
		catch (...)
		{
			// The graph's own bookkeeping allocates (a cell, the table, an edge list, the heap). A failure
			// in the middle of a state transition leaves the table and the heap disagreeing, and nothing
			// afterwards can tell -- so it is fatal, not "reported and carried on".
			ReportFatal("FTaskGraph: a submission could not be applied (out of memory) -- the graph's state "
				"is no longer trustworthy");
		}
	});
}

void FTaskGraph::Submit(std::span<const FTaskDesc> Descs, std::span<const FTaskKey> Predecessors,
	std::span<const FTaskKey> Completed)
{
	// The sugar: wait for what the batch must wait for, THEN submit node by node (so every node of the
	// batch is an ordinary single-node submission).
	Wait(Completed);

	for (const FTaskDesc& Desc : Descs)
	{
		Submit(Desc, Predecessors);
	}
}

void FTaskGraph::Wait(std::span<const FTaskKey> Keys)
{
	/** Counted for the destructor's sake, and released even if the wait throws on the way in: RAII, because
	 *  a leaked count would make a LATER, legitimate destruction abort. */
	struct FWaiterCount
	{
		std::atomic<std::uint32_t>& Count;
		explicit FWaiterCount(std::atomic<std::uint32_t>& InCount) : Count(InCount)
		{
			Count.fetch_add(1, std::memory_order_relaxed);
		}
		~FWaiterCount() { Count.fetch_sub(1, std::memory_order_relaxed); }
	} Counting(Impl->Waiters);

	// Drain this role's lane FIRST: `Submit` is fire-and-forget, so the requests posted before this call
	// may still be sitting on the scheduler's lane -- and a fence that ran now would count a submission
	// nobody has looked at yet and return early. After the flush, "outstanding" means what it says.
	Flush();

	// The caller's own key list next (it asked for it), then the submission fence.
	AwaitCompletions(Keys);

	std::unique_lock Lock(Impl->FenceMutex);
	while (Impl->Outstanding != 0)
	{
		if (Impl->FenceCv.wait_for(Lock, std::chrono::seconds(StallDumpSeconds))
			== std::cv_status::timeout)
		{
			// The dump walks the scheduler's chains, so it runs ON the scheduler: ask it to print and go
			// back to waiting. It is a diagnostic -- the wait itself is unaffected.
			PostToLane([this]() { Impl->DumpOutstanding(); });
		}
	}
}

void FTaskGraph::AwaitCompletions(std::span<const FTaskKey> Keys)
{
	if (Keys.empty() || !IsRunning())
	{
		return;
	}

	// The table is the scheduler's private state, so the keys have to be resolved there -- one request,
	// one answer, and what comes back is the node each key names NOW plus the occupancy it resolved.
	FResolveReply Reply;
	PostToLane([this, Keys, ReplyPtr = &Reply]()
	{
		try
		{
			Impl->ApplyResolve(Keys, *ReplyPtr);
		}
		catch (...)
		{
			// Resolving allocates the answer. Failing here would leave the CALLER waiting for an answer that
			// never comes -- a nameless hang, in a thread we do not own. Fatal is the honest alternative.
			ReportFatal("FTaskGraph: a waitlist could not be resolved (out of memory) -- a caller would be "
				"left waiting for an answer that cannot come");
		}
	});
	const std::vector<FWaitSlot> Slots = Reply.Await();

	for (const FWaitSlot& Slot : Slots)
	{
		std::unique_lock Lock(Impl->FenceMutex);
		// Two conditions, because the node may be RECYCLED and REUSED while we sleep: a different
		// generation means the occupancy we resolved is over, and only a completed node is recycled.
		while (Slot.Node->Generation.load(std::memory_order_acquire) == Slot.Occupancy
			&& !Slot.Node->bCompleted.load(std::memory_order_acquire))
		{
			if (Impl->FenceCv.wait_for(Lock, std::chrono::seconds(StallDumpSeconds))
				== std::cv_status::timeout)
			{
				// Still waiting: name the node we are stuck on AND ask the scheduler what it is doing.
				Lock.unlock();
				std::fprintf(stderr, "[taskgraph] Wait is stuck on a node (no completion after %ds)\n",
					StallDumpSeconds);
				std::fflush(stderr);
				PostToLane([this]() { Impl->DumpOutstanding(); });
				Lock.lock();
			}
		}
	}
}

}
}
