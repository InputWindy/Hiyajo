#pragma once

// StageMetadata -- the GRAPH-LEVEL table of "which (frame, stage) touched WHICH RESOURCE, and HOW"
// (change add-resource-metadata-edges, step 2).
//
// ── WHY THIS EXISTS ─────────────────────────────────────────────────────────────────────────────
//
// Until now the only thing that said "these two stages touch the same thing" was a HAND-WRITTEN edge
// per pair, discovered by crashing. The missing model was never the edge -- it was the fact behind it:
// WHO touches WHAT, WHEN. This header carries that fact:
//
//   row := { frame name, stage name, implementing method, [ {resource name, access mode, ordering} ] }
//
// keyed by `(frame name, stage name)` -- the same vocabulary a node's identity uses (`GetName()` +
// `Stage::StaticName()`), which is what makes a row bindable to a node at all. Step 4 derives ordering
// edges FROM these rows; step 2 (this one) only records them and can dump them, so a human can read
// the table before anything starts moving on its own.
//
// ── A RESOURCE IS A TYPE, AND ITS NAME COMES FROM A TRAIT ───────────────────────────────────────
//
// `FLog`, `IRHI`, `GameWorld::FComponentPools` -- the resource IS the type (Core/ResourceName.h), so a
// row stores its STATIC NAME and never a pointer: the table describes a SCHEDULE, not a memory graph,
// and it must stay readable after the object it names is long gone.
//
// ── THE THREE DECLARATIONS IN ONE PLACE ────────────────────────────────────────────────────────
//
//   1. access MODE        (`EResourceAccess`)      -- create / read / write / destroy,
//   2. ordering SEMANTICS (`EResourceOrdering`)    -- what a mode pair implies for edge derivation
//                                                     (design D4; the trait, defaulted, placeheld),
//   3. capability         (`ICreate/IDestroy/IRead/IWrite`) -- which modes a resource ALLOWS, so a
//                                                     write to a read-only resource is a COMPILE error
//                                                     rather than a rule somebody has to remember.
//
// The vocabulary a declaration site uses is at the bottom: the SHORT names (`Create(R)`, `Read(R)`,
// `Write(R)`, `Destroy(R)`) live in Core/StageAccess.h -- deliberately a SEPARATE header, because they
// are common words (see the note there), while the `MAHO_`-prefixed spellings are always available.

#include <Core/Export.h>
#include <Core/ResourceName.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <type_traits>

namespace Maho
{

/** One access WINDOW a stage body opens on a resource (design D3). It says what the body does with the
 *  thing for the duration of the call -- NOT who owns it: the resource's lifetime still belongs to
 *  whoever created it, and `Create`/`Destroy` in a row mark the two ends of that window as the stage
 *  saw them, nothing more. */
enum class EResourceAccess : std::uint8_t
{
	Create,
	Read,
	Write,
	Destroy,
};

/** How accesses to ONE resource order each other (design D4). The trait below defaults every resource
 *  to `LifetimeOnly`; a resource opts into the other two where they are the truth about it:
 *
 *    LifetimeOnly -- only EXISTENCE orders: Create < any access < Destroy.
 *    Snapshot     -- a HAND-OFF channel (an immutable snapshot, deliberately scheduled apart on the
 *                    two sides): the same lifetime rules, and NO Write -> Read edge. `FPrimitiveRegistry`
 *                    (the render mirror) and the input ring are the cases: a write-then-read edge is
 *                    neither needed for correctness (nothing can tear) nor free (it would serialise the
 *                    K-deep pipeline into lock-step).
 *    Strict       -- two stages touch the SAME mutable thing with no other edge ordering them: only
 *                    here do Write -> Read and Write -> Write edges get derived.
 *
 *  THIS ROUND REGISTERS THE TRAIT ONLY (step 4 derives the edges): nothing reads it yet, and that is
 *  deliberate -- the trait's value has to be reviewable in the dump BEFORE it decides anything. */
enum class EResourceOrdering : std::uint8_t
{
	LifetimeOnly,
	Snapshot,
	Strict,
};

/** The resource's ordering semantics; `LifetimeOnly` unless the resource says otherwise. */
template <typename TResource>
struct TResourceOrdering
{
	static constexpr EResourceOrdering Value = EResourceOrdering::LifetimeOnly;
};

/** `MAHO_DECLARE_RESOURCE_ORDERING(X, Snapshot)` -- state a resource's ordering semantics where the
 *  rest of its identity is declared (the plugin that owns the type). Written INSIDE `namespace Maho`.
 *  It reads as one declaration (the expansion ends in `;`). */
#define MAHO_DECLARE_RESOURCE_ORDERING(X, Ordering)                       \
	template <>                                                           \
	struct TResourceOrdering<X>                                           \
	{                                                                     \
		static constexpr EResourceOrdering Value = EResourceOrdering::Ordering; \
	};

// ── capability: which modes a resource ALLOWS ───────────────────────────────────────────────────
//
// The markers are EMPTY types a resource opts into by deriving from them -- no member, no vtable, no
// layout requirement (`std::is_base_of` is the whole check). The rule for a resource that declares
// NOTHING is "unconstrained", and that default is load bearing: most resources are heavyweight types
// authored in another plugin (`FLog`, `IRHI`), and a metadata step must not force every one of them to
// grow a base class on the day the table appears. Declaring ONE capability switches the resource to
// "only what I declared": `struct FReadOnlyThing : IRead {}` then rejects a `Write(...)` row at
// compile time, which is design D3's "只读资源" story in one line.

/** The resource's lifetime can start at this stage. */
struct ICreate
{
};
/** The resource's lifetime can end at this stage. */
struct IDestroy
{
};
/** The resource may be read. */
struct IRead
{
};
/** The resource may be written. */
struct IWrite
{
};

namespace Detail
{
	/** Is `T` a complete type? A capability check on an INCOMPLETE type would be UB (`is_base_of`
	 *  requires complete types), and the table is deliberately allowed to name a resource by a forward
	 *  declaration -- so completeness is part of the question. */
	template <typename T>
	inline constexpr bool IsCompleteV = requires { sizeof(T); };

