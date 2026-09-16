#pragma once

// Frame -- the ENGINE side of the frame system.
//
// Core's FFrameExtension (Core/FrameGraph.h) is the DECLARATION layer: a name plus the edges it declares.
// It deliberately knows nothing about a stage sequence (see the note above it). This file is the
// other half -- the stage machinery that sits on top of it:
//
//   MAHO_DECLARE_FRAME(FrameType)   identity + DLL factory for a concrete frame
//   Invoke<TStage, TContext>        the stage dispatch protocol: one full specialization per
//                                   (stage interface x context), declared by the context's owner
//                                   via MAHO_DECLARE_STAGE_DISPATCH
//   TFrameDispatch<TStages, TContext>
//                                   the FFrameBridge policy: which stages a frame implements,
//                                   and how to run one. THE ONLY place that knows a stage list.
//
// A concrete frame is therefore:
//
//   class FWorld : public FFrameExtension, public IPipeline<IInit, ITick, IShutdown>
//   {
//       MAHO_DECLARE_FRAME(FWorld);
//   public:
//       FWorld() { MyStage<IInit>().IsWaiting<FLog>().ForStage<IInit>(); }
//   };
//
// IPipeline<TStages...> is the ordered sequence (it already binds the stage interfaces, and it
// exposes them as TStages); FFrameExtension supplies identity + the edge declarations. Neither knows the
// other, which is why the pair is spelled as two bases rather than one.
//
// A frame that does not implement a stage contributes NO node for it (the bridge asks
// TFrameDispatch::Implements) -- there is no empty node, and therefore no no-op to schedule.

#include <Core/Assembly.h>
#include <Core/FrameGraph.h>
#include <Core/Interface.h>
#include <Core/TypeList.h>

#include <functional>
#include <string>
#include <string_view>
#include <typeindex>
#include <type_traits>

/**
 * Frame declaration sugar -- generates StaticName() + GetName() + CreateFrame() +
 * GetModulePath(). CreateFrame/GetModulePath are NOT engine-frame-specific: every
 * FFrameExtension-derived frame that can be dynamically loaded carries the factory + module path.
 * Usage:
 *
 *   class FWorld : public FFrameExtension, public IPipeline<IInit, ITick> { MAHO_DECLARE_FRAME(FWorld); };
 *
 * The name comes from stringifying the type name (#FrameType); dependency declarations use
 * the same type deduction, so it is self-consistent, and it is a STRING LITERAL -- which is
 * what makes FTaskKey::Name (a string_view) safe (invariant I3).
 */
