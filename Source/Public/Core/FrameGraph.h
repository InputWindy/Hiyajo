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

struct FDependency
{
	FTaskKey Target{};
};

struct FTask
{
	FTaskKey                 Key{};
	std::vector<FDependency> Dependencies;
	std::vector<FDependency> Blocks;
	std::function<void()>    Closure;
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

	struct FEdge
	{
		std::string_view TargetName{};
		std::string_view TargetStage{};
		std::int32_t     FrameOffset = 0;
		bool             bReverse    = false;
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

			template <typename TTargetStage>
			TTarget& ForStage(std::int32_t Offset = 0)
			{
				Self.AddDependency(TMyStage::StaticName(),
					FEdge{ TTargetFrame::StaticName(), TTargetStage::StaticName(), Offset });
				return *this;
			}

			template <typename TTargetStage>
			TTarget& OnStage(std::int32_t Offset = 0)
			{
				return ForStage<TTargetStage>(Offset);
			}
		};

		struct TNamedTarget
		{
			FFrameExtension&          Self;
			std::string_view TargetName{};
			std::int32_t     Offset = 0;
			TNamedTarget(FFrameExtension& InSelf, std::string_view InName) : Self(InSelf), TargetName(InName) {}

			template <typename TTargetStage>
			TNamedTarget& ForStage(std::int32_t Offset = 0)
			{
				Self.AddDependency(TMyStage::StaticName(),
					FEdge{ TargetName, TTargetStage::StaticName(), Offset });
				return *this;
			}

			template <typename TTargetStage>
			TNamedTarget& OnStage(std::int32_t Offset = 0)
			{
				return ForStage<TTargetStage>(Offset);
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
					FEdge{ TTargetFrame::StaticName(), TTargetStage::StaticName(), Offset, true });
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
					FEdge{ TargetName, TTargetStage::StaticName(), Offset, true });
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

protected:
	[[nodiscard]] const char* GetThreadName() const override;

private:
	using FThreadedServer::Submit;

	using FThreadedServer::Flush;

	struct FImpl;
	std::unique_ptr<FImpl> Impl;
};

}
