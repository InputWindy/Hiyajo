#pragma once

#include <Core/Async/ThreadPool.h>
#include <Core/Topology.h>
#include <Core/TypeList.h>

#include <cstdint>
#include <memory>
#include <utility>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// Singleton base (CRTP Meyers singleton).
// ───────────────────────────────────────────────────────────────────────

template <typename TDerived>
class TSingleton
{
protected:
	TSingleton() = default;

public:
	virtual ~TSingleton() = default;

	TSingleton(const TSingleton&) = delete;
	TSingleton& operator=(const TSingleton&) = delete;

	static TDerived& Get()
	{
		static TDerived Instance;
		return Instance;
	}
};

// ───────────────────────────────────────────────────────────────────────
// Runtime extension interface: the non-template common base every extension
// (static TExtension or dynamically-loaded plugin) implements. Stage-driven.
// ───────────────────────────────────────────────────────────────────────

template <typename TStage>
class IExtension
{
public:
	virtual ~IExtension() = default;

	[[nodiscard]] virtual bool ExecuteStage(TStage Stage) = 0;
};

// ───────────────────────────────────────────────────────────────────────
// Parallel extension base: one page of a group of same-tier features driven
// by a stage enum (IExtension / IRenderFeature, and the singleton lifecycle
// above).
// ───────────────────────────────────────────────────────────────────────

template <typename TStage, typename TDerived>
class TExtension : public TSingleton<TDerived>, public IExtension<TStage>
{
protected:
	TExtension() = default;

public:
	virtual ~TExtension() = default;
};

// ───────────────────────────────────────────────────────────────────────
// Extension list carrier: static assembly via inheritance.
//
//   class FGameEngine final
//       : public FEngineBase
//       , public FExtensions<FRenderSystem, FScriptSystem>
//   {
//       void Tick() override { Execute<EEngineStage::Tick, FList>(); }
//   };
// ───────────────────────────────────────────────────────────────────────

template <typename... TExtensions>
struct FExtensions
{
	using FList = TTypeList<TExtensions...>;
};

// ───────────────────────────────────────────────────────────────────────
// ForEach schedulers (satisfy FForEachScheduler in TypeList.h).
// ───────────────────────────────────────────────────────────────────────

/** Serial: run every callable in order (no thread, stateless). */
struct FSerialTraversePolicy
{
	template <typename... FCallables>
	void Run(FCallables&&... Callables) const
	{
		(Callables(), ...);
	}
};

/** Parallel: run every callable concurrently on the pool (barrier at the end). */
struct FParallelTraversePolicy
{
	FThreadPool& Pool;

	template <typename... FCallables>
	void Run(FCallables&&... Callables) const
	{
		Pool.Run(std::forward<FCallables>(Callables)...);
	}
};

// ───────────────────────────────────────────────────────────────────────
// Topology policies: forward (deps first) vs reverse (dependents first).
// ───────────────────────────────────────────────────────────────────────

/** Forward topology: the level bands as-is (deps before dependents). */
struct FForwardTopology
{
	template <typename TLevels>
	using Apply = TLevels;
};

/** Reverse topology: reversed band sequence (dependents before deps). */
struct FReverseTopology
{
	template <typename TLevels>
	using Apply = Topo::TReverse_t<TLevels>;
};

template <typename TStage>
class TScheduler
{
protected:
	explicit TScheduler()
	{
	}
};

// ───────────────────────────────────────────────────────────────────────
// Parallel extension scheduler: drives a group of TExtension by
// dependency levels — within a level, extensions run in parallel; between
// levels, serially (barrier). Owns its thread pool.
//
// Project side assembles extensions statically via FExtensions:
//
//   class FGameEngine final
//       : public FEngineBase
//       , public FExtensions<FRenderSystem, FScriptSystem>
//   {
//       void Tick() override     { Execute<EEngineStage::Tick, FList>(); }
//       void Shutdown() override { Execute<EEngineStage::Shutdown, FList, FReverseTopology>(); }
//   };
// ───────────────────────────────────────────────────────────────────────

template <typename TStage>
class TParallelScheduler : public TScheduler<TStage>
{
protected:
	explicit TParallelScheduler()
		: pPool(std::make_unique<FThreadPool>())
	{
	}

public:
	/**
	 * Drive every extension for one stage value (level-parallel).
	 * Stage is a compile-time constant; TTopology picks forward / reverse.
	 */
	template <TStage Stage, typename TExtensions, typename TTopology = FForwardTopology>
	void Execute()
	{
		using FLevels = typename TTopology::template Apply<Topo::TLevels_t<TExtensions, Stage>>;
		// Outer: levels run serially (level 0 before level 1 ...).
		ForEach<FLevels>(FSerialTraversePolicy{}, [&](auto LevelTag) {
			using FLevel = typename decltype(LevelTag)::Type;
			// Inner: same-level extensions run in parallel.
			ForEach<FLevel>(FParallelTraversePolicy{*pPool}, [](auto Tag) {
				using T = typename decltype(Tag)::Type;
				T::Get().ExecuteStage(Stage);
			});
		});
	}

private:
	std::unique_ptr<FThreadPool> pPool;
};

} // namespace Maho
