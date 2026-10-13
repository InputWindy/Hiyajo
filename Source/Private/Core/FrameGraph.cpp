#include <Core/FrameGraph.h>

#include <Core/Fatal.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Maho
{

namespace
{
#if MAHO_WITH_TRACE

	bool StageTraceEnabled()
	{
		static const bool bOn = (std::getenv("MAHO_TRACE_STAGES") != nullptr);
		return bOn;
	}

	void TraceStage(const char* What, const FTaskKey& Key)
	{
		std::fprintf(stderr, "[fg] %s %.*s::%.*s@%d\n", What,
			static_cast<int>(Key.Name.size()), Key.Name.data(),
			static_cast<int>(Key.Stage.size()), Key.Stage.data(), Key.Phase);
		std::fflush(stderr);
	}

#else

	bool StageTraceEnabled()
	{
		return false;
	}

	void TraceStage(const char*, const FTaskKey&)
	{
	}

#endif

	std::int32_t SlotIndexOf(std::int32_t Phase)
	{
		return Phase;
	}

	bool PhaseIsKnown(std::int32_t Phase)
	{
		return IsValidPhase(Phase);
	}

	thread_local char GCurrentGraphNode[192] = { 0 };

	struct FScopedGraphNode
	{
		char Previous[192] = { 0 };

		explicit FScopedGraphNode(const char* Identity) noexcept
		{
			std::memcpy(Previous, GCurrentGraphNode, sizeof(Previous));
			std::snprintf(GCurrentGraphNode, sizeof(GCurrentGraphNode), "%s",
				Identity != nullptr ? Identity : "");
		}
		~FScopedGraphNode() { std::memcpy(GCurrentGraphNode, Previous, sizeof(Previous)); }
	};

	bool NodeDumpEnabled()
	{
		static const bool bOn = (std::getenv("MAHO_GRAPH_NODES") != nullptr);
		return bOn;
	}

	using FEvent = std::uint32_t;
	constexpr FEvent InvalidEvent = 0xFFFFFFFFu;

	using FNodeId = std::uint32_t;
	constexpr FNodeId InvalidNodeId = 0xFFFFFFFFu;

	FEvent EventOf(FNodeId Id, std::int32_t Phase, std::int32_t PhaseCount) noexcept
	{
		return static_cast<FEvent>(Id * static_cast<std::uint32_t>(PhaseCount)
			+ static_cast<std::uint32_t>(Phase));
	}

	struct FTaskNode
	{
		FTaskKey             Key{};
		std::vector<FEvent>  Dependencies;
		std::uint32_t        UnmetCount = 0;
		std::vector<FNodeId> Successors;
		FEvent               CompletionEvent = InvalidEvent;
		std::function<void()> Closure;

		std::string          NameSnapshot;

		std::atomic<bool>    bDispatched{ false };
	};
}


namespace
{
	std::atomic<std::int32_t (*)()> GFramesInFlightSource{ nullptr };

	std::atomic<std::int32_t> GFramesInFlight{ 0 };
}

void SetFramesInFlightSource(std::int32_t (*InSource)()) noexcept
{
	GFramesInFlightSource.store(InSource, std::memory_order_release);
}

const char* GetCurrentGraphNode() noexcept
{
	return GCurrentGraphNode;
}

std::int32_t GetFramesInFlight() noexcept
{
	const std::int32_t Cached = GFramesInFlight.load(std::memory_order_acquire);
	if (Cached != 0)
	{
		return Cached;
	}

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
	H ^= std::hash<std::string_view>{}(Key.Stage) + 0x9e3779b9u + (H << 6) + (H >> 2);
	H ^= std::hash<std::int32_t>{}(Key.Phase) + 0x9e3779b9u + (H << 6) + (H >> 2);
	return H;
}

struct FFrameExtension::FImpl
{
	std::unordered_map<std::string_view, std::vector<FEdge>> Dependencies;
	std::vector<std::pair<std::string_view, FEdge>>          Dependents;
};

FFrameExtension::FFrameExtension()
	: Impl(std::make_unique<FImpl>())
{
}

FFrameExtension::~FFrameExtension() = default;

std::string_view FFrameExtension::GetCollectorName() const noexcept
{
	return {};
}

bool FFrameExtension::ImplementsStage(std::string_view Stage) const
{
	(void)Stage;
	return false;
}

void* FFrameExtension::GetStageInstance(std::string_view Stage) noexcept
{
	(void)Stage;
	return nullptr;
}

void* FFrameExtension::QuerySubobject(const void* TargetTag) noexcept
{
	(void)TargetTag;
	return nullptr;
}

void* FFrameExtension::GetCollectorInterface() noexcept
{
	return nullptr;
}

void FFrameExtension::AddDependency(std::string_view MyStage, FEdge Edge)
{
	Impl->Dependencies[MyStage].push_back(Edge);
}

void FFrameExtension::AddDependent(std::string_view MyStage, FEdge Edge)
{
	Impl->Dependents.emplace_back(MyStage, Edge);
}

const std::unordered_map<std::string_view, std::vector<FFrameExtension::FEdge>>&
FFrameExtension::GetDependencies() const
{
	return Impl->Dependencies;
}

const std::vector<std::pair<std::string_view, FFrameExtension::FEdge>>&
FFrameExtension::GetDependents() const
{
	return Impl->Dependents;
}

struct FFrameGraph::FImpl
{
	FImpl(FFrameGraph& InOwner, FThreadPool& InPool, FThreadPool::FLane InLane);

	FNodeId GetOrCreateNodeId(const FTaskKey& Key);
	void    ApplySubmit(std::vector<FTask> Tasks);
	void    OnNodeCompleted(FNodeId Id);
	void    Dispatch(FNodeId Id);
	void    TryReap(FNodeId Id);
	bool    Validate(const std::vector<FTask>& Tasks, std::string& OutReason) const;


	[[nodiscard]] static std::string NodeIdentity(const FTaskNode& Node);

	[[nodiscard]] std::string DescribeNode(const FTaskNode& Node) const;

	void DumpOutstanding(const char* Why);

	void RequestOutstandingDump(const char* Why);

	void ReportNodeFailure(FNodeId Id, const char* What);
	void ReportNodeFailureOnScheduler(FNodeId Id, std::string What);

	static constexpr std::int32_t StallDumpSeconds = 3;

	void EnsureDepth();

	[[nodiscard]] std::int32_t GetDepth() const noexcept { return Depth; }

	void WaitPhasesIdle(const std::vector<FTask>& Tasks);

	[[nodiscard]] bool        IsPhaseIdle(std::int32_t Phase) const;
	[[nodiscard]] std::size_t GetLiveNodeCount() const;

	FNodeId ResolveTarget(const FTaskKey& Target);

	void WireNode(FTaskNode& Node, const FTask& Decl);

	bool Submit(std::vector<FTask> Tasks, std::uint64_t Frame, std::string* OutReason);

	void Wait();

	FThreadedServer&   Server;
	FThreadPool&       Pool;
	FThreadPool::FLane Lane;

	const char* OwnerName = "";

	std::deque<FTaskNode>                               Nodes;
	std::unordered_map<FTaskKey, FNodeId, FTaskKeyHash> Registry;
	std::deque<std::atomic_bool>                        EventStates;

	std::int32_t Depth = 0;

	std::vector<std::atomic<std::uint32_t>> SlotInFlight;

	std::atomic<std::uint32_t> AwaitingCompletion{ 0 };

	std::condition_variable SlotCv;
	mutable std::mutex      SlotMutex;
};

FFrameGraph::FImpl::FImpl(FFrameGraph& InOwner, FThreadPool& InPool, FThreadPool::FLane InLane)
	: Server(InOwner)
	, Pool(InPool)
	, Lane(InLane)
{
}

FFrameGraph::FFrameGraph(FThreadPool& InPool, FThreadPool::FLane InLane)
	: Impl(std::make_unique<FImpl>(*this, InPool, InLane))
{
}

FFrameGraph::~FFrameGraph()
{
	FThreadedServer::Shutdown();
}

const char* FFrameGraph::GetThreadName() const
{
	return "FrameGraph";
}

bool FFrameGraph::Submit(std::vector<FTask> Tasks, std::uint64_t Frame, std::string* OutReason)
{
	return Impl->Submit(std::move(Tasks), Frame, OutReason);
}

void FFrameGraph::Wait()
{
	Impl->Wait();
}

void FFrameGraph::SetOwnerName(const char* InOwnerName)
{
	Impl->OwnerName = InOwnerName;
}

void FFrameGraph::FImpl::EnsureDepth()
{
	if (Depth != 0)
	{
		return;
	}

	Depth = GetFramesInFlight();
	MAHO_CHECKF(Depth > 0, "FFrameGraph: the pipeline depth resolved to %d -- the depth is a "
		"process-wide configuration value (r.FramesInFlight) and may never be zero", Depth);

	SlotInFlight = std::vector<std::atomic<std::uint32_t>>(static_cast<std::size_t>(Depth));
	for (auto& Count : SlotInFlight)
	{
		Count.store(0, std::memory_order_relaxed);
	}
}

bool FFrameGraph::FImpl::Submit(std::vector<FTask> Tasks, std::uint64_t Frame, std::string* OutReason)
{
	if (!Server.IsRunning())
	{
		if (OutReason != nullptr) { *OutReason = "Submit before Initialize()"; }
		return false;
	}

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

	WaitPhasesIdle(Tasks);

	{
		std::lock_guard<std::mutex> Lock(SlotMutex);
		AwaitingCompletion.fetch_add(static_cast<std::uint32_t>(Tasks.size()),
			std::memory_order_acq_rel);
	}

	Server.Submit([this, Batch = std::move(Tasks)]() mutable
	{
		ApplySubmit(std::move(Batch));
	});
	return true;
}

bool FFrameGraph::FImpl::Validate(const std::vector<FTask>& Tasks, std::string& OutReason) const
{
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

	{
		std::unordered_map<FTaskKey, int, FTaskKeyHash> Color;

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
						continue;
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


FNodeId FFrameGraph::FImpl::GetOrCreateNodeId(const FTaskKey& Key)
{
	const auto It = Registry.find(Key);
	if (It != Registry.end())
	{
		return It->second;
	}

	const FNodeId Id = static_cast<FNodeId>(Nodes.size());
	Nodes.emplace_back();
	Registry.emplace(Key, Id);

	const std::size_t Needed = (static_cast<std::size_t>(Id) + 1)
		* static_cast<std::size_t>(Depth);
	while (EventStates.size() < Needed)
	{
		EventStates.emplace_back(false);
	}
	return Id;
}

FNodeId FFrameGraph::FImpl::ResolveTarget(const FTaskKey& Target)
{
	const auto It = Registry.find(Target);
	if (It == Registry.end())
	{
		return InvalidNodeId;
	}
	return It->second;
}

void FFrameGraph::FImpl::WireNode(FTaskNode& Node, const FTask& Decl)
{
	const FNodeId MyId = Registry.at(Decl.Key);

	auto AddEdge = [&](const FDependency& Dep)
	{
		const FNodeId Target = ResolveTarget(Dep.Target);
		if (Target == InvalidNodeId)
		{
			return;
		}

		const FEvent Event = EventOf(Target, Nodes[Target].Key.Phase, Depth);

		if (std::find(Node.Dependencies.begin(), Node.Dependencies.end(), Event)
			!= Node.Dependencies.end())
		{
			return;
		}

		Node.Dependencies.push_back(Event);

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

void FFrameGraph::FImpl::ApplySubmit(std::vector<FTask> Tasks)
{
	std::vector<FNodeId> Ids;
	Ids.reserve(Tasks.size());
	for (const FTask& Task : Tasks)
	{
		const FNodeId Id = GetOrCreateNodeId(Task.Key);
		Ids.push_back(Id);

		FTaskNode& Node = Nodes[Id];
		Node.Key = Task.Key;
		Node.NameSnapshot.assign(Task.Key.Name);
		Node.Dependencies.clear();
		Node.Successors.clear();
		Node.UnmetCount = 0;
		Node.CompletionEvent = EventOf(Id, Task.Key.Phase, Depth);
		Node.Closure = Task.Closure;
		Node.bDispatched.store(false, std::memory_order_relaxed);

		EventStates[Node.CompletionEvent].store(false, std::memory_order_release);
	}

	for (std::size_t I = 0; I < Tasks.size(); ++I)
	{
		WireNode(Nodes[Ids[I]], Tasks[I]);
	}

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

			if (Other.bDispatched.load(std::memory_order_acquire))
			{
				continue;
			}

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

	for (FNodeId Id : Ids)
	{
		if (!Nodes[Id].bDispatched.load(std::memory_order_relaxed)
			&& Nodes[Id].UnmetCount == 0)
		{
			Dispatch(Id);
		}
	}
}

void FFrameGraph::FImpl::Dispatch(FNodeId Id)
{
	FTaskNode& Node = Nodes[Id];
	Node.bDispatched.store(true, std::memory_order_release);

	SlotInFlight[SlotIndexOf(Node.Key.Phase)].fetch_add(1, std::memory_order_acq_rel);

	std::string Identity = NodeIdentity(Node);
	if (OwnerName != nullptr && OwnerName[0] != 0)
	{
		Identity = std::string(OwnerName) + ": " + Identity;
	}
	Pool.Submit(Lane, [this, Id, Identity = std::move(Identity)]()
	{
		const FScopedGraphNode ScopedNode(Identity.c_str());

		const bool bTrace = StageTraceEnabled();
		if (bTrace)
		{
			TraceStage("enter", Nodes[Id].Key);
		}

		try
		{
			Nodes[Id].Closure();
		}
		catch (const std::exception& E)
		{
			ReportNodeFailure(Id, E.what());
		}
		catch (...)
		{
			ReportNodeFailure(Id, "unknown exception");
		}

		if (bTrace)
		{
			TraceStage("exit ", Nodes[Id].Key);
		}

		Server.Submit([this, Id]() { OnNodeCompleted(Id); });
	});
}

void FFrameGraph::FImpl::OnNodeCompleted(FNodeId Id)
{
	FTaskNode& Node = Nodes[Id];

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

	{
		std::lock_guard<std::mutex> Lock(SlotMutex);
		SlotInFlight[SlotIndexOf(Node.Key.Phase)].fetch_sub(1, std::memory_order_acq_rel);
		AwaitingCompletion.fetch_sub(1, std::memory_order_acq_rel);
	}
	SlotCv.notify_all();

	TryReap(Id);
}

void FFrameGraph::FImpl::TryReap(FNodeId Id)
{
	FTaskNode& Node = Nodes[Id];

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


std::string FFrameGraph::FImpl::NodeIdentity(const FTaskNode& Node)
{
	const std::string_view Name = Node.NameSnapshot.empty()
		? std::string_view("<unknown>")
		: std::string_view(Node.NameSnapshot);

	return std::string(Name) + "::" + std::string(Node.Key.Stage) + "@" + std::to_string(Node.Key.Phase);
}

std::string FFrameGraph::FImpl::DescribeNode(const FTaskNode& Node) const
{
	return NodeIdentity(Node)
		+ (Node.bDispatched.load(std::memory_order_relaxed) ? " [running]" : " [queued]")
		+ (Node.UnmetCount == 0 ? "" : " unmet=" + std::to_string(Node.UnmetCount));
}

void FFrameGraph::FImpl::DumpOutstanding(const char* Why)
{
	std::string Report = std::string("[fgnode] ") + (Why == nullptr ? "" : Why) + " -- outstanding="
		+ std::to_string(AwaitingCompletion.load(std::memory_order_acquire))
		+ " live=" + std::to_string(GetLiveNodeCount()) + " of " + std::to_string(Nodes.size())
		+ " node(s) in this graph";

	std::size_t Listed = 0;
	const std::size_t Stride = Depth > 0 ? static_cast<std::size_t>(Depth) : 1;
	for (std::size_t I = 0; I < Nodes.size(); ++I)
	{
		FTaskNode& Node = Nodes[I];
		if (!Node.bDispatched.load(std::memory_order_relaxed)
			&& EventStates[Node.CompletionEvent].load(std::memory_order_acquire))
		{
			continue;
		}

		Report += "\n[fgnode]   " + DescribeNode(Node) + " waits for:";
		bool bAnyUnmet = false;
		for (const FEvent Event : Node.Dependencies)
		{
			if (EventStates[Event].load(std::memory_order_acquire))
			{
				continue;
			}
			const std::size_t TargetId = Event / Stride;
			Report += " " + (TargetId < Nodes.size()
				? DescribeNode(Nodes[TargetId])
				: std::string("<no node for event ") + std::to_string(Event) + ">");
			bAnyUnmet = true;
		}
		if (!bAnyUnmet)
		{
			Report += Node.UnmetCount == 0 ? " nothing (ready, its body has not run yet)" : " (?)";
		}
		Listed += 1;
		if (Listed >= 64)
		{
			Report += "\n[fgnode]   ... (truncated)";
			break;
		}
	}
	if (Listed == 0)
	{
		Report += "\n[fgnode]   (no node is outstanding -- the fence is not waiting on a node)";
	}

	ReportError(Report.c_str());
}

void FFrameGraph::FImpl::RequestOutstandingDump(const char* Why)
{
	if (!Server.IsRunning())
	{
		ReportError((std::string("[fgnode] ") + (Why == nullptr ? "" : Why)
			+ " -- the scheduler thread is gone, so the outstanding set cannot be read").c_str());
		return;
	}
	Server.Submit([this, Reason = std::string(Why == nullptr ? "" : Why)]()
	{
		DumpOutstanding(Reason.c_str());
	});
}

void FFrameGraph::FImpl::ReportNodeFailure(FNodeId Id, const char* What)
{
	if (!Server.IsRunning())
	{
		ReportError((std::string("node threw: ") + What
			+ " (the graph no longer runs: identity unavailable)").c_str());
		return;
	}

	Server.Submit([this, Id, Message = std::string(What)]()
	{
		ReportNodeFailureOnScheduler(Id, Message);
	});
}

void FFrameGraph::FImpl::ReportNodeFailureOnScheduler(FNodeId Id, std::string What)
{
	const std::string Node = Id < Nodes.size() ? DescribeNode(Nodes[Id]) : std::string("<gone>");
	ReportError((std::string("node threw: ") + What + " -- node " + Node
		+ (OwnerName == nullptr || OwnerName[0] == 0
			? std::string()
			: std::string(" (graph of '") + OwnerName + "')")).c_str());

	DumpOutstanding("in flight when the node above threw");
}


void FFrameGraph::FImpl::WaitPhasesIdle(const std::vector<FTask>& Tasks)
{
	std::vector<char> Wanted(static_cast<std::size_t>(Depth), 0);
	for (const FTask& Task : Tasks)
	{
		if (PhaseIsKnown(Task.Key.Phase))
		{
			Wanted[static_cast<std::size_t>(SlotIndexOf(Task.Key.Phase))] = 1;
		}
	}

	std::unique_lock<std::mutex> Lock(SlotMutex);
	for (std::size_t I = 0; I < Wanted.size(); ++I)
	{
		if (!Wanted[I])
		{
			continue;
		}
		while (SlotInFlight[I].load(std::memory_order_acquire) != 0)
		{
			if (!SlotCv.wait_for(Lock, std::chrono::seconds(StallDumpSeconds), [this, I]()
				{
					return SlotInFlight[I].load(std::memory_order_acquire) == 0;
				}))
			{
				Lock.unlock();
				RequestOutstandingDump((std::string("admission waiting for ring slot ")
					+ std::to_string(I) + " to drain for " + std::to_string(StallDumpSeconds)
					+ "s").c_str());
				Lock.lock();
			}
		}
	}
}

bool FFrameGraph::FImpl::IsPhaseIdle(std::int32_t Phase) const
{
	if (SlotInFlight.empty() || !PhaseIsKnown(Phase))
	{
		return true;
	}
	return SlotInFlight[SlotIndexOf(Phase)].load(std::memory_order_acquire) == 0;
}

void FFrameGraph::FImpl::Wait()
{
	std::unique_lock<std::mutex> Lock(SlotMutex);
	while (AwaitingCompletion.load(std::memory_order_acquire) != 0)
	{
		if (!SlotCv.wait_for(Lock, std::chrono::seconds(StallDumpSeconds), [this]()
			{
				return AwaitingCompletion.load(std::memory_order_acquire) == 0;
			}))
		{
			const std::uint32_t Outstanding = AwaitingCompletion.load(std::memory_order_acquire);
			Lock.unlock();
			RequestOutstandingDump((std::string("submission fence not satisfied for ")
				+ std::to_string(StallDumpSeconds) + "s (awaited="
				+ std::to_string(Outstanding) + ")").c_str());
			Lock.lock();
		}
	}
}

std::size_t FFrameGraph::FImpl::GetLiveNodeCount() const
{
	std::size_t Total = 0;
	for (const auto& Count : SlotInFlight)
	{
		Total += Count.load(std::memory_order_acquire);
	}
	return Total;
}


std::int32_t FFrameBridge::PhaseOf(std::uint64_t Frame) noexcept
{
	return PhaseOfFrame(Frame);
}

FFrameBridge::FResult FFrameBridge::Build(std::span<FFrameExtension* const> Frames,
	std::span<const std::string_view> Stages,
	std::uint64_t Frame,
	IDispatch& Dispatch)
{
	FResult Result;

	std::unordered_map<FTaskKey, std::size_t, FTaskKeyHash> Index;
	for (FFrameExtension* F : Frames)
	{
		if (F == nullptr)
		{
			continue;
		}

		for (const std::string_view Stage : Stages)
		{
			if (!Dispatch.Implements(*F, Stage))
			{
				continue;
			}

			FTask Task;
			Task.Key = FTaskKey{ F->GetName(), Stage, PhaseOf(Frame) };
			Task.Closure = Dispatch.MakeClosure(*F, Stage, Frame);

			Index.emplace(Task.Key, Result.Tasks.size());
			Result.Tasks.push_back(std::move(Task));
		}
	}

	auto Find = [&](std::string_view Name, std::string_view Stage, std::int32_t Offset) -> FTask*
	{
		const auto It = Index.find(FTaskKey{ Name, Stage, PhaseOfShiftedFrame(Frame, Offset) });
		return It == Index.end() ? nullptr : &Result.Tasks[It->second];
	};

	std::unordered_map<std::string_view, FFrameExtension*> ByName;
	for (FFrameExtension* F : Frames)
	{
		if (F != nullptr)
		{
			ByName.emplace(F->GetName(), F);
		}
	}

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

	auto InSequence = [&](std::string_view Stage)
	{
		return std::find(Stages.begin(), Stages.end(), Stage) != Stages.end();
	};

	auto IsOtherBatchDeclaration = [&](const FFrameExtension::FEdge& Edge) -> bool
	{
		return !InSequence(Edge.TargetStage);
	};

	std::size_t DeclaredEdges = 0;
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
				continue;
			}

			FTask* Mine = Find(F->GetName(), MyStage, 0);
			if (Mine == nullptr)
			{
				Result.Diagnostics.push_back({ "stage not implemented by this frame",
					F->GetName(), MyStage, {}, {}, 0, false });
				continue;
			}

			for (const FFrameExtension::FEdge& Edge : Edges)
			{
				if (Edge.FrameOffset > 0)
				{
					Result.Diagnostics.push_back({ "frame offset must be <= 0 (a wait points backwards)",
						F->GetName(), MyStage, Edge.TargetName, Edge.TargetStage, Edge.FrameOffset, false });
					continue;
				}

				if (IsOtherBatchDeclaration(Edge))
				{
					continue;
				}

				if (const char* Why = CheckTarget(Edge))
				{
					Result.Diagnostics.push_back({ Why, F->GetName(), MyStage, Edge.TargetName,
						Edge.TargetStage, Edge.FrameOffset, false });
				}

				if (!IsFrameInsideWindow(Edge.FrameOffset))
				{
					continue;
				}

				Mine->Dependencies.push_back(FDependency{
					FTaskKey{ Edge.TargetName, Edge.TargetStage,
						PhaseOfShiftedFrame(Frame, Edge.FrameOffset) } });
				++DeclaredEdges;
			}
		}
	}

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
				continue;
			}

			FTask* Mine = Find(F->GetName(), MyStage, 0);
			if (Mine == nullptr)
			{
				Result.Diagnostics.push_back({ "stage not implemented by this frame",
					F->GetName(), MyStage, {}, {}, 0, true });
				continue;
			}

			if (Edge.FrameOffset > 0)
			{
				Result.Diagnostics.push_back({ "frame offset must be <= 0 (a wait points backwards)",
					F->GetName(), MyStage, Edge.TargetName, Edge.TargetStage, Edge.FrameOffset, true });
				continue;
			}

			if (IsOtherBatchDeclaration(Edge))
			{
				continue;
			}

			if (const char* Why = CheckTarget(Edge))
			{
				Result.Diagnostics.push_back({ Why, F->GetName(), MyStage, Edge.TargetName,
					Edge.TargetStage, Edge.FrameOffset, true });
			}

			if (!IsFrameInsideWindow(Edge.FrameOffset))
			{
				continue;
			}

			FTask* Target = Find(Edge.TargetName, Edge.TargetStage, Edge.FrameOffset);
			if (Target == nullptr)
			{
				continue;
			}

			Mine->Blocks.push_back(FDependency{ Target->Key });
		}
	}

#if MAHO_DO_CHECK
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

		char        FirstMissing[256] = { 0 };
		const auto  NoteMissing = [&FirstMissing](const FFrameExtension& F, std::string_view Stage,
			const char* Which)
		{
			if (FirstMissing[0] == 0)
			{
				std::snprintf(FirstMissing, sizeof(FirstMissing), "%.*s@%.*s <- %s",
					static_cast<int>(F.GetName().size()), F.GetName().data(),
					static_cast<int>(Stage.size()), Stage.data(), Which);
			}
		};

		const bool bExpectSelfEdge = IsFrameInsideWindow(-1);

		for (FFrameExtension* F : Frames)
		{
			if (F == nullptr)
			{
				continue;
			}
			const FTask* PreviousEmitted = nullptr;
			for (const std::string_view Stage : Stages)
			{
				FTask* Node = Find(F->GetName(), Stage, 0);
				if (Node == nullptr)
				{
					continue;
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
		MAHO_CHECKF(Actual == DeclaredEdges,
			"FFrameBridge::Build: batch carries %zu dependency(ies) but %zu declaration(s) were "
			"emitted (%zu of them structural, frame %llu) -- a dependency was pushed by something "
			"other than a declaration, or an in-window declaration produced no push",
			Actual, DeclaredEdges, ExpectedStructural, static_cast<unsigned long long>(Frame));

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

				if (NodeDumpEnabled())
				{
					for (const FTask& Task : Result.Tasks)
					{
						std::fprintf(stderr, "[fgnodes] %.*s::%.*s@%d deps=%zu\n",
							static_cast<int>(Task.Key.Name.size()), Task.Key.Name.data(),
							static_cast<int>(Task.Key.Stage.size()), Task.Key.Stage.data(),
							Task.Key.Phase, Task.Dependencies.size());
					}
					std::fflush(stderr);
				}
			}
		}
	}
#endif

	return Result;
}

}