	template <typename T, typename TCapability, bool = IsCompleteV<T>>
	struct TDeclaresCapability : std::false_type
	{
	};

	/** `is_base_of` BEFORE a type that may be incomplete: the completeness answer SELECTS this
	 *  specialization instead of being `&&`-ed onto the trait.
	 *
	 *  WHY NOT `IsCompleteV<T> && std::is_base_of_v<TCapability, T>`: `&&` short-circuits VALUES, not
	 *  INSTANTIATIONS -- both operands of the initializer are still instantiated, so `is_base_of` is
	 *  asked about the incomplete type anyway and MSVC refuses outright (`C2139: undefined class cannot
	 *  be used as an argument to _Is_base_of`). Putting the completeness in the ARGUMENT LIST is what
	 *  defers it: the primary template never names `is_base_of`, and the specialization is reached only
	 *  when there IS a complete type to ask.
	 *
	 *  Not a corner case: the table's whole point is that a resource is a type AUTHORED ELSEWHERE
	 *  (`FLog`, `IRHI`, `Platform::FPlatform`), so most rows name a forward declaration. */
	template <typename T, typename TCapability>
	struct TDeclaresCapability<T, TCapability, true> : std::is_base_of<TCapability, T>
	{
	};

	template <typename T, typename TCapability>
	inline constexpr bool DeclaresCapabilityV = TDeclaresCapability<T, TCapability>::value;

