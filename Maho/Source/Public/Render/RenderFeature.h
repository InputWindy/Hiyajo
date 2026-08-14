#pragma once

#include <Core/Misc/DependsPack.h>
#include <Core/Misc/Export.h>
#include <Core/Misc/TypeList.h>
#include <Render/RenderPipelineStage.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <typeindex>
#include <vector>

namespace Maho
{

class FRenderSystem;
class FRDGBuilder;
class FWorld;

// ── Compile-time Feature dependency slot ──────────────────────────────

template <ERenderPipelineStage StageKey, typename DependsList = TTypeList<>>
struct TFeatureDependsOn
{
	static constexpr ERenderPipelineStage Key = StageKey;
	using FDependsList = DependsList;
};

template <typename... TSlots>
struct TFeatureDependsPack
{
	using FDependsPack = TFeatureDependsPack;
	static constexpr std::size_t NumSlots = sizeof...(TSlots);

	template <typename TVisitor>
	static constexpr void ForEachSlot(TVisitor&& Visitor)
	{
		if constexpr (sizeof...(TSlots) > 0)
			(Visitor(TSlots::Key, static_cast<typename TSlots::FDependsList*>(nullptr)), ...);
		else
			(void)Visitor;
	}

	template <ERenderPipelineStage Stage>
	static constexpr bool ParticipatesIn()
	{
		if constexpr (sizeof...(TSlots) > 0)
			return ((TSlots::Key == Stage) || ...);
		else
			return false;
	}
};

template <typename T, typename = void>
struct TResolveFeatureDependsPack
{
	using Type = TFeatureDependsPack<>;
};

template <typename T>
struct TResolveFeatureDependsPack<T, std::void_t<typename T::FDependsPack>>
{
	using Type = typename T::FDependsPack;
};

// ── Game context slice (type-erased per-feature frame data) ────────────

/**
 * Base for a feature's per-frame game context slice.
 * Each feature defines its own slice (data + Gather from the ECS world);
 * FRenderSystem gathers all slices on the game thread and moves them
 * to the render thread as FGameFrameContext.
 */
class MAHO_API IGameContextSlice
{
public:
	virtual ~IGameContextSlice() = default;
};

/** One frame's assembled context: one slice per render feature. */
struct MAHO_API FGameFrameContext
{
	std::vector<std::unique_ptr<IGameContextSlice>> Slices;

	FGameFrameContext() = default;
	FGameFrameContext(FGameFrameContext&&) = default;
	FGameFrameContext& operator=(FGameFrameContext&&) = default;
	FGameFrameContext(const FGameFrameContext&) = delete;
	FGameFrameContext& operator=(const FGameFrameContext&) = delete;
};

/**
 * Per-frame render context (render-side working state, 3-slot ring on FRenderSystem).
 * Features use it to index their per-frame resources (FrameIndex % N) and to
 * access transient per-frame allocations. The FRDGBuilder stays per-stage.
 */
struct MAHO_API FFrameContext
{
	std::uint64_t FrameIndex = 0;
};

// ── IRenderFeature ────────────────────────────────────────────────────

class MAHO_API IRenderFeature
{
public:
	virtual ~IRenderFeature() = default;

	[[nodiscard]] virtual const char* GetName() const = 0;

	virtual bool OnRegister(FRenderSystem& RenderSystem) { (void)RenderSystem; return true; }
	virtual void OnUnregister(FRenderSystem& RenderSystem) { (void)RenderSystem; }

	/**
	 * Game thread: gather this feature's per-frame data from the ECS world
	 * into its own context slice. FRenderSystem calls this for every feature
	 * each frame; the returned slice is moved to the render thread.
	 */
	[[nodiscard]] virtual std::unique_ptr<IGameContextSlice> GatherContext(FWorld& World) = 0;