#define MAHO_DECLARE_FRAME(FrameType)                        \
public:                                                      \
	static constexpr std::string_view StaticName()           \
	{                                                        \
		return #FrameType;                                    \
	}                                                        \
	std::string_view GetName() const override                \
	{                                                        \
		return StaticName();                                  \
	}                                                        \
	static Maho::FFrameExtension* CreateFrame()                       \
	{                                                        \
		return new FrameType();                                \
	}                                                        \
	static std::string GetModulePath()                       \
	{                                                        \
		return Maho::ApplyModuleExtension(#FrameType);         \
	}

namespace Maho
{

/**
 * Stage dispatch - a free function template specialized per (stage, context) pair. The bridge's
 * closure calls Invoke<TStage>(Frame, Context) at run time; each stage interface gets full
 * specializations per context type (e.g. Invoke<IInit, FEngineBase> in Engine.h,
 * Invoke<IRender, FRender> in Render.h). A frame that does not implement the interface silently
 * skips -- which is a legal state, not an error (the bridge never even emits the node).
 */
template <typename TStage, typename TContext>
void Invoke(FFrameExtension* Frame, TContext& Context);

/**
 * Stage dispatch specialization sugar - full-specializes Invoke<TStage, TContext> to
 * dynamic_cast the frame to CastType and call Method(Context). Each context type (FEngineBase,
 * FRender, ...) declares its own specializations for the stage interfaces it schedules.
 *
 *   MAHO_DECLARE_STAGE_DISPATCH(FEngineBase, IInit, IInit, Initialize)
 *   // => Invoke<IInit, FEngineBase>(Frame, Engine) -> cast IInit -> Initialize(Engine)
 *
 * The cast is a cross-cast (the stage interface is a base of the concrete frame, not of FFrameExtension),
 * which is exactly why FFrameExtension must be polymorphic.
 */
#define MAHO_DECLARE_STAGE_DISPATCH(ContextType, StageType, CastType, Method) \
template <>                                                                    \
inline void Invoke<StageType, ContextType>(FFrameExtension* Frame, ContextType& Context) \
{                                                                              \
	if (auto* S = dynamic_cast<CastType*>(Frame))                              \
	{                                                                          \
		S->Method(Context);                                                     \
	}                                                                          \
}

/** Context placeholder for stage sequences that need none. */
struct FEmptyContext
{
};

/**
 * The bridge policy for one stage sequence + one context: which stages a frame implements, and
 * how to run one. Deliberately the ONLY thing in the codebase that knows a stage list -- Core's
 * FFrameBridge takes it as an opaque, ordered span of type_indices.
 *
 * The dispatch object must OUTLIVE every batch it produced (its closures capture it, and the
 * frame's reference), which the host's lifetime rules already guarantee: it lives on the stack
 * of whatever drives the batch, which then waits for the batch to drain.
 */
template <typename TStages, typename TContext = FEmptyContext>
class TFrameDispatch : public FFrameBridge::IDispatch
{
public:
	using FStages = TStages;

	explicit TFrameDispatch(TContext& InContext)
		: Context(InContext)
	{
	}

	/** Does the frame implement this stage? A stage it does not implement gets NO node. */
	[[nodiscard]] bool Implements(const FFrameExtension& Frame, std::type_index Stage) const override
	{
		return ImplementsImpl(Frame, Stage, FStages{});
	}

	/** The stage's body: `Invoke<TStage, TContext>(Frame, Context)`, baked at compile time. */
	[[nodiscard]] std::function<void()> MakeClosure(FFrameExtension& Frame, std::type_index Stage) override
	{
		return MakeClosureImpl(Frame, Stage, FStages{});
	}

	/** The stage sequence in the runtime form FFrameBridge::Build consumes. */
	[[nodiscard]] static constexpr auto StageIndices() noexcept
	{
		return StageIndicesOf(FStages{});
	}

private:
	template <typename TCurrent, typename... TRest>
	bool ImplementsImpl(const FFrameExtension& Frame, std::type_index Stage,
		TTypeList<TCurrent, TRest...>) const
	{
		if (Stage == std::type_index(typeid(TCurrent)))
		{
			return dynamic_cast<const TCurrent*>(&Frame) != nullptr;
		}
		if constexpr (sizeof...(TRest) > 0)
		{
			return ImplementsImpl(Frame, Stage, TTypeList<TRest...>{});
		}
		return false;
	}

	template <typename... TRest>
	bool ImplementsImpl(const FFrameExtension&, std::type_index, TTypeList<>) const
	{
		return false;
	}

	template <typename TCurrent, typename... TRest>
	std::function<void()> MakeClosureImpl(FFrameExtension& Frame, std::type_index Stage,
		TTypeList<TCurrent, TRest...>)
	{
		if (Stage == std::type_index(typeid(TCurrent)))
		{
			return [this, &Frame]() { Invoke<TCurrent, TContext>(&Frame, Context); };
		}
		if constexpr (sizeof...(TRest) > 0)
		{
			return MakeClosureImpl(Frame, Stage, TTypeList<TRest...>{});
		}
		return {};
	}

	template <typename... TRest>
	std::function<void()> MakeClosureImpl(FFrameExtension&, std::type_index, TTypeList<>)
	{
		return {};
	}

	TContext& Context;
};

} // namespace Maho