	/** Did the resource declare ANY capability? False => "undeclared" => every mode is allowed. */
	template <typename T>
	inline constexpr bool DeclaresAnyCapabilityV =
		DeclaresCapabilityV<T, ICreate> || DeclaresCapabilityV<T, IDestroy>
		|| DeclaresCapabilityV<T, IRead> || DeclaresCapabilityV<T, IWrite>;
}

/** May `TAccess` be declared on `TResource`? See the note above: a resource that declares no
 *  capability at all answers yes to everything; one that declares any answers only for what it
 *  opted into. */
template <typename TResource, EResourceAccess TAccess>
[[nodiscard]] constexpr bool ResourceSupportsAccess() noexcept
{
	if (!Detail::DeclaresAnyCapabilityV<TResource>)
	{
		return true;
	}
	if constexpr (TAccess == EResourceAccess::Create)
	{
		return Detail::DeclaresCapabilityV<TResource, ICreate>;
	}
	else if constexpr (TAccess == EResourceAccess::Destroy)
	{
		return Detail::DeclaresCapabilityV<TResource, IDestroy>;
	}
	else if constexpr (TAccess == EResourceAccess::Read)
	{
		return Detail::DeclaresCapabilityV<TResource, IRead>;
	}
	else
	{
		return Detail::DeclaresCapabilityV<TResource, IWrite>;
	}
}

/** One declared access, as the table stores it: the resource's static name + the mode + the resource's
 *  ordering semantics (kept here so a dump is one pass over the table, with no second lookup and no
 *  RTTI to ask a type anything at run time). */
struct FResourceAccess
{
	std::string_view  Resource{};
	EResourceAccess   Access   = EResourceAccess::Read;
	EResourceOrdering Ordering = EResourceOrdering::LifetimeOnly;
};

/** What a declaration-tail access macro expands to: the value, plus the two COMPILE-TIME checks the
 *  row must pass -- the resource is registered (it has a name), and the mode is one it allows. Written
 *  as a function so the checks are instantiated exactly where the row is written, and so the row is a
 *  VALUE (usable in a table) rather than a rule somebody has to enforce by reading. */
template <typename TResource, EResourceAccess TAccess>
[[nodiscard]] constexpr FResourceAccess MakeResourceAccess() noexcept
{
	static_assert(!TResourceName<TResource>::value.empty(),
		"this type has no resource name: register it with MAHO_DECLARE_RESOURCE(...) in Core/ResourceName.h "
		"before it appears in a stage's access table");
	static_assert(ResourceSupportsAccess<TResource, TAccess>(),
		"this resource does not declare this access mode: it opted into a capability set "
		"(ICreate/IDestroy/IRead/IWrite) that does not contain this one");
	return FResourceAccess{ TResourceName<TResource>::value, TAccess, TResourceOrdering<TResource>::Value };
}

/** One row of the table (the copy `FindStageAccess` hands out). */
struct FStageAccessRow
{
	/** The frame-name half of the key. For an INTERFACE binding (`bInterfaceBinding`) this is the STAGE
	 *  interface's own name: such a row states how a stage is CALLED, and whoever mounts it implements
	 *  it -- the row cannot name one frame, because many do. */
	std::string_view Frame{};
	std::string_view Stage{};
	/** The implementing METHOD's name, as written at the declaration site (`Initialize`, `Tick`, ...). */
	std::string_view Method{};
	/** True when `Frame == Stage`, i.e. this row declares the STAGE's own dispatch (the interface
	 *  binding every frame that mounts the stage is called through), not one frame's implementation. */
	bool             bInterfaceBinding = false;
	/** The declared accesses; `AccessCount` of them at `Accesses` (points into the table's own
	 *  storage, valid for the process's lifetime). */
	const FResourceAccess* Accesses = nullptr;
	std::size_t            AccessCount = 0;
};

/** Register one `(frame, stage)` row. Called by the declaration macro's static registrant, which is
 *  why it takes the parts rather than a row: registration happens at static-init time, from every
 *  translation unit that includes the declaration (a header's declaration is expanded per TU) and
 *  from every DLL that carries one -- so the call must be IDEMPOTENT. Re-registering the SAME row is
 *  normal and silent; registering a DIFFERENT one under the same key is a conflict and is reported
 *  (the key is supposed to be unique by construction -- a duplicate `TStageBinding` is a compile
 *  error -- so a run-time conflict means two spellings of one identity). */
MAHO_API void RegisterStageAccess(std::string_view Frame, std::string_view Stage, std::string_view Method,
	std::initializer_list<FResourceAccess> Accesses);

/** The row for `(Frame, Stage)`, or nullptr. */
MAHO_API const FStageAccessRow* FindStageAccess(std::string_view Frame, std::string_view Stage) noexcept;

/** How many rows are registered. */
MAHO_API std::size_t StageAccessRowCount() noexcept;

/** Print every row to stderr, ONCE, when the environment variable `MAHO_GRAPH_META` is set (the
 *  `MAHO_GRAPH_*` family the graph's own dumps use -- same switch spelling, same stderr stream, same
 *  reason: an env var lets ONE binary produce both the baseline run and the diagnostic run). Called
 *  from the host's frame loop, i.e. after every plugin that registers a row has been loaded -- the
 *  names are string literals inside those modules, so the dump must happen while they are mapped.
 *  Idempotent: the second and later calls do nothing. */
MAHO_API void DumpStageAccess();

/** The mode/ordering as text (the dump's vocabulary; also what a diagnostic quotes). */
MAHO_API const char* ToString(EResourceAccess Access) noexcept;
/** See above. */
MAHO_API const char* ToString(EResourceOrdering Ordering) noexcept;

} // namespace Maho

// ── the access vocabulary (declaration tails) ───────────────────────────────────────────────────
//
//   MAHO_IMPLEMENT_STAGE(FGameWorld, IShutdown, Shutdown,
//       MAHO_ACCESS_DESTROY(GameWorld::FComponentPools),
//       MAHO_ACCESS_READ(FLog))
//
// Each expands to the `FResourceAccess` VALUE for that resource and mode, checking at compile time
// that the resource is registered and that it allows the mode. The short spellings (`Destroy(...)`,
// `Read(...)`) are the ones the change's declaration sites read best with; they live in
// Core/StageAccess.h because they are common words that collide with ordinary methods
// (`FEntityRegistry::Create()`, the input probes' `Read(...)`), so a translation unit has to ask for
// them on purpose. These prefixed forms are always safe and always correct.

