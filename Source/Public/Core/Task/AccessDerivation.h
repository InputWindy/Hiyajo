/// @file
/// @brief Turning declared MEMORY ACCESSES into the predecessor keys a batch is submitted with.
///
/// Two halves, and only the second is machinery:
///
///   1. a DECLARATION says which memory a piece of work touches. It is TYPE-LEVEL on purpose -- the work's
///      regions are concrete C++ types in a type list (`TDecl::List`), so the compiler resolves every name
///      (a region is a type, not a string: a typo is a compile error, and two same-named types in different
///      namespaces are two regions, never one);
///   2. a LEDGER derives, from those declarations, which earlier keys a new item must wait for.
///
/// ## A region is a TYPE, and its identity is that type
///
/// A region is "a piece of memory", named by its type (`MAHO_DECLARE_REGION`, which only provides the
/// readable name a dump prints -- the identity is the type itself, so nothing can be misspelled). It needs
/// NO base class, no members, no layout: a tag type is enough. A type that provides several regions is
/// split with a NESTED type; a piece of memory with no type of its own gets an empty tag type.
///
/// The ONE naming rule: a region type is `<Name>Region`, and callers name the MEMORY (`Name`). A property
/// generates that pair itself (`MAHO_PROPERTY(int, Position)` gives `Position` + `PositionRegion`); a bare
/// region is a hand-written `struct FMyBufferRegion {}` declared with `MAHO_DECLARE_REGION(FMyBufferRegion)`.
/// `MAHO_MEMORY_ACCESS_READ`/`MAHO_MEMORY_ACCESS_WRITE` append the suffix, so it never appears at a call site.
///
/// ## A declaration IS a type, and it is a type list
///
///     struct FMyBufferRegion {};  struct FMyOutRegion {};  struct FMyInRegion {};
///     MAHO_DECLARE_REGION(FMyBufferRegion);
///     MAHO_DECLARE_REGION(FMyOutRegion);
///     MAHO_DECLARE_REGION(FMyInRegion);
///
///     MAHO_DECLARE_ACCESSES(UpdateTransforms,                    // <- a plain function's declaration
///         MAHO_MEMORY_ACCESS_READ(FMyBuffer),
///         MAHO_MEMORY_ACCESS_WRITE(FMyOut));                            //    (the macro DEFINES the type)
///
///     MAHO_DECLARE_ACCESSES(Tick,                                // <- a caller: pulls the callee in
///         MAHO_ACCESS_INCLUDE(UpdateTransforms),
///         MAHO_MEMORY_ACCESS_READ(FMyIn));
///
/// The list is COMPOSED BY THE COMPILER (`TCatch`, `Core/TypeList.h`): including another declaration is a
/// type-list splice, so a diamond is free and a missing label is a compile error rather than a silently
/// empty set. A type that declared nothing has no `List` at all, which is what makes "declared before it can
/// be submitted" a compile-time requirement (`CHasDeclaredAccesses`).
///
/// The declaration being a TYPE (rather than a tag handed to a traits specialization) is what lets one be
/// written inside another type: a task declares one PER OVERLOAD in its own body (`Core/Task/Task.h`), and
/// an explicit specialization cannot be written there.
///
/// ## The unit of scheduling IS the function -- so one access lasts the whole call
///
/// The graph sees a closure and nothing inside it: a helper a body calls is invisible to it, and that is
/// deliberate (the NODE is the unit, not the statement). An access therefore lasts the whole call, and a
/// callee's accesses belong to its caller -- which is what `MAHO_ACCESS_INCLUDE` states. The union is the
/// declaration, not an approximation of a finer one.
///
/// Two consequences, both accepted and both explicit:
///
///   * the declaration must be COMPLETE: a callee whose accesses were never pulled in is a missing edge,
///     i.e. a race, and nothing at run time can notice. That is why a debug audit -- comparing what a body
///     actually touched against what it declared -- belongs next to this (a diagnostic build's job);
///   * the order is CONSERVATIVE: a caller holds its regions for its whole call, not only across the
///     statement that touched them. Precision would mean making the sub-work a NODE of its own (splitting
///     the body around the call), a design decision -- not something a declaration can express.
///
/// ## The derivation: one state machine per region, not a global barrier
///
/// The ledger remembers, PER REGION, only two things:
///
///     Writer        -- the most recent write to it (a key + the generation), possibly empty
///     ReadersSince  -- the reads that happened after that write
///
/// and answers a new access with the MINIMAL set that orders it:
///
///   | new access | derived predecessors | then |
///   |---|---|---|
///   | write | `Writer` (write -> write) and every `ReadersSince` (read -> write) | `Writer` becomes it, the readers are cleared |
///   | read  | `Writer` (write -> read) | it joins `ReadersSince` |
///   | read vs read | nothing | |
///
/// Keeping ONE writer is what makes this an edge SET and not a barrier: writers are themselves chained
/// (each waited for the previous one), so a new writer is transitively ordered against the older ones and
/// never has to name them.
///
/// ## Exemptions are for the write -> read pair
///
/// That chain argument is what lets one writer stand for all of them, so exempting a WRITE->WRITE or a
/// READ->WRITE pair breaks it: a later writer would no longer name the writer it skipped and would race it
/// silently. Exempting the WRITE->READ pair is safe, and it is exactly what a hand-off channel needs (the
/// caller synchronizes that pair with its own signal), so that is what `Exempt` is for -- and a pair that is
/// not of that shape is REPORTED rather than accepted.
///
/// ## Ageing, not forgetting -- and no clock of its own
///
/// This header has no `BeginGeneration` and no `Forget`. Time reaches it in the only two forms it needs:
///
///   * **K** (`FramesInFlight`), a policy given once at construction;
///   * **the frame an access belongs to**, an input carried by the `Record`/`Exempt` call.
///
/// Walking a region drops whatever is K frames old, which is CORRECT rather than lossy: the dispatcher waits
/// for frame - K before it opens a frame, so nothing of that frame is still running, and a key the graph can
/// no longer resolve means "already completed" to it anyway (see `Core/Task/TaskGraph.h`). So the ledger's
/// size is bounded by (regions x K) with nobody having to report "this key is done" -- and there is no second
/// clock that could disagree with the dispatcher's.
///
/// ## One thread drives it
///
/// A ledger is not synchronized: the DERIVING thread is the one that submits, and it is the thread that
/// owns this state (the graph is submitted to from a driver thread; see the hardening list in
/// `Core/Task/TaskGraph.h` for why waiting is that thread's job too).
///
/// ## The metadata store is for HUMANS here
///
/// A declaration also files `(item, region name) -> r/w` rows into `Core/MetaData.h`, so the whole schedule
/// can be dumped with `MAHO_METADATA` and audited. That is a DIAGNOSTIC: identity lives in the types, so an
/// empty or missing store changes nothing about correctness.
#pragma once


