#pragma once


#include <Core/Export.h>
#include <Core/ThreadedServer.h>
#include <Core/ThreadPool.h>
#include <Core/TypeList.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace Maho
{

#ifndef MAHO_FRAMES_IN_FLIGHT
#	define MAHO_FRAMES_IN_FLIGHT 3
#endif

MAHO_API void SetFramesInFlightSource(std::int32_t (*InSource)()) noexcept;
[[nodiscard]] MAHO_API std::int32_t GetFramesInFlight() noexcept;

inline bool IsValidPhase(std::int32_t Phase) noexcept
{
	return Phase >= 0 && Phase < GetFramesInFlight();
}

[[nodiscard]] inline std::int32_t PhaseOfFrame(std::uint64_t Frame) noexcept
{
	return static_cast<std::int32_t>(Frame % static_cast<std::uint64_t>(GetFramesInFlight()));
}

[[nodiscard]] inline std::int32_t PhaseOfShiftedFrame(std::uint64_t Frame, std::int32_t Offset) noexcept
{
	const std::int32_t K = GetFramesInFlight();
	const std::int32_t P = PhaseOfFrame(Frame);
	return static_cast<std::int32_t>(
		((static_cast<std::int64_t>(P) + Offset) % K + K) % K);
}

[[nodiscard]] inline bool IsFrameInsideWindow(std::int32_t Offset) noexcept
{
	return Offset > -GetFramesInFlight();
}

[[nodiscard]] MAHO_API const char* GetCurrentGraphNode() noexcept;

struct FTaskKey
{
	std::string_view Name{};

	std::string_view Stage{};

	std::int32_t     Phase = 0;

	bool operator==(const FTaskKey& Other) const;
};

struct FTaskKeyHash
{
	std::size_t operator()(const FTaskKey& Key) const noexcept;
};

/** A dependency's TYPE - how the edge came to exist, not what it points at. The two DECLARED forms (a
 *  frame's own `WaitFor` / `BlockOn`) plus the two STRUCTURAL forms a frame declares about itself
 *  (`MAHO_FRAME_STAGE_CHAIN` / `MAHO_FRAME_SELF_EDGES`) are the whole set today; a derived edge (§4)
 *  adds to it. The dump prints it, and the structural check counts the last two. */
enum class EDependencyType : std::uint8_t
{
	Wait,         // WaitFor: the declaring stage waits for the target
	Block,        // BlockOn: the target must run after the declaring stage
	StageChain,   // the in-frame stage chain
	SelfEdge,     // the per-stage cross-frame self edge
	Lifetime,     // derived from a resource's EXISTENCE (create < use < destroy)
	Shared,       // derived from two stages touching one mutable resource (Strict only)
};

[[nodiscard]] MAHO_API const char* GetDependencyTypeName(EDependencyType Type) noexcept;

struct FDependency
{
	FTaskKey        Target{};
	EDependencyType Type = EDependencyType::Wait;
	/** Why the edge exists, when the producer had something to say (a derived edge names its resource
	 *  and the mode pair). Empty for a declared edge, whose reason is its type. */
	std::string     Reason{};
};

struct FTask
{
	FTaskKey                 Key{};
	std::vector<FDependency> Dependencies;
	std::vector<FDependency> Blocks;
	std::function<void()>    Closure;
};

/** One edge a DERIVER wants added at graph level: a pair of keys the graph already holds, plus where
 *  the edge came from. Core neither invents nor interprets the reason -- it carries the sentence the
 *  deriver wrote (a resource name and the modes that met on it, in the engine's case). */
struct FDerivedEdge
{
	FTaskKey        From{};
	FTaskKey        To{};
	EDependencyType Type = EDependencyType::Wait;
	std::string     Reason{};
};

/** The seam that lets semantics live where they are known: Core knows a round's NODES and nothing
 *  about resources, so the GRAPH hands this the keys it currently holds -- every node of the window,
 *  i.e. more than one batch -- and pushes back whatever edges come out. The aggregation (per resource,
 *  over a whole round) is the deriver's business: neither side of a pair knows the other inside one
 *  batch, and the edges that matter (a writer this round, a reader the next) live across them. */
struct IDerivedEdgeSource
{
	virtual ~IDerivedEdgeSource() = default;

	virtual void DeriveEdges(std::span<const FTaskKey> LiveKeys, std::uint64_t Frame,
		std::vector<FDerivedEdge>& Out) = 0;
};


class FFrameExtension
{
public:
	FFrameExtension();
	virtual ~FFrameExtension();

	[[nodiscard]] virtual std::string_view GetName() const = 0;

	[[nodiscard]] virtual std::string_view GetCollectorName() const noexcept;

	[[nodiscard]] virtual bool ImplementsStage(std::string_view Stage) const;

	[[nodiscard]] virtual void* GetStageInstance(std::string_view Stage) noexcept;

	[[nodiscard]] virtual void* QuerySubobject(const void* TargetTag) noexcept;

	[[nodiscard]] virtual void* GetCollectorInterface() noexcept;

	struct FEdge
	{
		std::string_view TargetName{};
		std::string_view TargetStage{};
		std::int32_t     FrameOffset = 0;
		bool             bReverse    = false;

		/** Which declaration produced this edge (see EDependencyType). The scope sugar below fills it
		 *  in; the two structural helpers in Engine/Frame.h pass StageChain / SelfEdge. */
		EDependencyType  Type        = EDependencyType::Wait;
	};


	void AddDependency(std::string_view MyStage, FEdge Edge);

	void AddDependent(std::string_view MyStage, FEdge Edge);

	[[nodiscard]] const std::unordered_map<std::string_view, std::vector<FEdge>>&
	GetDependencies() const;

	[[nodiscard]] const std::vector<std::pair<std::string_view, FEdge>>&
	GetDependents() const;


	template <typename TMyStage>
	struct TWaitScope
	{
		FFrameExtension& Self;
		explicit TWaitScope(FFrameExtension& InSelf) : Self(InSelf) {}

		template <typename TTargetFrame>
		struct TTarget
		{
			FFrameExtension&      Self;
			std::int32_t Offset = 0;
			explicit TTarget(FFrameExtension& InSelf) : Self(InSelf) {}

			template <typename TTargetStage, EDependencyType TEdgeType = EDependencyType::Wait>
			TTarget& ForStage(std::int32_t Offset = 0)
			{
				Self.AddDependency(TMyStage::StaticName(),
					FEdge{ TTargetFrame::StaticName(), TTargetStage::StaticName(), Offset, false, TEdgeType });
				return *this;
			}

			template <typename TTargetStage, EDependencyType TEdgeType = EDependencyType::Wait>
			TTarget& OnStage(std::int32_t Offset = 0)
			{
				return ForStage<TTargetStage, TEdgeType>(Offset);
			}
		};

		struct TNamedTarget
		{
			FFrameExtension&          Self;
			std::string_view TargetName{};
			std::int32_t     Offset = 0;
			TNamedTarget(FFrameExtension& InSelf, std::string_view InName) : Self(InSelf), TargetName(InName) {}

			template <typename TTargetStage, EDependencyType TEdgeType = EDependencyType::Wait>
			TNamedTarget& ForStage(std::int32_t Offset = 0)
			{
				Self.AddDependency(TMyStage::StaticName(),
					FEdge{ TargetName, TTargetStage::StaticName(), Offset, false, TEdgeType });
				return *this;
			}

			template <typename TTargetStage, EDependencyType TEdgeType = EDependencyType::Wait>
			TNamedTarget& OnStage(std::int32_t Offset = 0)
			{
				return ForStage<TTargetStage, TEdgeType>(Offset);
			}
		};

		template <typename TTargetFrame>
		TTarget<TTargetFrame> IsWaiting() { return TTarget<TTargetFrame>(Self); }

		TNamedTarget IsWaiting(std::string_view TargetName)
		{
			return TNamedTarget(Self, TargetName);
		}

		template <typename TTargetFrame>
		TTarget<TTargetFrame> WaitFor() { return TTarget<TTargetFrame>(Self); }

		TNamedTarget WaitFor(std::string_view TargetName)
		{
			return TNamedTarget(Self, TargetName);
		}
	};

	template <typename TMyStage>
	struct TBlockScope : TWaitScope<TMyStage>
	{
		FFrameExtension& Self;
		explicit TBlockScope(FFrameExtension& InSelf) : TWaitScope<TMyStage>(InSelf), Self(InSelf) {}

		template <typename TTargetFrame>
		struct TTarget
		{
			FFrameExtension&      Self;
			std::int32_t Offset = 0;
			explicit TTarget(FFrameExtension& InSelf) : Self(InSelf) {}

			template <typename TTargetStage>
			TTarget& OnStage(std::int32_t Offset = 0)
			{
				Self.AddDependent(TMyStage::StaticName(),
					FEdge{ TTargetFrame::StaticName(), TTargetStage::StaticName(), Offset, true,
						EDependencyType::Block });
				return *this;
			}
		};

		struct TNamedTarget
		{
			FFrameExtension&          Self;
			std::string_view TargetName{};
			std::int32_t     Offset = 0;
			TNamedTarget(FFrameExtension& InSelf, std::string_view InName) : Self(InSelf), TargetName(InName) {}

			template <typename TTargetStage>
			TNamedTarget& OnStage(std::int32_t Offset = 0)
			{
				Self.AddDependent(TMyStage::StaticName(),
					FEdge{ TargetName, TTargetStage::StaticName(), Offset, true, EDependencyType::Block });
				return *this;
			}
		};

		template <typename TTargetFrame>
		TTarget<TTargetFrame> IsBlocking() { return TTarget<TTargetFrame>(Self); }

		TNamedTarget IsBlocking(std::string_view TargetName)
		{
			return TNamedTarget(Self, TargetName);
		}

		template <typename TTargetFrame>
		TTarget<TTargetFrame> BlockOn() { return TTarget<TTargetFrame>(Self); }

		TNamedTarget BlockOn(std::string_view TargetName)
		{
			return TNamedTarget(Self, TargetName);
		}
	};

	template <typename TMyStage>
	TBlockScope<TMyStage> MyStage() { return TBlockScope<TMyStage>(*this); }

private:
	struct FImpl;
	std::unique_ptr<FImpl> Impl;
};


class FFrameBridge
{
public:
	struct IDispatch
	{
		virtual ~IDispatch() = default;

		[[nodiscard]] virtual bool Implements(const FFrameExtension& Frame, std::string_view Stage) const = 0;

		[[nodiscard]] virtual std::function<void()> MakeClosure(FFrameExtension& Frame,
			std::string_view Stage, std::uint64_t Round) = 0;
	};

	struct FDiagnostic
	{
		std::string_view Reason{};
		std::string_view Frame{};
		std::string_view Stage{};
		std::string_view Target{};
		std::string_view TargetStage{};
		std::int32_t     FrameOffset = 0;
		bool             bReverse = false;
	};

	struct FResult
	{
		std::vector<FTask>       Tasks;
		std::vector<FDiagnostic> Diagnostics;
	};

	[[nodiscard]] static FResult Build(std::span<FFrameExtension* const> Frames,
		std::span<const std::string_view> Stages,
		std::uint64_t Frame,
		IDispatch& Dispatch);

	[[nodiscard]] static std::int32_t PhaseOf(std::uint64_t Frame) noexcept;
};

template <typename... TStages>
[[nodiscard]] std::array<std::string_view, sizeof...(TStages)> StageIndicesOf(TTypeList<TStages...>)
{
	return { TStages::StaticName()... };
}

class FFrameGraph : public FThreadedServer
{
public:
	explicit FFrameGraph(FThreadPool& InPool, FThreadPool::FLane InLane = FThreadPool::DefaultLane);
	~FFrameGraph() override;

	FFrameGraph(const FFrameGraph&) = delete;
	FFrameGraph& operator=(const FFrameGraph&) = delete;


	[[nodiscard]] bool Submit(std::vector<FTask> Tasks, std::uint64_t Frame,
		std::string* OutReason = nullptr);

	void Wait();

	void SetOwnerName(const char* InOwnerName);

	/** Install the edge deriver. Called once, before the graph drives anything (the deriver is consulted
	 *  after every batch merges, against the nodes the graph then holds). The graph does NOT own it:
	 *  the deriver outlives the graph, because it belongs to the semantics layer, not the scheduler. */
	void SetDeriver(IDerivedEdgeSource* InDeriver);

protected:
	[[nodiscard]] const char* GetThreadName() const override;

private:
	using FThreadedServer::Submit;

	using FThreadedServer::Flush;

	struct FImpl;
	std::unique_ptr<FImpl> Impl;
};

}