#define MAHO_ACCESS_CREATE(R)  ::Maho::MakeResourceAccess<R, ::Maho::EResourceAccess::Create>()
#define MAHO_ACCESS_READ(R)    ::Maho::MakeResourceAccess<R, ::Maho::EResourceAccess::Read>()
#define MAHO_ACCESS_WRITE(R)   ::Maho::MakeResourceAccess<R, ::Maho::EResourceAccess::Write>()
#define MAHO_ACCESS_DESTROY(R) ::Maho::MakeResourceAccess<R, ::Maho::EResourceAccess::Destroy>()

// ── the access vocabulary (function bodies -- SCAN MARKERS) ─────────────────────────────────────
//
// A stage body that touches a resource says SO, IN THE BODY, with these four:
//
//   MAHO_READ(FLog)  L = GetLog();          // const FLog*  -- I only read it
//   MAHO_WRITE(FLog) L = GetLog();          // FLog*        -- I may modify it
//   Create/Foo(M);  MAHO_CREATE(Foo, P) ... // a non-const pointer: I bring it into existence
//
// WHY THEY EXIST: the declaration table above is HAND-WRITTEN this round, and the next increment
// (task 6.1) SCANS these markers to generate it. A marker is the only access form a scanner can see,
// so the vocabulary has to be in place and in use BEFORE the scanner is written -- that is all this
// round does with them (nothing is forced to convert yet).
//
// The READ/WRITE forms are TYPES (`const R*` / `R*`), which is what makes the read/write split the
// type system's job instead of a convention: a body that declares `MAHO_READ(R)` cannot call a
// non-const method on the resource, and no rule has to be remembered. The CREATE/DESTROY forms take
// the POINTER and hand it back unchanged (they mark the window, they do not take ownership): both
// refuse a `const` argument, because you cannot create or destroy what you may not modify.

namespace Maho
{
namespace Detail
{
	/** The type of a READ window on `R`; also where the read capability is checked. */
	template <typename TResource>
	struct TReadWindow
	{
		static_assert(ResourceSupportsAccess<TResource, EResourceAccess::Read>(),
			"this resource does not declare read capability");
		using Type = const TResource*;
	};

	/** The type of a WRITE window on `R`; also where the write capability is checked. */
	template <typename TResource>
	struct TWriteWindow
	{
		static_assert(ResourceSupportsAccess<TResource, EResourceAccess::Write>(),
			"this resource does not declare write capability");
		using Type = TResource*;
	};

	template <typename TResource>
	[[nodiscard]] constexpr TResource* BeginCreateWindow(TResource* In) noexcept
	{
		static_assert(std::is_const_v<TResource> == false, "MAHO_CREATE marks a CREATE window and cannot "
			"take a const pointer -- creating something is a modification of it");
		static_assert(ResourceSupportsAccess<TResource, EResourceAccess::Create>(),
			"this resource does not declare create capability");
		return In;
	}

	template <typename TResource>
	[[nodiscard]] constexpr TResource* BeginDestroyWindow(TResource* In) noexcept
	{
		static_assert(std::is_const_v<TResource> == false, "MAHO_DESTROY marks a DESTROY window and cannot "
			"take a const pointer -- destroying something is a modification of it");
		static_assert(ResourceSupportsAccess<TResource, EResourceAccess::Destroy>(),
			"this resource does not declare destroy capability");
		return In;
	}
}

} // namespace Maho

/** A read window on `R`: `MAHO_READ(FLog) L = GetLog();` */
#define MAHO_READ(R)  ::Maho::Detail::TReadWindow<R>::Type
/** A write window on `R`: `MAHO_WRITE(FLog) L = GetLog();` */
#define MAHO_WRITE(R) ::Maho::Detail::TWriteWindow<R>::Type
/** Mark that `p` (a NON-const pointer) is this stage's CREATE of the resource it points at. */
#define MAHO_CREATE(R, p)  (::Maho::Detail::BeginCreateWindow<R>(p))
/** Mark that `p` (a NON-const pointer) is this stage's DESTROY of the resource it points at. */
#define MAHO_DESTROY(R, p) (::Maho::Detail::BeginDestroyWindow<R>(p))
