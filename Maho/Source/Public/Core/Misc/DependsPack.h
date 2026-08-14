#pragma once

#include <Core/Engine/EngineStage.h>
#include <Core/Misc/TypeList.h>

#include <cstddef>
#include <cstdint>
#include <typeindex>
#include <type_traits>

namespace Maho
{

/**
 * Strength of a TDependsOn slot.
 * Strong (default): missing peer → ReportFatal.
 * Weak: missing peer is treated as already finished (silent skip). Only Detach / PrepareExit / Shutdown.
 */
enum class EExtensionDepStrength : std::uint8_t
{
	Strong = 0,
	Weak,
};

/** One peer edge recorded from a TDependsOn slot (runtime graph). */
struct FExtensionStageDep
{
	std::type_index Type;
	EExtensionDepStrength Strength = EExtensionDepStrength::Strong;
};

/**
 * One depends slot: StageKey (EEngineStage value) → TTypeList of peers → strength.
 * Dep completes before Self.
 */
template <
	auto StageKey,
	typename TList,
	EExtensionDepStrength Strength = EExtensionDepStrength::Strong>
struct TDependsOn
{
	static constexpr auto Key = StageKey;
	using FList = TList;
	static constexpr EExtensionDepStrength DepStrength = Strength;

private:
	static constexpr bool bStageAllowsWeak =
		std::is_same_v<decltype(StageKey), EEngineStage>
		&& (StageKey == EEngineStage::Detach
			|| StageKey == EEngineStage::PrepareExit
			|| StageKey == EEngineStage::Shutdown);

public:
	static_assert(
		Strength != EExtensionDepStrength::Weak || bStageAllowsWeak,
		"EExtensionDepStrength::Weak is only allowed for Detach, PrepareExit, and Shutdown");
};

/**
 * Variadic depends pack — inherit beside IEngineExtension.
 * Example:
 *   class FFoo : public IEngineExtension, public TDependsPack<
 *     TDependsOn<EEngineStage::Tick, TTypeList<FBar>>,
 *     TDependsOn<EEngineStage::Shutdown, TTypeList<FBar>, EExtensionDepStrength::Weak>>
 */
template <typename... TSlots>
struct TDependsPack
{
	using FDependsPack = TDependsPack;
	static constexpr std::size_t NumSlots = sizeof...(TSlots);

	/**
	 * Visit every registered slot. Visitor(StageKey, static_cast<TList*>(nullptr), Strength).
	 */
	template <typename TVisitor>
	static constexpr void BuildGraph(TVisitor&& Visitor)
	{
		if constexpr (sizeof...(TSlots) > 0)
		{
			(InvokeSlot<TSlots>(Visitor), ...);
		}
		else
		{
			(void)Visitor;
		}
	}

	/** Keys only. */
	template <typename TVisitor>
	static constexpr void ForEachStage(TVisitor&& Visitor)
	{
		if constexpr (sizeof...(TSlots) > 0)
		{
			(Visitor(TSlots::Key), ...);
		}
		else
		{
			(void)Visitor;
		}
	}

private:
	template <typename TSlot, typename TVisitor>
	static constexpr void InvokeSlot(TVisitor& Visitor)
	{
		Visitor(
			TSlot::Key,
			static_cast<typename TSlot::FList*>(nullptr),
			TSlot::DepStrength);
	}
};

template <typename T, typename = void>
struct TResolveDependsPack
{
	using Type = TDependsPack<>;
};

template <typename T>
struct TResolveDependsPack<T, std::void_t<typename T::FDependsPack>>
{
	using Type = typename T::FDependsPack;
};

template <auto A, auto B>
inline constexpr bool TDependsKeyEqual_v =
	std::is_same_v<decltype(A), decltype(B)> && A == B;

template <typename TPack, auto StageKey>
struct TFindInDependsPack
{
	using Type = TTypeList<>;
};

template <auto StageKey>
struct TFindInDependsPack<TDependsPack<>, StageKey>
{
	using Type = TTypeList<>;
};

template <
	auto StageKey,
	auto SlotKey,
	typename TList,
	EExtensionDepStrength Strength,
	typename... TRest>
struct TFindInDependsPack<
	TDependsPack<TDependsOn<SlotKey, TList, Strength>, TRest...>,
	StageKey>
{
	using Type = std::conditional_t<
		TDependsKeyEqual_v<StageKey, SlotKey>,
		TList,
		typename TFindInDependsPack<TDependsPack<TRest...>, StageKey>::Type>;
};

/** Resolve slot list for concrete type T (empty if no pack / no matching key). */
template <typename T, auto StageKey>
struct TFindDependsList
{
	using Type = typename TFindInDependsPack<typename TResolveDependsPack<T>::Type, StageKey>::Type;
};

/** Edge Dep → Self when TTo's Tick depends slot contains Dep. */
template <typename TFrom, typename TTo>
struct THasFrameEdge
	: TContains<typename TFindDependsList<TTo, EEngineStage::Tick>::Type, TFrom>
{
};

template <typename TFrom, typename TTo>
inline constexpr bool THasFrameEdge_v = THasFrameEdge<TFrom, TTo>::value;

template <typename TNodes, typename TNode, typename TPath>
struct TDfsFrameCycle;

template <typename TNodes, typename TPath, typename TDeps>
struct TDfsFrameCycleList;

template <typename TNodes, typename TPath>
struct TDfsFrameCycleList<TNodes, TPath, TTypeList<>> : std::false_type
{
};

template <typename TNodes, typename TPath, typename THead, typename... TRest>
struct TDfsFrameCycleList<TNodes, TPath, TTypeList<THead, TRest...>>
	: std::bool_constant<
		  TDfsFrameCycle<TNodes, THead, TPath>::value
		  || TDfsFrameCycleList<TNodes, TPath, TTypeList<TRest...>>::value>
{
};

template <typename TNodes, typename TNode, typename TPath>
struct TDfsFrameCycle
{
private:
	static constexpr bool bInPath = TContains_v<TPath, TNode>;
	using FDeps = typename TFilterDepsInNodes<
		TNodes,
		typename TFindDependsList<TNode, EEngineStage::Tick>::Type>::Type;
	using FNewPath = typename TCons<TNode, TPath>::Type;

public:
	static constexpr bool value =
		bInPath || (!bInPath && TDfsFrameCycleList<TNodes, FNewPath, FDeps>::value);
};

template <typename TNodes>
struct THasFrameCycleFromAny;

template <>
struct THasFrameCycleFromAny<TTypeList<>> : std::false_type
{
};

template <typename THead, typename... TRest>
struct THasFrameCycleFromAny<TTypeList<THead, TRest...>>
	: std::bool_constant<
		  TDfsFrameCycle<TTypeList<THead, TRest...>, THead, TTypeList<>>::value
		  || THasFrameCycleFromAny<TTypeList<TRest...>>::value>
{
};

template <typename TNodes>
inline constexpr bool THasFrameCycle_v = THasFrameCycleFromAny<TNodes>::value;

template <typename TNodes>
inline constexpr bool TIsFrameAcyclic_v = !THasFrameCycle_v<TNodes>;

template <typename TNodes>
constexpr void TAssertFrameAcyclic()
{
	static_assert(
		TIsFrameAcyclic_v<TNodes>,
					"TDependsPack frame graph has a cycle (Dep -> Self via Tick slot)");
}

} // namespace Maho