#include <Core/Fatal.h>
#include <Core/MetaData.h>
#include <Core/Task/Access.h>
#include <Core/Task/TaskBuilder.h>
#include <Core/TypeList.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>


#include <map>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace Maho
{
namespace Core
{

// ── 1. regions ─────────────────────────────────────────────────────────────────────────────────────────

/// @brief A region's readable NAME -- the only thing a declaration has to state about a region besides the
///        type itself (which IS the identity, so nothing here can be misspelled into a wrong region).
/// @tparam TRegion The region type.
template <typename TRegion>
struct TRegionName
{
	/// @brief The empty default: a type that has not been declared a region (see `MAHO_DECLARE_REGION`).
	static constexpr std::string_view value{};
};

/// @brief Declares a type as a REGION (a piece of memory work items may be ordered against). At namespace
///        scope, once per region; write the type's qualified name, so the dump is unambiguous.
///
/// INTERNAL, and the exception rather than the rule: a piece of memory that some object OWNS is a
/// `MAHO_PROPERTY` (which declares its region itself, so nothing is named by hand). This is only for memory no
/// object owns -- a scratch buffer, an arena, a resource handle -- where the region is a bare ordering identity
/// with no storage and no handle. Application code does not call it.
/// @param TRegion The region type (a tag type is enough; a nested type splits a bigger thing).
#define MAHO_DECLARE_REGION(TRegion)                                                                     \
	template <>                                                                                          \
	struct ::Maho::Core::TRegionName<TRegion>                                                            \
	{                                                                                                    \
		inline static constexpr std::string_view value = #TRegion;                                       \
	}

/// @brief The marker a region type inherits when it has no hand-written name: a property's nested tag, e.g.
///
/// `MAHO_DECLARE_REGION` must be written at namespace scope, which a nested tag cannot have -- so a region that
/// lives inside a type says so by INHERITING this, and its name/token come from the compiler's spelling of the
/// type. Opt-in by inheritance keeps the "declared before it can be used" check intact: a type that neither
/// declares a name nor inherits this marker is still not a region.
struct FRegionTag
{
};

/// @brief Whether a type identifies its region by the compiler's spelling (a marked tag) rather than by a
///        `MAHO_DECLARE_REGION` name.
/// @tparam TRegion The region type.
template <typename TRegion>
inline constexpr bool UsesSpelledName = std::is_base_of_v<FRegionTag, TRegion>;

/// @brief Whether a region is PER-INSTANCE: two copies of it are two pieces of memory, so the ledger keys them
///        apart. It is what a property tag (`FRegionTag`) is.
///
/// A declared tag region is the opposite: a SINGLETON -- the region itself is the one piece of memory -- so it
/// keeps a single key (null instance).
///
/// This is what replaced the old "hand-off channel" marker: a double-buffered ring is two INSTANCES (two
/// buffers, or the K slots of a slice array), so the ledger derives no edge between them and nothing has to be
/// exempted by hand. The escape hatch that remains is `Exempt`: per pair, and a broken writer chain is reported.
/// @tparam TRegion The region type.
template <typename TRegion>
inline constexpr bool IsPerInstanceRegion = std::is_base_of_v<FRegionTag, TRegion>;

/// @brief A region's readable NAME (what a dump prints), for every kind of region.
///
/// A DECLARED region answers with the name its `MAHO_DECLARE_REGION` spelled; a marked tag (a property's)
/// answers with the compiler's spelling of its type, which is why it needs no hand-written name at all.
/// @tparam TRegion The region type.
/// @return The name (empty when the type is not a region).
template <typename TRegion>
[[nodiscard]] constexpr std::string_view RegionName() noexcept
{
	if constexpr (UsesSpelledName<TRegion>)
	{
		return ::Maho::TypeSpelling<TRegion>();
	}
	else
	{
		return TRegionName<TRegion>::value;
	}
}

/// @brief Whether a type is a region: declared with `MAHO_DECLARE_REGION` (a singleton), or a marked tag (what
///        a property is: per-instance).
template <typename TRegion>
concept CRegion = requires
{
	{ RegionName<TRegion>() } -> std::convertible_to<std::string_view>;
} && (RegionName<TRegion>().size() > 0);

/// @brief The token a region is identified by.
///
/// A DECLARED region uses the address of its name (stable for the process, and the same everywhere it is
/// expanded). A marked tag has no such name to point at, so it uses its own store row (`TypeToken`), keyed by
/// the compiler's spelling -- which every module writes identically, so the row (and therefore the token) is
/// one value per process, exactly like a declared region's.
/// @tparam TRegion The region type.
/// @return The token (never dereferenced, only compared).
template <CRegion TRegion>
[[nodiscard]] const void* RegionToken() noexcept
{
	if constexpr (UsesSpelledName<TRegion>)
	{
		return ::Maho::TypeToken<TRegion>();      // a store row keyed by the spelling: one value per process
	}
	else
	{
		return static_cast<const void*>(&TRegionName<TRegion>::value);
	}
}

#if MAHO_DO_CHECK

/// @brief One access the body actually made, as the handles saw it.
struct FObservedAccess
{
	/// @brief The region it touched.
	const void* Region = nullptr;

	/// @brief Its readable name (for the report).
	std::string_view Name{};

	/// @brief Which copy of it.
	const void* Instance = nullptr;

	/// @brief How.
	EMemoryAccess Mode = EMemoryAccess::Read;
};

/// @brief Checks that a node's DECLARATION is honest: what its body actually touched, against what its binding
///        declared. Diagnostics only -- it never feeds the derivation (that happens at submit) and it is not
///        built at all where `MAHO_DO_CHECK` is off.
///
/// A node wraps its body in one of these (see `TTaskBinding::Node`); the per-instance region handles inside the
/// body report into it through `ObserveAccess`. It then reports BOTH directions, because both are bugs:
///
///   * an access nobody declared      -> the edge the derivation needed is missing (a race);
///   * a declared write nobody made   -> a spurious edge, and a false write can hide a real read->write hazard;
///   * a declared read that WROTE     -> the declaration understated the access;
///   * a declared region never touched -> a WARNING (the branch may simply not have run).
///
/// Only accesses that go through a handle are visible: a body touching memory some other way is invisible here,
/// which is exactly why the "all shared memory goes through a handle" discipline is what makes this meaningful.
class FAccessObservation
{
public:
	/// @brief How many accesses/declarations one node may record before it stops counting (and says so).
	static constexpr std::size_t MaxEntries = 64;

	/// @brief Makes this the scope of the calling thread (a node body runs on one worker).
	FAccessObservation() noexcept;

	/// @brief Restores the previous scope (nesting is supported).
	~FAccessObservation();

	FAccessObservation(const FAccessObservation&) = delete;
	FAccessObservation& operator=(const FAccessObservation&) = delete;

	/// @brief Records one DECLARED access (called by the node wrapper, once per declared entry).
	/// @param Region The region token.
	/// @param Name Its readable name.
	/// @param Mode The declared mode.
	void Declare(const void* Region, std::string_view Name, EMemoryAccess Mode) noexcept;

	/// @brief Records one actual access (called by the handles).
	/// @param Region The region token.
	/// @param Name Its readable name.
	/// @param Instance Which copy (null for a singleton region).
	/// @param Mode What was done.
	void Observe(const void* Region, std::string_view Name, const void* Instance,
		EMemoryAccess Mode) noexcept;

	/// @brief Compares the two sets and reports what does not match (each (binding, region, direction) ONCE).
	/// @param Binding The binding's name, so the line says which node kind is dishonest.
	/// @return How many problems were found (0 = honest, which is the ordinary case).
	std::size_t Report(std::string_view Binding) const;

	/// @brief The scope of the calling thread, or null outside a node.
	/// @return The scope.
	[[nodiscard]] static FAccessObservation* Current() noexcept;

private:
	/// @brief True when `Mode` reads (the declaration covers a read; a write needs a declared write).
	[[nodiscard]] static bool IsWrite(EMemoryAccess Mode) noexcept { return Mode == EMemoryAccess::Write; }

	/// @brief The declared entries.
	FObservedAccess Declared[MaxEntries]{};

	/// @brief How many are used.
	std::size_t DeclaredCount = 0;

	/// @brief The observed entries.
	FObservedAccess Observed[MaxEntries]{};

	/// @brief How many are used.
	std::size_t ObservedCount = 0;

	/// @brief Whether an array ran out (reported once, so a busy node does not lie by silence).
	bool bOverflowed = false;

	/// @brief The scope this one replaced (nesting).
	FAccessObservation* Previous = nullptr;
};

/// @brief Reports one access through a handle to the active scope (a no-op outside a node, and in Shipping --
///        where the whole observation machinery does not exist, so a handle call compiles to the same code as
///        before).
/// @tparam TRegion The region declaration the handle is for.
/// @param Instance Which copy (null for a singleton region).
/// @param Mode What was done.
template <typename TRegion>
inline void ObserveAccess(const void* Instance, EMemoryAccess Mode) noexcept
{
	if (FAccessObservation* Scope = FAccessObservation::Current())
	{
		Scope->Observe(RegionToken<TRegion>(), RegionName<TRegion>(), Instance, Mode);
	}
}

#else

/// @brief Reports one access through a handle -- nothing at all where the observation is not built.
/// @tparam TRegion The region declaration the handle is for.
/// @param Instance Which copy (unused).
/// @param Mode What was done (unused).
template <typename TRegion>
inline void ObserveAccess(const void*, EMemoryAccess) noexcept
{
}

#endif

/// @brief Whether a region declares what it STORES (`TRegion::FValue`): a property tag does; a bare
///        `MAHO_DECLARE_REGION` tag does not (it is only an ordering identity).
///
/// This is the "can this metadata describe this memory?" question, answered at compile time: a
/// `TMemoryBinding<TRegion>` only exists for a region that answers it, and its constructor only accepts the very
/// type the region declared.
/// @tparam TRegion The region type.
template <typename TRegion>
concept CValuedRegion = requires { typename TRegion::FValue; };

/// @brief A property: one piece of memory whose storage is PRIVATE, so it can only be read or written through
///        `Get`/`Edit` -- and those two ARE the two modes (`Get` hands out `const`, `Edit` a mutable reference).
///
/// This is the C#-style property the access layer wanted: "no naked access" is not a convention here, it is the
/// only thing the type allows, so a body that touches this memory cannot silently skip the access vocabulary.
/// The names are the MODES on purpose: there is no "setter" that takes a value (that would be an assignment
/// dressed as a write), there is `Edit()` -- lending the reference out -- and `Get()`, lending it read-only.
///
/// A property belongs to its OWNER: `Instance()` is its address, which is what a per-owner ordering needs (two
/// entities' `Position` are two pieces of memory, and the ledger keys them apart -- see `FMemoryAccess`).
/// @tparam TRegion The region METADATA (a property tag): it carries the identity AND the value type, so the
///        property cannot disagree with it.
template <typename TRegion>
	requires (CRegion<TRegion> && CValuedRegion<TRegion>)
class TProperty
{
public:
	/// @brief The region this property is (its identity).
	using FRegion = TRegion;

	/// @brief The value it stores -- taken from the region, never spelled twice.
	using FValue = typename TRegion::FValue;

	/// @brief The MODE-SELECTED access -- the one place an access is made, and the shape a marker macro wraps.
	///
	/// `Access<Read>()` hands out `const`, `Access<Write>()` a mutable reference: the mode is a TEMPLATE
	/// ARGUMENT, so "this line reads" / "this line writes" is visible in the source (and to a scanner) instead
	/// of being inferred from a function name. The `Write` overload does not exist on a `const` object at all,
	/// so `const FLight& L; L.Position.Access<Write>()` is a compile error -- which is what makes the marker
	/// trustworthy.
	///
	/// `Get`/`Edit` are the everyday spellings; they delegate here, so the observation happens in ONE place.
	/// @tparam TMode What this access does (`Read` or `Write`).
	/// @return The stored value, `const` for a read and mutable for a write.
	template <EMemoryAccess TMode>
		requires (TMode == EMemoryAccess::Read)
	[[nodiscard]] const FValue& Access() const noexcept
	{
		ObserveAccess<TRegion>(this, TMode);
		return Value;
	}

	/// @brief `Access<Write>`: the mutable form (absent on a `const` object).
	/// @tparam TMode `Write`.
	/// @return The stored value, mutable.
	template <EMemoryAccess TMode>
		requires (TMode == EMemoryAccess::Write)
	[[nodiscard]] FValue& Access() noexcept
	{
		ObserveAccess<TRegion>(this, TMode);
		return Value;
	}

	/// @brief READS it: hands out a `const` reference, so a "read" access cannot write through it.
	/// @return The stored value, const.
	[[nodiscard]] const FValue& Get() const noexcept { return Access<EMemoryAccess::Read>(); }

	/// @brief WRITES it: hands out a mutable reference (this IS the "write" access).
	/// @return The stored value, mutable -- `Value.Edit() = x` and `Value.Edit() += d` both work.
	[[nodiscard]] FValue& Edit() noexcept { return Access<EMemoryAccess::Write>(); }

	/// @brief This property's instance identity (its own address; stable for the owner's lifetime).
	/// @return The instance token.
	[[nodiscard]] const void* Instance() const noexcept { return this; }

private:
	/// @brief The storage -- private, which is what makes a naked access impossible.
	FValue Value{};
};

/// @brief Declares one ACCESS-CONTROLLED field: the metadata for it (a nested region tag derived from the
///        field's NAME, carrying the value type) plus the property itself (storage private, `Get`/`Edit` only).
///
/// A property is the ONLY per-instance memory a declaration can name. A plain member is not monitored at all:
/// it carries no region, so it cannot be declared, cannot be observed, and does not take part in read/write
/// conflicts -- if a piece of state must be ordered against, make it a property.
///
///     struct FEntity
///     {
///         MAHO_PROPERTY(int, Position);
///         // -> struct PositionRegion : FRegionTag { using FValue = int; };
///         //    TProperty<PositionRegion> Position;
///     };
///
///     // The access declaration names the MEMORY; the macro appends `Region`:
///     MAHO_DECLARE_ACCESSES(FPhysicsTick_Execute, MAHO_MEMORY_ACCESS_WRITE(FEntity::Position));
///     // And another piece of such memory can be mounted with the same metadata at run time:
///     TMemoryBinding<FEntity::PositionRegion> View(SomeOtherInt);
///
/// Write it in a section whose visibility the access declarations can see: **`public`**, in practice. That is
/// not a style rule -- a generated declaration names the memory as `decltype(Owner::Member)::FRegion`, and
/// `decltype` is subject to access control even though it is unevaluated, so a private property cannot be
/// declared from outside the class.
/// @param Type The value type (it becomes the region's `FValue`).
/// @param Name The member's name (its region tag is `Name##Region`).
#define MAHO_PROPERTY(Type, Name)                                                                         \
	struct Name##Region : ::Maho::Core::FRegionTag                                                       \
	{                                                                                                    \
		/** @brief The value type this memory stores: what a binding of it may bind. */                   \
		using FValue = Type;                                                                              \
	};                                                                                                   \
	::Maho::Core::TProperty<Name##Region> Name

/// @brief Declares a GLOBAL property (namespace scope): one piece of memory with metadata of its own -- the
///        counterpart of `MAHO_PROPERTY` (which does the same for a member).
///
/// The storage is a `TProperty`, so there is no naked access: everything goes through `Get()`/`Edit()`, which is
/// also where an access is observed for the honesty check. The region tag inherits `FRegionTag`, so it is
/// per-instance -- and for a global there is exactly one instance, which is what a global is.
///
///     MAHO_GLOBAL_PROPERTY(GCounter, int);                       // -> GCounterRegion + `GCounter`
///     MAHO_DECLARE_ACCESSES(FTick_Execute, MAHO_MEMORY_ACCESS_WRITE(GCounter));   // the memory, not the tag
///
///     GCounter.Edit() += 1;                                        // the only way in
/// @param Name The property's name (its region tag is `Name##Region`).
/// @param Type What is stored (it becomes the region's `FValue`).
#define MAHO_GLOBAL_PROPERTY(Name, Type)                                                                 \
	struct Name##Region : ::Maho::Core::FRegionTag                                                       \
	{                                                                                                    \
		/** @brief The value type this memory stores: what a binding of it may bind. */                   \
		using FValue = Type;                                                                              \
	};                                                                                                   \
	inline ::Maho::Core::TProperty<Name##Region> Name

/// @brief A MEMORY BINDING: one piece of declared METADATA, mounted on ONE concrete piece of memory -- the
///        memory-side counterpart of `TTaskBinding` (a declaration bound to an instance).
///
/// INTERNAL: application code reaches memory through `MAHO_PROPERTY` (which owns its storage, so there is
/// nothing to mount). This is for the other case -- a region declared with `MAHO_DECLARE_REGION` describing
/// memory that lives somewhere else (a ring's slot, an arena, a resource handle).
///
/// It is the vtable idea, one level down: the metadata is declared once (a region type, carrying its identity and
/// the value type it stores), and at run time it is MOUNTED on a particular memory -- a ring's slot is literally
/// "this metadata, on `Slices[i]`". The ledger then keys that access as `(region, &Slices[i])`, which is what
/// makes two slots independent without any extra concept.
///
/// The check the user wants lives in the constructor: it accepts only `TRegion::FValue`, so metadata can never be
/// mounted on memory of another type. The mode comes from the method (`Get` is const, `Edit` is mutable).
/// @tparam TRegion The region METADATA (must declare what it stores).
template <typename TRegion>
	requires (CRegion<TRegion> && CValuedRegion<TRegion>)
class TMemoryBinding
{
public:
	/// @brief The value stored there, as the metadata declared it.
	using FValue = typename TRegion::FValue;

	/// @brief Mounts the metadata on one piece of memory.
	/// @param Storage The memory (it must outlive every node that uses this binding).
	explicit TMemoryBinding(FValue& Storage) noexcept
		: Value(Storage)
	{
	}

	/// @brief The MODE-SELECTED access on the mounted memory (see `TProperty::Access`).
	/// @tparam TMode What this access does (`Read` or `Write`).
	/// @return The memory, `const` for a read and mutable for a write.
	template <EMemoryAccess TMode>
		requires (TMode == EMemoryAccess::Read)
	[[nodiscard]] const FValue& Access() const noexcept
	{
		ObserveAccess<TRegion>(&Value, TMode);
		return Value;
	}

	/// @brief `Access<Write>`: the mutable form (absent on a `const` binding view).
	/// @tparam TMode `Write`.
	/// @return The memory, mutable.
	template <EMemoryAccess TMode>
		requires (TMode == EMemoryAccess::Write)
	[[nodiscard]] FValue& Access() noexcept
	{
		ObserveAccess<TRegion>(&Value, TMode);
		return Value;
	}

	/// @brief Reads it (a `const` reference: this access cannot write).
	/// @return The memory, const.
	[[nodiscard]] const FValue& Get() const noexcept { return Access<EMemoryAccess::Read>(); }

	/// @brief WRITES it (a mutable reference: this IS the write access).
	/// @return The memory.
	[[nodiscard]] FValue& Edit() noexcept { return Access<EMemoryAccess::Write>(); }

	/// @brief The instance token of this binding: the piece of memory itself.
	/// @return The token.
	[[nodiscard]] const void* Instance() const noexcept { return &Value; }

private:
	/// @brief The memory this binding is about (a reference: the binding is a view).
	FValue& Value;
};

// ── 2. access, as a TYPE ───────────────────────────────────────────────────────────────────────────────

/// @brief One access, as a type: this region, this mode. (A type, so it can live in a type list and be
///        checked by the compiler -- see the file comment.)
/// @tparam TRegion The region (must be declared).
/// @tparam TMode Read or write.
template <typename TRegion, EMemoryAccess TMode>
	requires CRegion<TRegion>
struct TAccess
{
	/// @brief The region type.
	using FRegion = TRegion;

	/// @brief The mode.
	static constexpr EMemoryAccess Mode = TMode;
};

/// @brief One READ access, as a type.
template <typename TRegion>
using TReadAccess = TAccess<TRegion, EMemoryAccess::Read>;

/// @brief One WRITE access, as a type.
template <typename TRegion>
using TWriteAccess = TAccess<TRegion, EMemoryAccess::Write>;

// ── 3. the declaration ─────────────────────────────────────────────────────────────────────────────────

/// @brief Whether a type IS a declaration: it composed an access list (see `MAHO_DECLARE_ACCESSES`). There is
///        no fallback, so a type that never declared its accesses is a compile error at the point of use
///        rather than a silently empty list.
///
/// A declaration is a type rather than a tag handed to a traits specialization, because a declaration must
/// also be expressible INSIDE another type: a task declares one per overload, in its own body (see
/// `Core/Task/Task.h`), and an explicit specialization cannot be written there.
/// @tparam TDecl The declaration type.
template <typename TDecl>
concept CHasDeclaredAccesses = requires { typename TDecl::List; };

/// @brief One access to declare, spelled as a one-element list -- because every fragment of a declaration is
///        a LIST, and lists compose by concatenation.
///
/// This is the TYPE-level form: it takes the region type itself. Callers normally write `MAHO_MEMORY_ACCESS_READ` /
/// `MAHO_MEMORY_ACCESS_WRITE` instead, which take the memory's NAME (`Owner::Position`, `Counter`) and append the
/// `Region` suffix -- a region is always `<Name>Region`, so the suffix is the macro's business, not the
/// caller's.
/// @param TRegion The region type.
/// @param TMode `EMemoryAccess::Read` or `Write`.
#define MAHO_MEMORY_ACCESS(TRegion, TMode) ::Maho::TTypeList<::Maho::Core::TAccess<TRegion, TMode>>

/// @brief One READ access, by the memory's NAME (the region is `Name` + `Region`).
/// @param Name The memory: a property (`Owner::Position`), a global property (`Counter`), or a declared
///        region's name (`Buffer`, whose type is `BufferRegion`).
#define MAHO_MEMORY_ACCESS_READ(Name) MAHO_MEMORY_ACCESS(Name##Region, ::Maho::Core::EMemoryAccess::Read)

/// @brief One WRITE access, by the memory's NAME (the region is `Name` + `Region`).
/// @param Name The memory (see `MAHO_MEMORY_ACCESS_READ` to the region rule).
#define MAHO_MEMORY_ACCESS_WRITE(Name) MAHO_MEMORY_ACCESS(Name##Region, ::Maho::Core::EMemoryAccess::Write)

/// @brief Marks a READ at the USE site: `MAHO_READ(Position)` is `Position.Access<Read>()`.
///
/// The two markers exist so "this line reads / writes" is SPELLED, not inferred from a function name: the
/// operand is an ordinary expression (a property, or a mounted `TMemoryBinding`), the mode is the template
/// argument, and the access is observed in exactly one place (`Access`). A `const` operand simply will not
/// accept `MAHO_WRITE` -- the marker cannot lie about an object it cannot write.
///
/// (They are also what a source scanner looks for: a marker plus its enclosing class names the region, so an
/// access declaration can be generated from the code that uses it -- see design D18.)
/// @param Memory The property / mounted memory being read.
#define MAHO_READ(Memory) ((Memory).Access<::Maho::Core::EMemoryAccess::Read>())

/// @brief Marks a WRITE at the USE site: `MAHO_WRITE(Position)` is `Position.Access<Write>()`.
/// @param Memory The property / mounted memory being written.
#define MAHO_WRITE(Memory) ((Memory).Access<::Maho::Core::EMemoryAccess::Write>())

/// @brief Pulls ANOTHER declaration's accesses into this one: a type-list splice, so it is transitive, free,
///        and a label that was never declared does not compile.
///
/// It takes a DECLARATION (`MAHO_DECLARE_ACCESSES`), and also a BINDING (`MAHO_DECLARE_TASK_BINDING`): a binding
/// exposes its `List`, so `MAHO_ACCESS_INCLUDE(FLightSwarm)` reads as "this node touches everything that node
/// touches" -- the ordinary case for an implementation that runs another one.
/// @param TDecl The declaration (or binding) to include.
#define MAHO_ACCESS_INCLUDE(TDecl) typename TDecl::List

/// @brief The empty declaration (the way to say "this work touches nothing").
#define MAHO_ACCESS_NONE ::Maho::TTypeList<>

/// @brief The metadata axis a declaration's rows are filed under.
inline constexpr std::string_view KTaskAccess = "TaskAccess";

/// @brief One filed row's SHAPE, as `Core/MetaData.h` keeps it: a region name and a mode (names, not tokens --
///        the store is for humans, see the file comment).
struct FDeclaredAccess
{
	/// @brief The region's name.
	std::string_view Name{};

	/// @brief The mode.
	EMemoryAccess Mode = EMemoryAccess::Read;
};

/// @brief The members a declaration consists of, WITHOUT the type that carries them -- so the same access
///        declaration can be written inside any type (a task's shape, see `Core/Task/Task.h`) as well as at
///        namespace scope (a plain function's, see `MAHO_DECLARE_ACCESSES`).
/// @param TDecl The declared name (`#TDecl` is what its metadata rows are keyed by, so it must be unique).
/// @param ... The fragments: `MAHO_ACCESS_*` / `MAHO_ACCESS_INCLUDE` / `MAHO_ACCESS_NONE`.
#define MAHO_ACCESS_MEMBERS(TDecl, ...) MAHO_ACCESS_MEMBERS_NAMED(#TDecl, __VA_ARGS__)

/// @brief The same members, with the row key given as an EXPRESSION rather than a token to stringize.
///
/// That is what lets a declaration be keyed by something a token cannot spell: a signature type (see
/// `MAHO_DECLARE_TASK_METHOD_OVERRIDE`, whose key is the signature) or a name the compiler derived. The key
/// is a `std::string` here rather than a literal, because composing one out of two literals is a run-time
/// build; every reader takes it as a `std::string_view` either way.
/// @param Name The row key (any `std::string_view`-convertible expression, evaluated once).
/// @param ... The fragments: `MAHO_ACCESS_*` / `MAHO_ACCESS_INCLUDE` / `MAHO_ACCESS_NONE`.
#define MAHO_ACCESS_MEMBERS_NAMED(Name, ...)                                                            \
	/** @brief The declaration's readable name (what its metadata rows are keyed by). */                  \
	inline static const std::string TagName = Name;                                                      \
                                                                                                        \
	/** @brief The accesses, composed by the compiler. */                                               \
	using List = typename ::Maho::TCatch<__VA_ARGS__>::Type;                                            \
                                                                                                        \
	/** @brief Files the diagnostic rows, once, when this type is instantiated. */                        \
	inline static const bool bFiled = ::Maho::Core::RegisterDeclaredAccesses(TagName, List{})

/// @brief Declares what a piece of work touches -- the declaration IS a type (its name is its identity, its
///        `List` is what the compiler composed, its rows are what a dump reads).
///
/// Invoke it in the DECLARATION'S OWN namespace (the macro defines the type, and a namespace member cannot be
/// defined from the outside). The list is composed by the COMPILER (`TCatch` over the fragments), and the
/// same macro files the diagnostic rows into the metadata store -- neither can be forgotten, because both
/// come from this macro.
/// @param TDecl The declaration's name (its identity, and the prefix its rows are keyed by).
/// @param ... The fragments: `MAHO_ACCESS_*` / `MAHO_ACCESS_INCLUDE` / `MAHO_ACCESS_NONE`.
#define MAHO_DECLARE_ACCESSES(TDecl, ...)                                                               \
	struct TDecl                                                                                        \
	{                                                                                                   \
		MAHO_ACCESS_MEMBERS(TDecl, __VA_ARGS__);                                                        \
	}

/// @brief Files one declaration's rows into the metadata store (called by `MAHO_DECLARE_ACCESSES`).
///
/// Two things per declaration, and the FIRST is not a diagnostic: its own row (no access suffix), whose
/// ADDRESS is the declaration's identity token -- process-unique, because the store is one object per process
/// (see `Core/MetaData.h`), which is what a key may be built from. An inline static member's address would
/// NOT do: every DLL carries its own copy, so one declaration would hand out two tokens.
/// @tparam TAccesses The declared access types.
/// @param Tag The work item's tag, as a readable string.
/// @return True (so it can initialize a `const bool` member).
template <typename... TAccesses>
bool RegisterDeclaredAccesses(std::string_view Tag, TTypeList<TAccesses...>)
{
	GetMetaData().Register(KTaskAccess, Tag, "");
	((GetMetaData().Register(KTaskAccess, std::string(Tag) + "|"
			+ std::string(RegionName<typename TAccesses::FRegion>()),
			TAccesses::Mode == EMemoryAccess::Write ? "w" : "r")), ...);
	return true;
}

/// @brief A declaration's IDENTITY TOKEN: the address of its own row in the store.
///
/// A token, not a name, and not a hash of one: it is ONE value per declaration for the whole process (the
/// store is one object per process), so a key can fold it directly -- which is why a key never has to hash a
/// string, and why the token is the same in every module (an inline static member's address is not: each DLL
/// carries its own copy of it).
///
/// Comparing tokens is enough for identity; nothing ever dereferences one.
/// @tparam TDecl The declaration (a task's shape, or a plain function's own).
/// @return The token (nullptr only if the declaration never registered, which its macro makes impossible).
template <typename TDecl>
	requires CHasDeclaredAccesses<TDecl>
[[nodiscard]] const void* DeclarationToken() noexcept
{
	static const void* const Cached = GetMetaData().Find(KTaskAccess, TDecl::TagName);
	return Cached;
}

/// @brief Visits every access one declaration made, in no particular order -- the DIAGNOSTIC view (the
///        store's rows), which is what a dump, an audit or a validity check reads.
/// @tparam TDecl The declaration (must have one).
/// @tparam TVisitor Called as `Visit(std::string_view RegionName, EMemoryAccess Mode)`.
/// @param Visit The visitor.
template <typename TDecl, typename TVisitor>
	requires CHasDeclaredAccesses<TDecl>
void ForEachDeclaredAccess(TVisitor&& Visit)
{
	const std::string Prefix = std::string(TDecl::TagName) + "|";
	for (const FMetaDataEntry& Row : GetMetaData().Snapshot(KTaskAccess))
	{
		if (Row.Key.size() <= Prefix.size() || Row.Key.compare(0, Prefix.size(), Prefix) != 0)
		{
			continue;
		}
		Visit(std::string_view(Row.Key).substr(Prefix.size()),
			Row.Value == "w" ? EMemoryAccess::Write : EMemoryAccess::Read);
	}
}

// ── 4. the derivation ──────────────────────────────────────────────────────────────────────────────────

/// @brief What one derivation produced: the earlier keys this item must wait for.
///
/// Nothing else: with reads and writes, every hazard has a direction, and a work item's SEMANTIC conflicts
/// (a double create, a use after destroy) belong to whoever owns those lifetimes.
struct FDerivationResult
{
	/// @brief The earlier keys this item must wait for (its derived predecessors).
	std::vector<FTaskKey> Predecessors{};
};

/// @brief Remembers who touched what memory -- the ordering FRONTIER of every region -- and derives an item's
///        predecessors from it.
///
/// ## It has no clock of its own, on purpose
///
/// The ledger needs exactly two pieces of time information, and it takes each one where it is needed, from the
/// party that owns it:
///
///   * **how old is too old** -- a POLICY, given once at construction (`FramesInFlight`, the same K the
///     dispatcher and the graph's phase ring use);
///   * **which frame an access belongs to** -- an INPUT, carried by the `Record`/`Exempt` call.
///
/// So there is no `BeginGeneration` and no `Forget`: nothing to advance, and nothing to retire by name.
/// Anything older than K frames is dropped as the region is walked, and that is CORRECT rather than lossy --
/// a key older than K frames is provably complete (the dispatcher waits for frame - K before opening a frame),
/// and a key the graph can no longer resolve means "already completed" to it (see `Core/Task/TaskGraph.h`).
///
/// (Forgetting the clock is a lesson, not a shortcut: a component should take the minimal time input at the
/// call that needs it, and never own a clock somebody else already owns -- two clocks for one notion is what
/// makes "keep them in step" a rule nobody can check.)
class FMemoryAccessLedger
{
public:
	/// @brief Builds a ledger that drops accessors older than `FramesInFlight` frames.
	/// @param FramesInFlight How many frames may overlap (at least 1; the dispatcher's K, from ONE source).
	explicit FMemoryAccessLedger(std::size_t FramesInFlight = 2);

	/// @brief Declares that `(Region, From, To)` is ORDAINED BY THE CALLER: no edge is derived for it.
	///
	/// The exemption is as old as the frame it was declared in: after K frames both keys are provably complete,
	/// so the exemption goes -- otherwise this table would grow with the frame count.
	/// @param Region The region token.
	/// @param From The earlier key.
	/// @param To The later key.
	/// @param Frame The frame these keys belong to (the ledger's only notion of time).
	void Exempt(const void* Region, FTaskKey From, FTaskKey To, std::uint64_t Frame, const void* Instance = nullptr);

	/// @brief Derives the predecessors of one piece of work, from what it DECLARED it touches.
	///
	/// Walking the declaration also AGES the regions it touches: accessors older than K frames are dropped here,
	/// which is what keeps the ledger bounded without anyone calling `Forget`.
	/// @tparam TDecl The declaration (must have one -- see `CHasDeclaredAccesses`).
	/// @param Key Its key (already computed by the caller -- this layer never invents a key).
	/// @param Frame The frame it belongs to.
	/// @return The earlier keys it must wait for.
	template <typename TDecl>
		requires CHasDeclaredAccesses<TDecl>
	[[nodiscard]] FDerivationResult Record(FTaskKey Key, std::uint64_t Frame, const void* Instance = nullptr)
	{
		using FDeclaredList = typename TDecl::List;   // the declaration, as the compiler composed it
		PruneExemptions(Frame);
		FDerivationResult Result;
		ApplyAll(Result, Key, Frame, Instance, FDeclaredList{});
		return Result;
	}

	/// @brief How many accessors are remembered (diagnostics: it must not grow with the frame count).
	/// @return The entry count.
	[[nodiscard]] std::size_t Count() const noexcept;

private:
	/// @brief One tracked accessor: the key that touched a region, and which frame it did it in.
	struct FAccessor
	{
		/// @brief The key that touched it (never invented here: the caller computed it).
		FTaskKey Key = INDEX_NONE;

		/// @brief The frame it happened in (the only time this ledger knows).
		std::uint64_t Frame = 0;
	};

	/// @brief What the ledger remembers about ONE region (see the file comment's state machine).
	struct FRegionState
	{
		/// @brief Whether `Writer` names anything (it is dropped once it is older than K frames).
		bool bHasWriter = false;

		/// @brief The most recent write to the region.
		FAccessor Writer{};

		/// @brief The reads that followed it (they survive the writer ageing out: a reader still has to be
		///        ordered against the next writer).
		std::vector<FAccessor> ReadersSince;
	};

	/// @brief One pair the caller says it synchronizes itself, and the frame it was declared in.
	struct FExemption
	{
		/// @brief The region token the pair is about.
		const void* Region = nullptr;

		/// @brief Which copy of that region the pair is about (null = a singleton region).
		const void* Instance = nullptr;

		/// @brief The earlier key of the pair.
		FTaskKey From = INDEX_NONE;

		/// @brief The later key of the pair.
		FTaskKey To = INDEX_NONE;

		/// @brief The frame the pair belongs to (it expires K frames later).
		std::uint64_t Frame = 0;
	};

	/// @brief Whether the caller exempted this exact pair.
	/// @param Region The region token.
	/// @param From The earlier key.
	/// @param To The later key.
	/// @return True when an exemption names exactly this pair.
	[[nodiscard]] bool IsExempt(const void* Region, const void* Instance, FTaskKey From, FTaskKey To) const;

	/// @brief Drops a region's accessors that are older than K frames (they are provably complete).
	/// @param State The region's state (modified in place).
	/// @param Frame The frame being recorded (the ledger's only notion of time).
	void PruneRegion(FRegionState& State, std::uint64_t Frame) const;

	/// @brief Drops the exemptions older than K frames (after that both of their keys are complete).
	/// @param Frame The frame being recorded.
	void PruneExemptions(std::uint64_t Frame);

	/// @brief Reports (once) that an exemption broke the writer chain -- a later writer would race the writer
	///        it no longer names.
	/// @param Region The region whose chain broke.
	void ReportChainBreak(const void* Region) const;

	/// @brief Runs the state machine for ONE access of `Key`: the pack expansion over a declaration's list
	///        calls this once per access, so an included declaration's accesses are ordered exactly like the
	///        item's own.
	/// @param OutResult The result being built (predecessors are appended).
	/// @param Key The item's key.
	/// @param Frame The frame it belongs to.
	/// @param Access The one access to apply.
	/// @param bHandover Whether the region is a hand-off channel (a READ then derives no writer edge).
	void ApplyAccess(FDerivationResult& OutResult, FTaskKey Key, std::uint64_t Frame,
		const FMemoryAccess& Access);

	/// @brief Expands a declaration's type list into one `ApplyAccess` per access (compile-time, no array).
	/// @tparam TAccesses The declared access types.
	/// @param OutResult The result being built.
	/// @param Key The item's key.
	/// @param Frame The frame it belongs to.
	template <typename... TAccesses>
	void ApplyAll(FDerivationResult& OutResult, FTaskKey Key, std::uint64_t Frame,
		const void* Instance, TTypeList<TAccesses...>)
	{
		(ApplyAccess(OutResult, Key, Frame,
			FMemoryAccess{ RegionToken<typename TAccesses::FRegion>(),
				// A per-instance region (a property tag) takes THIS node's instance; a declared tag region
				// is a singleton, so it stays one (null means "the region itself").
				IsPerInstanceRegion<typename TAccesses::FRegion> ? Instance : nullptr,
				TAccesses::Mode }), ...);
	}

	/// @brief How many frames may overlap: anything older is dropped (the same K the dispatcher uses).
	std::size_t FramesInFlight = 2;

	/// @brief The per-region state machine (a region the declaration never touched has no entry).
	std::map<std::pair<const void*, const void*>, FRegionState> Regions;

	/// @brief The pairs the caller says it synchronizes itself (each expires after K frames).
	std::vector<FExemption> Exemptions;

	/// @brief Whether the broken-chain report was already made (say it once, not per access).
	mutable bool bChainBreakReported = false;
};

// ── implementation ─────────────────────────────────────────────────────────────────────────────────────
//
// INLINE, and therefore in this header: the generated projects list their sources explicitly, so a new
// `.cpp` would cost a project regeneration for everyone who builds this layer. The declaration side is a
// thin layer over the process-wide store (`Core/MetaData.h`), which is itself a singleton.

inline FMemoryAccessLedger::FMemoryAccessLedger(std::size_t InFramesInFlight)
	: FramesInFlight(InFramesInFlight == 0 ? 1 : InFramesInFlight)
{
}

inline void FMemoryAccessLedger::PruneRegion(FRegionState& State, std::uint64_t Frame) const
{
	// An accessor is provably complete once it is K frames old: the dispatcher waits for frame - K before it
	// opens a frame, so nothing of that frame is still running. Keeping it would only cost time and memory.
	// (Everything from the CURRENT frame stays, which is what keeps same-frame order intact.)
	const auto TooOld = [this, Frame](const FAccessor& Accessor)
	{
		return Frame >= Accessor.Frame && (Frame - Accessor.Frame) >= FramesInFlight;
	};

	State.ReadersSince.erase(
		std::remove_if(State.ReadersSince.begin(), State.ReadersSince.end(), TooOld),
		State.ReadersSince.end());

	if (State.bHasWriter && TooOld(State.Writer))
	{
		// The writer aged out. The READERS that followed it do not necessarily: a recent reader still has to
		// be ordered against whatever writes the region next.
		State.bHasWriter = false;
	}
}

inline void FMemoryAccessLedger::PruneExemptions(std::uint64_t Frame)
{
	// An exemption names two keys of one frame; once that frame is K old, both are complete and the exemption
	// can never matter again -- dropping it is what keeps this table from growing with the frame count.
	Exemptions.erase(
		std::remove_if(Exemptions.begin(), Exemptions.end(),
			[this, Frame](const FExemption& Exemption)
			{
				return Frame >= Exemption.Frame && (Frame - Exemption.Frame) >= FramesInFlight;
			}),
		Exemptions.end());
}

inline void FMemoryAccessLedger::Exempt(const void* Region, FTaskKey From, FTaskKey To, std::uint64_t Frame, const void* Instance)
{
	Exemptions.push_back(FExemption{ Region, Instance, From, To, Frame });
}

inline bool FMemoryAccessLedger::IsExempt(const void* Region, const void* Instance, FTaskKey From, FTaskKey To) const
{
	return std::any_of(Exemptions.begin(), Exemptions.end(),
		[Region, Instance, From, To](const FExemption& Exemption)
		{
			return Exemption.Region == Region && Exemption.Instance == Instance
				&& Exemption.From == From && Exemption.To == To;
		});
}

inline void FMemoryAccessLedger::ReportChainBreak(const void* Region) const
{
	if (bChainBreakReported)
	{
		return;
	}
	bChainBreakReported = true;
	std::fprintf(stderr, "[access] ERROR: an exemption breaks the writer chain on region %p -- writes to it "
		"can now race each other; exempt only (writer, reader) pairs\n", Region);
	std::fflush(stderr);
}

inline void FMemoryAccessLedger::ApplyAccess(FDerivationResult& OutResult, FTaskKey Key, std::uint64_t Frame,
	const FMemoryAccess& Access)
{
	const auto AddPredecessor = [&OutResult, Key](FTaskKey Candidate)
	{
		// An item can never be its own predecessor: a node cannot wait for itself. That also makes a DIAMOND
		// in a declaration tree harmless (the same access reached twice would otherwise name the item's own
		// key on the second pass).
		if (Candidate == Key)
		{
			return;
		}
		if (std::find(OutResult.Predecessors.begin(), OutResult.Predecessors.end(), Candidate)
			== OutResult.Predecessors.end())
		{
			OutResult.Predecessors.push_back(Candidate);
		}
	};

	FRegionState& State = Regions[{ Access.Region, Access.Instance }];
	PruneRegion(State, Frame);

	const bool bWrites = Access.Mode == EMemoryAccess::Write;

	// write -> read, and write -> write. (Naming ONE writer is enough: writers are chained.)
	if (State.bHasWriter)
	{
		if (IsExempt(Access.Region, Access.Instance, State.Writer.Key, Key))
		{
			if (bWrites)
			{
				ReportChainBreak(Access.Region);   // a WAW exemption: the chain no longer holds
			}
		}
		else
		{
			AddPredecessor(State.Writer.Key);
		}
	}

	if (bWrites)
	{
		// read -> write: a write must wait for whoever was reading.
		for (const FAccessor& Reader : State.ReadersSince)
		{
			if (IsExempt(Access.Region, Access.Instance, Reader.Key, Key))
			{
				ReportChainBreak(Access.Region);   // a WAR exemption: same problem
				continue;
			}
			AddPredecessor(Reader.Key);
		}
		State.bHasWriter = true;
		State.Writer = FAccessor{ Key, Frame };
		State.ReadersSince.clear();
	}
	else
	{
		State.ReadersSince.push_back(FAccessor{ Key, Frame });
	}
}

inline std::size_t FMemoryAccessLedger::Count() const noexcept
{
	std::size_t Count = 0;
	for (const auto& Region : Regions)
	{
		Count += (Region.second.bHasWriter ? 1 : 0) + Region.second.ReadersSince.size();
	}
	return Count;
}

#if MAHO_DO_CHECK

namespace Detail
{
	/// @brief The observation scope of the calling thread (a node body runs on one worker).
	inline thread_local FAccessObservation* GObservingScope = nullptr;

	/// @brief The (binding, region, direction) lines already printed, so a dishonest node says each ONCE.
	inline std::unordered_set<std::string>& ReportedLines()
	{
		static std::unordered_set<std::string> Lines;
		return Lines;
	}

	/// @brief Guards `ReportedLines` (nodes run on several workers).
	inline std::mutex& ReportedMutex()
	{
		static std::mutex Mutex;
		return Mutex;
	}
}

inline FAccessObservation::FAccessObservation() noexcept
	: Previous(Detail::GObservingScope)
{
	Detail::GObservingScope = this;
}

inline FAccessObservation::~FAccessObservation()
{
	Detail::GObservingScope = Previous;
}

inline FAccessObservation* FAccessObservation::Current() noexcept
{
	return Detail::GObservingScope;
}

inline void FAccessObservation::Declare(const void* Region, std::string_view Name, EMemoryAccess Mode) noexcept
{
	if (DeclaredCount < MaxEntries)
	{
		Declared[DeclaredCount++] = FObservedAccess{ Region, Name, nullptr, Mode };
	}
	else
	{
		bOverflowed = true;
	}
}

inline void FAccessObservation::Observe(const void* Region, std::string_view Name, const void* Instance,
	EMemoryAccess Mode) noexcept
{
	if (ObservedCount < MaxEntries)
	{
		Observed[ObservedCount++] = FObservedAccess{ Region, Name, Instance, Mode };
	}
	else
	{
		bOverflowed = true;
	}
}

inline std::size_t FAccessObservation::Report(std::string_view Binding) const
{
	std::size_t Problems = 0;

	const auto Emit = [&](std::string_view What, const FObservedAccess& Entry)
	{
		++Problems;
		std::string Line = std::string(Binding) + ": " + std::string(What);
		if (!Entry.Name.empty())
		{
			Line += " ";
			Line += std::string(Entry.Name);
		}
		std::lock_guard Lock(Detail::ReportedMutex());
		if (Detail::ReportedLines().insert(Line).second)
		{
			ReportError(Line.c_str());
		}
	};

	const auto Find = [](const FObservedAccess* Entries, std::size_t Count, const void* Region)
	{
		for (std::size_t Index = 0; Index < Count; ++Index)
		{
			if (Entries[Index].Region == Region)
			{
				return &Entries[Index];
			}
		}
		return static_cast<const FObservedAccess*>(nullptr);
	};

	for (std::size_t Index = 0; Index < ObservedCount; ++Index)
	{
		const FObservedAccess& Seen = Observed[Index];
		const FObservedAccess* DeclaredEntry = Find(Declared, DeclaredCount, Seen.Region);
		if (DeclaredEntry == nullptr)
		{
			Emit("touched but did not declare", Seen);
		}
		else if (!IsWrite(DeclaredEntry->Mode) && IsWrite(Seen.Mode))
		{
			Emit("declared Read but WROTE", Seen);
		}
	}

	for (std::size_t Index = 0; Index < DeclaredCount; ++Index)
	{
		const FObservedAccess& DeclaredEntry = Declared[Index];
		const FObservedAccess* ObservedEntry = Find(Observed, ObservedCount, DeclaredEntry.Region);
		if (ObservedEntry == nullptr)
		{
			Emit("declared but never touched (the branch may not have run)", DeclaredEntry);
			continue;
		}
		if (IsWrite(DeclaredEntry.Mode) && !IsWrite(ObservedEntry->Mode))
		{
			Emit("declared Write but never wrote it", DeclaredEntry);
		}
	}

	if (bOverflowed)
	{
		Emit("overflowed its observation (it touched more than the scope can count)", FObservedAccess{});
	}

	return Problems;
}

#endif

}
}