	/**
	 * Render thread: execute a stage. MySlice is the slice this feature
	 * gathered on the game thread (cast it back to your concrete type).
	 * FrameCtx is the per-frame ring slot; GraphBuilder is per-stage.
	 */
	virtual void ExecuteStage(ERenderPipelineStage Stage,
	                          const IGameContextSlice& MySlice,
	                          FFrameContext& FrameCtx,
	                          FRDGBuilder& GraphBuilder)
	{
		(void)Stage;
		(void)MySlice;
		(void)FrameCtx;
		(void)GraphBuilder;
	}

	[[nodiscard]] virtual bool ParticipatesInStage(ERenderPipelineStage Stage) const = 0;
	virtual void ForEachStageDep(ERenderPipelineStage Stage,
	                             const std::function<void(const std::type_index&)>& Visitor) const = 0;
};

// ── CRTP base with auto dispatch ─────────────────────────────────────

template <typename TDerived>
class TRenderFeatureBase : public IRenderFeature
{
public:
	explicit TRenderFeatureBase(const char* InName)
		: Name(InName ? InName : "RenderFeature")
	{
	}

	[[nodiscard]] const char* GetName() const override { return Name; }

	[[nodiscard]] bool ParticipatesInStage(ERenderPipelineStage Stage) const override
	{
		using Pack = typename TResolveFeatureDependsPack<TDerived>::Type;
		bool bFound = false;
		Pack::ForEachSlot([&](auto Key, auto*) {
			if (Key == Stage) bFound = true;
		});
		return bFound;
	}

	void ForEachStageDep(ERenderPipelineStage Stage,
	                     const std::function<void(const std::type_index&)>& Visitor) const override
	{
		using Pack = typename TResolveFeatureDependsPack<TDerived>::Type;
		Pack::ForEachSlot([&](auto Key, auto* DepsList) {
			if (Key == Stage)
			{
				TDispatchDepList(DepsList, Visitor);
			}
		});
	}

protected:
	const char* Name;

private:
	template <typename... TDepTypes>
	static void TDispatchDepList(TTypeList<TDepTypes...>*,
		const std::function<void(const std::type_index&)>& V)
	{
		if constexpr (sizeof...(TDepTypes) > 0)
		{
			(V(std::type_index(typeid(TDepTypes))), ...);
		}
		else
		{
			(void)V;
		}
	}
};

// ── Simple named feature ─────────────────────────────────────────────

class MAHO_API FRenderFeature : public TRenderFeatureBase<FRenderFeature>
{
public:
	explicit FRenderFeature(const char* InName) : TRenderFeatureBase<FRenderFeature>(InName) {}
};

// ── Context-carrying feature (reduces per-feature boilerplate) ─────────

/**
 * CRTP feature that owns a concrete context slice type.
 * TContext must derive from IGameContextSlice and expose Gather(FWorld&).
 * The derived feature only implements ExecuteStage(Stage, TContext&, GB).
 */
template <typename TDerived, typename TContext>
class TRenderFeatureWithContext : public TRenderFeatureBase<TDerived>
{
	static_assert(std::is_base_of_v<IGameContextSlice, TContext>,
	              "TContext must derive from IGameContextSlice");

public:
	using TRenderFeatureBase<TDerived>::TRenderFeatureBase;

	[[nodiscard]] std::unique_ptr<IGameContextSlice> GatherContext(FWorld& World) override
	{
		auto Slice = std::make_unique<TContext>();
		Slice->Gather(World);
		return Slice;
	}

	/** Render thread: cast the slice back and forward to the typed hook. */
	void ExecuteStage(ERenderPipelineStage Stage,
	                  const IGameContextSlice& MySlice,
	                  FFrameContext& FrameCtx,
	                  FRDGBuilder& GraphBuilder) override
	{
		const auto& Slice = static_cast<const TContext&>(MySlice);
		static_cast<TDerived*>(this)->ExecuteStage(Stage, Slice, FrameCtx, GraphBuilder);
	}
};

} // namespace Maho
