/// @file
/// @brief The reference BINDING -- one task declaration paired with one OWNER TYPE -- plus the FRAME pieces:
///        a pure RECORDER and the CLOCK that submits what it recorded.
///
/// The binding is a NAME, not an object: it holds no state, performs nothing, and is never constructed. What it
/// is, is the pair "(this task) on (this owner type)", and the three statics below are how a recorder reaches
/// what that pair implies. It is the closest thing here to a Vulkan pipeline + descriptor layout: which code
/// runs, which contexts it is handed, and how such a node is named.
///
/// The three steps, when a layer drives a node by hand:
///
///     using FBinding = TTaskBinding<FWorld, ITick>;                // this task, on this owner TYPE
///     using FShape    = ITick::FMethod<void(int&)>;                  // one of its shapes
///
///     const FTaskKey Key = FBinding::KeyOf<FShape>(Frame);              // (1) the key
///     const FDerivationResult Edges
///         = FBinding::Derive<FShape>(Ledger, Key);                      // (2) the edges, from the shape
///     Graph.Submit(FBinding::Node<FShape>(Key, World, Delta),
///         Edges.Predecessors);                                           // (3) the closure, submitted
///
/// (A recorder does the same three steps, but splits them: the stable half of the key and the closure at
/// record time, the frame and the edges at submit -- see `FTaskBuffer`.)
///
/// ## What the binding does NOT decide
///
///   * **what a node IS**. `KeyOf` is a REFERENCE policy (read it below): whether two owners of this type, or
///     two shapes, are one node or two is the CALLER's answer, and a caller with other rules computes its own
///     key -- the graph only ever sees the number. The one thing a policy must not do is hand the same key to
///     two nodes that are in flight together, because that collision is fatal by construction.
///   * **who owns the contexts**. `Node` captures the owner and the contexts BY REFERENCE: they must outlive
///     the graph's fence, exactly like any other closure handed to `FTaskGraph` (a context that is copied
///     would be the wrong default here -- a frame's context holds its own locks and buffers).
///   * **when a generation turns**. The ledger ages itself by the frame every `Record` carries, and
///     `FTaskSystem` is what opens a frame: nothing here owns a clock.
///   * **where a node is submitted**. There is no `Submit` here on purpose: the caller already holds its
///     graph, and one line of its own reads better than a policy this layer would have to invent.
#pragma once


#include <Core/Delegate.h>
#include <Core/Fatal.h>
#include <Core/Task/AccessDerivation.h>
#include <Core/Task/Task.h>
#include <Core/Task/TaskBuilder.h>
#include <Core/Task/TaskGraph.h>
#include <Core/Task/ThreadPool.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace Maho
{
namespace Core
{

/// @brief Whether `TShape` is one of `TTaskType`'s DECLARED shapes.
///
/// A shape's identity is a specialization of the task's own member template (`TTaskType::FMethod<Signature>`),
/// so this is an exact answer rather than a name comparison: a shape of ANOTHER task is a different type and
/// does not satisfy it, and so does the primary -- which is why a signature nobody declared is rejected at
/// compile time (`FMethod<void(char)>` is the primary, and the primary is not a shape).
/// @tparam TTaskType The task.
/// @tparam TShape The shape to test.
template <typename TTaskType, typename TShape>
concept CTaskShape = requires
{
	{ TShape::bShape } -> std::convertible_to<bool>;
	typename TShape::FSignature;
	requires std::is_same_v<TShape, typename TTaskType::template FMethod<typename TShape::FSignature>>;
};

/// @brief Whether a task can be run with exactly these contexts on this owner -- i.e. whether some shape
///        declared that call. The compile-time face of the interface: what `Run` accepts is what was declared.
///
/// The owner is only NAMED here, never used: an unsatisfied shape fails on the constraints alone (the body is
/// never instantiated), so the test is about the task's declared shapes, not about what the owner happens to
/// implement.
/// @tparam TTaskType The task.
/// @tparam TOwner The owner type.
/// @tparam TArgs The contexts of the call.
template <typename TTaskType, typename TOwner, typename... TArgs>
concept CTaskRunnable = requires(TOwner& Owner, TArgs&... Args)
{
	TTaskType::Run(Owner, Args...);
};

/// @brief The slot value that means "this recording was not made for a particular slot" (the default).
///
/// A recording that is rebuilt every frame captures *this frame's* contexts, so it is slot-agnostic and needs
/// no stamp. A recording that is CACHED per slot captures one slot's contexts and must say so
/// (`FTaskBuffer::RecordedFor`), which is what lets `FTaskSystem::Submit` catch "submitted one frame early".
inline constexpr std::uint64_t kAnySlot = ~static_cast<std::uint64_t>(0);

/// @brief A layer's OWN identity for one owner INSTANCE -- and the only thing that tells two instances of one
///        owner type apart in a key.
///
/// It is the caller's number, deliberately: an instance's ADDRESS would be stable within a run but means
/// nothing (it drifts with the layout, and a moved owner changes it), while a number the layer chose can be
/// derived from something it already has -- a frame's name (resolve it to a store row's address and use that:
/// process-unique and equal in every module, see `DeclarationToken`), a collector's slot index, or a counter.
///
/// The default is ZERO, i.e. "this owner type runs once here" -- the ordinary case, and the one that needs no
/// identity at all. A layer that runs two instances of one type in one graph must pass distinct ones for
/// them, or the two nodes get one key and the graph reports the collision (which is fatal by construction,
/// see the key contract) rather than quietly sharing a slot.
struct FInstanceId
{
	/// @brief The caller's number. Zero means "the only instance of this owner type".
	std::uint64_t Value = 0;
};

/// @brief The metadata axis a registered binding's row is filed under (diagnostics: what a dump lists).
inline constexpr std::string_view KTaskBindingRow = "TaskBinding";

/// @brief Files one binding row `(KTaskBindingRow, "Owner|Task|Signature", "")`. Called by the macro below;
///        the row is what a dump or an audit reads, never what a lookup goes through (the compiler is the table).
/// @param Row The row's key.
/// @return True (so it can initialize a namespace-scope `const bool`).
[[nodiscard]] inline bool RegisterTaskBindingRow(std::string_view Row)
{
	GetMetaData().Register(KTaskBindingRow, Row, "");
	return true;
}

/// @brief The BINDING a `(Owner, Method, Signature)` names -- i.e. "the `<Signature>` version of `<Method>`, on
///        `<Owner>`" -- as a TYPE. This is what a call site "looks up": the compiler resolves it, so a wrong
///        owner, a task that was never declared, or a signature that task never declared is a compile error.
///
/// The signature is written as the method's PARAMETER LIST, in parentheses, like every other signature in
/// this folder (a comma inside a macro argument would split it): `MAHO_TASK_BINDING(FOwner, Tick, (int&))`.
/// The function type is completed here (`void(int&)`), which is what the shape is keyed by.
/// @param Owner The owner type.
/// @param Method The method, whose task name is derived exactly as `MAHO_BEGIN_DECLARE_TASK` derives it
///        (`Tick` -> `ITick`).
/// @param Signature The shape's parameter list, in parentheses.
#define MAHO_TASK_BINDING(Owner, Method, Signature)                                                      \
	::Maho::Core::TTaskBinding<Owner, MAHO_TASK_NAME(Method),                                            \
		typename MAHO_TASK_NAME(Method)::template FMethod<void (MAHO_TASK_UNPAREN(Signature))>>

/// @brief Declares ONE node kind: "the `<Signature>` version of `<Method>`, on `<Owner>`, touches THIS" --
///        checked where it is written, filed as one metadata row, and giving the linker nothing to look up.
///
/// This is where access control is declared, because this is where the implementing function is known: a shape
/// alone (`void(int&)`) may be implemented by many owners that touch completely different memory.
///
/// It is also where the node kind gets its NAME, so nothing else has to be written: `Name` is the type a
/// recorder says (`Buffer.Add<FWorldTick>(Entity, Delta)`), and it is spelled here once rather than derived --
/// one owner may bind several shapes of one method, and only the author knows which is which.
///
///     MAHO_DECLARE_TASK_BINDING(FWorldTick, FWorld, Tick, (FEntity&, int&),
///         MAHO_MEMORY_ACCESS_WRITE(FEntity::Position),       // the memory; the macro appends `Region`
///         MAHO_MEMORY_ACCESS_READ(FEntity::Scale));
///     // -> using FWorldTick = TTaskBinding<FWorld, ITick, ITick::FMethod<void(FEntity&, int&)>, FWorldTickAccesses>;
///     //    + the nested FWorldTickAccesses type that carries the two accesses
///
/// Write it anywhere a namespace-scope declaration may go -- next to the implementation it describes, in the
/// OWNER's namespace, is the natural place: the access declaration is a nested TYPE carried as a template
/// argument, so nothing has to be specialized in `Maho::Core` (which would confine it to `Maho`/global).
///
/// The fragments cannot be split across several calls: the declaration IS one type list, and a list cannot be
/// grown by separate macro invocations.
/// @param Name The node type's name (what a recorder names; see `MAHO_TASK_BINDING` for the type-level form).
/// @param Owner The owner type: a namespace-scope type that must INHERIT the task (a task is an interface, and a
///        binding is an implementation of it).
/// @param Method The method (its task is derived: `Tick` -> `ITick`).
/// @param Signature The shape's parameter list, in parentheses.
/// @param ... Its access fragments (`MAHO_MEMORY_ACCESS_READ/WRITE`/`INCLUDE`/`NONE`).
#define MAHO_DECLARE_TASK_BINDING(Name, Owner, Method, Signature, ...)                                   \
	/** @brief What this implementation touches on this shape -- a TYPE, so the binding can carry it. */   \
	struct Name##Accesses                                                                                \
	{                                                                                                    \
		/** @brief The memory (composed by the compiler). */                                             \
		using List = ::Maho::TCatch<__VA_ARGS__>::Type;                                                  \
                                                                                                         \
		/** @brief Whether a declaration was written (the ledger refuses a binding without one). */       \
		static constexpr bool bDeclared = true;                                                          \
                                                                                                         \
		/** @brief The readable name (`owner|method|signature`): rows and dumps are keyed by it. */       \
		inline static const std::string_view TagName = #Owner "|" #Method "|" #Signature;                \
                                                                                                         \
		/** @brief Files the diagnostic rows, once, at load time. */                                     \
		inline static const bool bFiled = ::Maho::Core::RegisterDeclaredAccesses(TagName, List{});        \
	};                                                                                                   \
                                                                                                         \
	static_assert(std::is_base_of_v<MAHO_TASK_NAME(Method), Owner>,                                      \
		"MAHO_DECLARE_TASK_BINDING: the owner must IMPLEMENT the task's interface (inherit from it)");   \
                                                                                                         \
	/** @brief The node TYPE this declaration names (what a recorder records). */                        \
	using Name = ::Maho::Core::TTaskBinding<Owner, MAHO_TASK_NAME(Method),                               \
		typename MAHO_TASK_NAME(Method)::template FMethod<void (MAHO_TASK_UNPAREN(Signature))>,           \
		Name##Accesses>

/// @brief Folds one more value into a key (FNV-1a -- the same mixer a caller keying by a name would use).
/// @param Key The key being built (updated in place).
/// @param Value The value to fold in (its bytes, little-endian).
inline void MixKey(FTaskKey& Key, std::uint64_t Value) noexcept
{
	for (int Byte = 0; Byte < 8; ++Byte)
	{
		Key ^= (Value >> (Byte * 8)) & 0xFF;
		Key *= 0x100000001b3ull;
	}
}

/// @brief Folds the FRAME into a STABLE key -- the one operation that turns "which node in this recording"
///        into "which node in this frame".
///
/// It is the same mixer `MixKey` uses, and `TTaskBinding::KeyOf` is defined as exactly this composition, so a
/// recorded node and a freshly computed key agree bit for bit (a cross-frame edge is written by computing a
/// key, so that agreement is what makes the edge possible at all).
/// @param StableKey The stable half (see `TTaskBinding::StableKeyOf`).
/// @param Frame The caller's absolute frame number.
/// @return The full key.
[[nodiscard]] inline FTaskKey FrameKey(FTaskKey StableKey, std::uint64_t Frame) noexcept
{
	FTaskKey Key = StableKey;
	MixKey(Key, Frame);
	return Key;
}

/// @brief The ACCESS declaration of ONE binding: (owner, task, shape) -> the memory it touches.
///
/// The primary is deliberately UNDEFINED, so a binding nobody declared cannot be recorded -- the error appears
/// where the access is derived, not as a silently empty list.
///
/// Why the binding and not the shape: what memory a node touches belongs to the IMPLEMENTATION, and one shape
/// (say `void(int&)`) may be implemented by many owners that touch completely different memory. Keying on the
/// triple makes each implementation declare its own, and keeps a typo a compile error.
/// @brief The access declaration a binding type carries, when nobody declared one.
///
/// Deliberately inert: an empty list, and `bDeclared` false. A binding that never DERIVES (it only names a key
/// or builds a node) is still usable; anything that needs the memory accesses says so at the place that needs
/// it (`TTaskBinding::Derive`), which is where the diagnostic belongs.
struct FNoBindingAccesses
{
	/// @brief No accesses.
	using List = ::Maho::TTypeList<>;

	/// @brief Whether a declaration was written (no).
	static constexpr bool bDeclared = false;

	/// @brief What a dump prints for it.
	inline static const std::string_view TagName = "<undeclared binding>";
};

/// @brief The binding: ONE node kind -- a task, ONE of its shapes, and the OWNER TYPE that runs it.
///
/// Folding the shape in is what makes a binding a *node kind* rather than a filter: `Buffer.Add<FIntTick>`
/// names everything the recorder needs, and a shape that task never declared fails at the ALIAS, not at a call
/// site. The OWNER is a template parameter and not a value, because it is what makes the node KIND static:
/// `Run` names `TOwner`'s implementation at compile time, per owner type, with no lookup -- the vtable the
/// task's interface declares is never consulted to find WHICH node runs, only (at most) to reach the body of
/// the shape the owner already committed to by inheriting.
///
/// It is a NAME, not an object: no state, nothing performed, never constructed. Write it as an alias (or with
/// `MAHO_TASK_BINDING`), and let `MAHO_DECLARE_TASK_BINDING` supply its access declaration.
/// @tparam TOwner The owner type whose method the shape is called on.
/// @tparam TTaskType The task declaration.
/// @tparam TShape One of that task's declared shapes.
/// @tparam TAccesses The ACCESS DECLARATION this binding carries (what `MAHO_DECLARE_TASK_BINDING` defines next
///         to the implementation: `List`, `bDeclared`, `TagName`, `bFiled`). It is a template PARAMETER rather
///         than a specialization looked up from here, because that is what lets the declaration live in the
///         owner's own namespace -- an explicit specialization of a `Maho::Core` template may not (MSVC C2888).
template <typename TOwner, typename TTaskType, typename TShape, typename TAccesses = FNoBindingAccesses>
	requires (CTask<TTaskType> && CTaskShape<TTaskType, TShape> && std::is_base_of_v<TTaskType, TOwner>)
class TTaskBinding
{
public:
	/// @brief The task this binding runs.
	using FTask = TTaskType;

	/// @brief The owner type it runs it on.
	using FOwner = TOwner;

	/// @brief The shape it runs (a node kind's signature).
	using FShape = TShape;

	/// @brief The ACCESS DECLARATION it carries (`List` / `TagName` / `bDeclared`) -- what a dump reads.
	using FAccesses = TAccesses;

	/// @brief Its access LIST, exposed so a binding can be INCLUDED like a declaration:
	///        `MAHO_ACCESS_INCLUDE(FLightSwarm)` means "this node touches what that node touches".
	using List = typename TAccesses::List;

	/// @brief The declaration's readable name (see `FAccesses`).
	inline static const std::string_view TagName = TAccesses::TagName;

	/// @brief Whether the accesses were actually declared (see `FNoBindingAccesses`).
	static constexpr bool bDeclared = TAccesses::bDeclared;

	/// @brief Folds one more value into a key (see `MixKey`; kept here so a policy reads as one expression).
	/// @param Key The key being built (updated in place).
	/// @param Value The value to fold in.
	static void Mix(FTaskKey& Key, std::uint64_t Value) noexcept { MixKey(Key, Value); }

	/// @brief The TYPE token: the metadata row of this `(owner, task, shape)` -- what a template record names.
	///
	/// It is the first term of `StableKeyOf` too, so "which type" and "which key" cannot drift apart; and it is a
	/// process-unique row address, so a recording made in one module names the same type in another.
	/// @return The token (never dereferenced, only compared).
	[[nodiscard]] static const void* TypeToken() noexcept
	{
		return ::Maho::TypeToken<TTaskBinding<TOwner, TTaskType, TShape, TAccesses>>();
	}

	/// @brief The STABLE half of a node's key: everything that does NOT change from frame to frame.
	///
	/// This is what a RECORDING keeps. The frame is folded in at SUBMIT (see `KeyOf`), which is what makes a
	/// recording reusable across frames at all: the same recorded frame submitted in one frame and in the next
	/// names two different nodes with one recording.
	///
	/// Its first term is the BINDING's own token -- the row of `(owner, task, shape)` -- so the owner TYPE is
	/// inside it already (one task serves any number of owners, and two of them are two node types), and a
	/// binding nobody declared has no token at all: it cannot even be named.
	/// @param Id The instance identity (default: the only instance of this owner type).
	/// @param Sub A discriminator for a second node of the same shape, instance and frame.
	/// @return The stable half.
	[[nodiscard]] static FTaskKey StableKeyOf(FInstanceId Id = {}, std::uint64_t Sub = 0) noexcept
	{
		FTaskKey Key = 0xcbf29ce484222325ull;
		Mix(Key, reinterpret_cast<std::uintptr_t>(TypeToken()));
		Mix(Key, Id.Value);
		Mix(Key, Sub);
		return Key;
	}

	/// @brief The REFERENCE key policy: one key per (shape, owner TYPE, instance, frame, sub) -- the stable half
	///        above with the frame folded in.
	///
	/// Every term of it is a value the LAYER can recompute, and none of them is an object's address:
	///
	///   * the SHAPE's token, not its name (see `DeclarationToken`): it implies BOTH the task and the signature;
	///   * the owner TYPE's token (see `TypeToken`), because ONE task serves ANY number of owner types: two
	///     types running the same shape in one frame are two nodes, and without this term they would be handed
	///     the same key -- which the graph refuses as fatal. It is the RTTI-free stand-in for `typeid`;
	///   * the INSTANCE identity, which the caller supplies (see `FInstanceId`);
	///   * the FRAME, because the key contract calls for an absolute frame (never a slot).
	///
	/// A caller replacing this policy should keep the property all of them have: the value must be recomputable
	/// by whoever names the node later (a cross-frame edge is declared by naming a key).
	/// @param Frame The caller's absolute frame number.
	/// @param Id The instance identity (default: the only instance of this owner type).
	/// @param Sub A discriminator for a second node of the same shape, instance and frame.
	/// @return The key.
	[[nodiscard]] static FTaskKey KeyOf(std::uint64_t Frame, FInstanceId Id = {},
		std::uint64_t Sub = 0) noexcept
	{
		return FrameKey(StableKeyOf(Id, Sub), Frame);
	}

	/// @brief Derives this node kind's predecessors from ITS OWN declaration (the binding's, not the shape's:
	///        two owners implementing one signature may touch different memory).
	///
	/// The FRAME is what the ledger ages its state by (it has no clock of its own): anything K frames older is
	/// provably complete and is dropped as the region is walked.
	/// @param Ledger The ledger the caller owns and drives.
	/// @param Key The key being submitted (see `KeyOf`).
	/// @param Frame The frame that key belongs to.
	/// @return The earlier keys this node must wait for.
	[[nodiscard]] static FDerivationResult Derive(FMemoryAccessLedger& Ledger, FTaskKey Key,
		std::uint64_t Frame, const void* Instance)
	{
		static_assert(TAccesses::bDeclared,
			"MAHO_DECLARE_TASK_BINDING: this binding has no access declaration -- write it next to the "
			"implementation it describes (it is what the ledger derives the edges from)");
		return Ledger.Record<TAccesses>(Key, Frame, Instance);
	}

	/// @brief Builds the node: the closure the graph runs, under this key, on this owner, through this shape.
	///
	/// The closure captures the owner and the contexts BY REFERENCE (see the file comment), and it runs the
	/// declared shape -- so `Node` can only be instantiated for contexts that shape accepts.
	/// @tparam TArgs The contexts of this run (deduced; must match the shape exactly).
	/// @param Key The key this node is submitted under (see `KeyOf`).
	/// @param Owner The owner instance to run it on.
	/// @param Args The contexts of this run.
	/// @return The description the graph consumes.
	template <typename... TArgs>
		requires CTaskRunnable<TTaskType, TOwner, TArgs...>
	[[nodiscard]] static FTaskDesc Node(FTaskKey Key, TOwner& Owner, TArgs&... Args)
	{
		FTaskDesc Out;
		Out.Key = Key;
#if MAHO_DO_CHECK
		// The BINDING's row name (`owner|method|signature`): what a dump or a stall report calls this node.
		Out.DebugKey = TAccesses::TagName;
#endif
		// By reference, both: the contexts belong to the caller (see the file comment). A pack capture, so a
		// shape of any arity forwards to the same static call the task declares.
#if MAHO_DO_CHECK
		// The DECLARATION is honest? The body's handle accesses are observed here and compared with what the
		// binding declared -- both directions, once each. Diagnostics only: the derivation happened at submit.
		Out.Body = [&Owner, &Args...]()
		{
			::Maho::Core::FAccessObservation Scope;
			DeclareAll(Scope, typename TAccesses::List{});
			TTaskType::Run(Owner, Args...);
			Scope.Report(TAccesses::TagName);
		};
#else
		Out.Body = [&Owner, &Args...]() { TTaskType::Run(Owner, Args...); };
#endif
		return Out;
	}

private:
#if MAHO_DO_CHECK
	/// @brief Feeds a declaration's type list into an observation scope (Debug only: it is how the honesty
	///        check knows what the binding PROMISED).
	///
	/// Every region a declaration names is a PROPERTY's tag (a plain member has no region at all), so every one
	/// of them can be observed by the handle its body goes through -- nothing here has to be filtered out.
	/// @tparam TAccesses The declared access types.
	/// @param Scope The node's scope.
	template <typename... TAccesses>
	static void DeclareAll(::Maho::Core::FAccessObservation& Scope, TTypeList<TAccesses...>)
	{
		(Scope.Declare(::Maho::Core::RegionToken<typename TAccesses::FRegion>(),
			::Maho::Core::RegionName<typename TAccesses::FRegion>(), TAccesses::Mode), ...);
	}
#endif
};

/// @brief The recorder a dispatcher submits (declared here, defined below: the dispatcher's `Submit` takes one,
///        and the buffer's own documentation points back at that submit).
class FTaskBuffer;

/// @brief The context POINTERS of a shape (`void(float)` -> `std::tuple<float*>`): what a supply carries, so a
///        context is never copied and never captured by a recording.
/// @tparam TSignature The shape's signature.
template <typename TSignature>
struct TContextPointers;

/// @brief The one case there is: a signature of N parameters.
/// @tparam TArgs The shape's parameters.
template <typename... TArgs>
struct TContextPointers<void(TArgs...)>
{
	/// @brief The context pointers, in the shape's order.
	using FType = std::tuple<std::remove_reference_t<TArgs>*...>;
};

/// @brief ONE node's instance and contexts, supplied at SUBMIT -- in the recording's ORDER.
///
///     // recording (structure only: which types, in which order)
///     Buffer.Begin();
///     Buffer.Record<FLightSwarm>();
///     Buffer.Record<FWorldSwarm>();
///     Buffer.Record<FLightSwarm>();
///     Buffer.End();
///
///     // every frame: one entry per recorded node, IN THE SAME ORDER
///     System.Submit(Buffer,
///         TTaskArgs<FLightSwarm>{ Lights[0], Dt },
///         TTaskArgs<FWorldSwarm>{ World, Dt },
///         TTaskArgs<FLightSwarm>{ Lights[1], Dt });
///
/// This is the other half of a template record: the recording names the TYPES, this names what each of them
/// runs on. The calls line up one-for-one, so the pairing is visible at the call site (and each node carries
/// its OWN contexts -- a per-light delta time is no different from a shared one).
/// @tparam TBinding The node type (task + owner type + shape).
template <typename TBinding>
class TTaskArgs
{
public:
	/// @brief The owner type (what an instance is).
	using FOwner = typename TBinding::FOwner;

	/// @brief The context pointers the shape takes (a type, so `std::apply` can rebuild the call).
	using FContexts = typename TContextPointers<typename TBinding::FShape::FSignature>::FType;

	/// @brief One node's instance and contexts.
	/// @tparam TArgs The contexts (must be exactly the shape's parameters).
	/// @param Owner The instance.
	/// @param Args The contexts (kept by ADDRESS: they must outlive the `Submit` call).
	template <typename... TArgs>
		requires std::is_same_v<std::tuple<TArgs*...>, FContexts>
	explicit TTaskArgs(FOwner& Owner, TArgs&... Args)
		: Instance(&Owner)
		, Contexts(&Args...)
	{
	}

	/// @brief The type this supply is for (what the matching recorded node must be).
	/// @return The token.
	[[nodiscard]] static const void* Type() noexcept { return TBinding::TypeToken(); }

	/// @brief Whether this supply covers MANY nodes of one type (a batch) rather than the next node in order.
	static constexpr bool bBatch = false;

	/// @brief How many nodes it can supply (one: it is the next node in recorded order).
	/// @return 1.
	[[nodiscard]] std::size_t Capacity() const noexcept { return 1; }

	/// @brief Builds this node (and reports which instance it runs on).
	/// @param Occurrence Unused: an aligned supply is the next node, whoever it is.
	/// @param OutInstance Filled with the instance token (what per-instance regions key on).
	/// @return The node.
	[[nodiscard]] FTaskDesc Apply(std::size_t Occurrence, const void*& OutInstance) const
	{
		(void)Occurrence;
		OutInstance = Instance;
		FTaskDesc Out;
		std::apply([&](auto*... Context)
			{
				Out = TBinding::Node(INDEX_NONE, *Instance, *Context...);
			}, Contexts);
		return Out;
	}

private:
	/// @brief The instance this node runs on.
	FOwner* Instance = nullptr;

	/// @brief This node's contexts.
	FContexts Contexts;
};

/// @brief MANY nodes of ONE type: a run of instances plus the contexts they share -- the OTHER way to supply a
///        template record, for a count only the DATA knows (200 lights).
///
///     Buffer.Begin();
///     for (int Index = 0; Index < Count; ++Index) { Buffer.Record<FLightSwarm>(); }   // structure: N nodes
///     Buffer.End();
///
///     System.Submit(Buffer, TTaskBatch<FLightSwarm>{ std::span{ Lights, Count }, Dt });
///
/// It is matched by TYPE (and consumed by occurrence), not by position, so it is the right supply when a type's
/// node count varies from frame to frame; `TTaskArgs` is the right one when the pairing should read at the call
/// site. Both may appear in one `Submit`: a batch takes every node of its type, the aligned entries take the
/// rest, in recorded order.
/// @tparam TBinding The node type (task + owner type + shape).
template <typename TBinding>
class TTaskBatch
{
public:
	/// @brief The owner type (what the instances are).
	using FOwner = typename TBinding::FOwner;

	/// @brief The context pointers the shape takes.
	using FContexts = typename TContextPointers<typename TBinding::FShape::FSignature>::FType;

	/// @brief The batch: one node per owner, all sharing the contexts.
	/// @tparam TArgs The contexts (must be exactly the shape's parameters).
	/// @param InOwners The instances, in recorded order (must outlive the `Submit` call).
	/// @param Args The contexts (kept by ADDRESS: they must outlive the `Submit` call).
	template <typename... TArgs>
		requires std::is_same_v<std::tuple<TArgs*...>, FContexts>
	explicit TTaskBatch(std::span<FOwner> InOwners, TArgs&... Args)
		: Owners(InOwners)
		, Contexts(&Args...)
	{
	}

	/// @brief The batch for ONE instance (shorthand for the count-of-one case).
	/// @tparam TArgs The contexts (must be exactly the shape's parameters).
	/// @param Owner The instance.
	/// @param Args The contexts (kept by ADDRESS: they must outlive the `Submit` call).
	template <typename... TArgs>
		requires std::is_same_v<std::tuple<TArgs*...>, FContexts>
	explicit TTaskBatch(FOwner& Owner, TArgs&... Args)
		: Owners(&Owner, 1)
		, Contexts(&Args...)
	{
	}

	/// @brief The type this supply is for.
	/// @return The token.
	[[nodiscard]] static const void* Type() noexcept { return TBinding::TypeToken(); }

	/// @brief Whether this supply covers MANY nodes of one type (yes: that is the point of a batch).
	static constexpr bool bBatch = true;

	/// @brief How many nodes it can supply (one per owner).
	/// @return The owner count.
	[[nodiscard]] std::size_t Capacity() const noexcept { return Owners.size(); }

	/// @brief Builds the n-th node of this type.
	/// @param Occurrence Which node of this type, in recorded order.
	/// @param OutInstance Filled with the instance token (what per-instance regions key on).
	/// @return The node; an empty `Body` when the batch has run out of owners.
	[[nodiscard]] FTaskDesc Apply(std::size_t Occurrence, const void*& OutInstance) const
	{
		if (Occurrence >= Owners.size())
		{
			return FTaskDesc{};
		}
		FOwner& Owner = Owners[Occurrence];
		OutInstance = &Owner;
		FTaskDesc Out;
		std::apply([&](auto*... Context)
			{
				Out = TBinding::Node(INDEX_NONE, Owner, *Context...);
			}, Contexts);
		return Out;
	}

private:
	/// @brief The instances, in recorded order.
	std::span<FOwner> Owners;

	/// @brief The contexts they share.
	FContexts Contexts;
};

/// @brief One supply, erased for `FTaskBuffer::Bind` and `FTaskDispatcher::Submit`.
struct FTaskSupply
{
	/// @brief `Type()` of the supply.
	const void* Type = nullptr;

	/// @brief The supply itself (it points into the recording's own storage).
	const void* Args = nullptr;

	/// @brief Whether it covers many nodes of its type (a batch) or the next node in recorded order.
	bool bBatch = false;

	/// @brief How many nodes it can supply.
	std::size_t Capacity = 1;

	/// @brief Builds the node it describes.
	FTaskDesc (*Apply)(const void* Args, std::size_t Occurrence, const void*& Instance) = nullptr;
};

/// @brief Applies an erased supply (`MakeSupply`'s function pointer).
/// @tparam TEntry The supply type.
/// @param Args The supply (cast back to what it is).
/// @param Occurrence Which node of its type (ignored by an aligned supply).
/// @param Instance Filled with the instance token.
/// @return The node.
template <typename TEntry>
[[nodiscard]] FTaskDesc ApplySupply(const void* Args, std::size_t Occurrence, const void*& Instance)
{
	return static_cast<const TEntry*>(Args)->Apply(Occurrence, Instance);
}

/// @brief Erases one supply (the one place its type is known).
/// @tparam TEntry The supply type.
/// @param Entry The supply.
/// @return Its record.
template <typename TEntry>
[[nodiscard]] FTaskSupply MakeSupply(const TEntry& Entry)
{
	return FTaskSupply{ TEntry::Type(), &Entry, TEntry::bBatch, Entry.Capacity(), &ApplySupply<TEntry> };
}

/// @brief One supply, erased and STORED IN THE RECORDING (see `FTaskBuffer::Bind`).
///
/// The entry itself (`TTaskArgs`/`TTaskBatch`) is small and trivially copyable -- a pointer plus its context
/// pointers -- so it is copied into `Bytes` and `Supply.Args` points at that copy. Storing the DATA (not a
/// closure) is what lets `Submit` take no arguments at all: the recording knows what it runs on.
struct FStoredSupply
{
	/// @brief The largest supply entry a recording accepts (checked where it is stored).
	static constexpr std::size_t kInlineBytes = 64;

	/// @brief The entry, byte for byte (`Supply.Args` points here).
	std::array<std::byte, kInlineBytes> Bytes{};

	/// @brief The erased view of it (built once, at `Bind`).
	FTaskSupply Supply{};

	/// @brief How many nodes of it this submit has already taken.
	std::size_t Used = 0;
};

/// @brief The DISPATCHER: it owns the frame the application is in, the ledger's generation, and the retirement
///        of the frames that are still in flight -- and it is what submits a recorded buffer.
///
///     FTaskDispatcher Dispatch{ Graph, Ledger };     // once, as a member of the layer's instance
///     FTaskBuffer Buffer;                            // a recording (see below): frame-free, cacheable
///
///     // every frame
///     const std::uint64_t Frame = Dispatch.Advance(Frame);   // the FRAME comes from the host, not from here
///     Buffer.Begin();
///     Buffer.Add<...>(Owner, Args...);                       // ... recorded once (see `FTaskBuffer`)
///     Buffer.End();
///     Dispatch.Submit(Buffer);                               // ... and submitted at this frame
///
/// ## The three pieces, and what each owns
///
/// | piece | owns |
/// |---|---|
/// | `TTaskBinding<Task, Owner>` | the STATIC translation: function -> node (identity recipe, edges, closure) |
/// | `FTaskBuffer` | the RECORDING: task packing, frame-free, therefore cacheable and appendable (secondary) |
/// | `FTaskDispatcher` | the NON-STABLE factors: the frame is injected HERE, the edges are derived HERE, the keys are retired HERE |
///
/// Putting the frame here (and not in the buffer) is what lets a buffer be recorded once and submitted every
/// frame; putting the retirement here is what keeps that from leaking derivation state.
///
/// ## Why it is a MEMBER, not a base class
///
/// The graph is composed, not inherited -- the same conclusion this repo already reached for the frame graph
/// ("the graph is a MEMBER of the frame that drives the sequence; there is no host base"), and here for one
/// more concrete reason: `FTaskGraph` has a raw `Submit(desc, preds)` and a `Wait(keys)` that a layer must NOT
/// reach, and inheriting them would hand those verbs back out through the subclass -- it would un-hide exactly
/// what this class exists to hide. Composition keeps the graph private (there is no accessor), so the only way
/// in is `Submit(Buffer)`, and the only fence a layer gets is this class's `Wait()`.
///
/// What the dispatcher ADDS to the graph is the two things the graph deliberately has none of: the FRAME (the
/// application's absolute counter, and the SLOT it occupies -- see `Slot`) and the RETIREMENT of the frames
/// those frames leave behind.
///
/// ## The frame number is the HOST's
///
/// One authority, passed in (`Advance(Frame)`): a frame loop owns the counter and every layer reads it. It
/// cannot be per-clock -- a key names a node ACROSS graphs, so two layers that counted for themselves would
/// name the same node differently (one layer's "frame 7" is the other's "frame 3"), and the edge between them
/// could never be written. What IS per-layer, and therefore lives here, is the ledger's generation and the
/// retirement of this graph's own keys.
///
/// ## What it makes impossible to forget
///
/// The pairing has three parts and the clock owns all of them, so a layer cannot get them out of step:
/// the frame it is in (as given), the ageing the ledger does by the frame it is given, and the retirement of a finished frame --
/// which means **waiting for its nodes and then forgetting their keys**, the PRECISE path (the ledger's
/// window stays the fallback for a key nobody ever tracked).
///
/// ## Frames in flight
///
/// `FramesInFlight` is how many frames may overlap: 1 (the default) retires the previous frame before opening
/// the next, which is the simplest correct policy. A larger K lets K frames be in flight at once, and then
/// **the ledger's window must be at least K** -- otherwise accessors are pruned while their nodes are still
/// running, and the derivation loses edges silently (see the change's design D3). It is the same K the graph's
/// phase ring uses (`r.FramesInFlight`), and taking both from one source is what keeps the two in step.
class FTaskDispatcher
{
public:
	/// @brief Binds the clock to the layer's graph and ledger.
	/// @param Graph The layer's task graph (it is what a retirement waits on).
	/// @param Ledger The layer's ledger (it is what a retirement forgets in).
	/// @param FramesInFlight How many frames may overlap (at least 1).
	FTaskDispatcher(FTaskGraph& Graph, FMemoryAccessLedger& Ledger, std::size_t FramesInFlight = 1) noexcept
		: Graph(Graph)
		, Ledger(Ledger)
		, FramesInFlight(FramesInFlight == 0 ? 1 : FramesInFlight)
	{
	}

	/// @brief The frame that is open now (0 before the first `Advance`).
	/// @return The frame number.
	[[nodiscard]] std::uint64_t Frame() const noexcept { return CurrentFrame; }

	/// @brief The SLOT this frame occupies in the layer's per-frame arrays: `Frame % FramesInFlight`.
	///
	/// This is the half of the cache story the frame number cannot give: a layer that keeps its contexts (and
	/// its recordings, see `FTaskBuffer`) per slot indexes them with THIS, records a slot once, and lets the
	/// dispatcher submit it every time the ring comes back aframe -- exactly a per-slot command buffer with a
	/// per-slot context.
	/// @return The slot (0 .. FramesInFlight-1).
	[[nodiscard]] std::uint64_t Slot() const noexcept
	{
		return CurrentFrame % static_cast<std::uint64_t>(FramesInFlight);
	}

	/// @brief Fences: blocks until everything this layer submitted is quiet.
	///
	/// The ONE graph verb a layer still needs (shutdown, and a test that wants everything done). It is forwarded
	/// rather than exposed as a graph, see the class comment.
	void Wait() { Graph.Wait(); }

	/// @brief Ends every frame that is `FramesInFlight` or more back, then opens the one the HOST says is current.
	///
	/// The frame number is PASSED IN, never invented here: it is the application's frame counter (the one a
	/// frame loop owns and every layer reads), and it has to be -- a key names a node across graphs, so two
	/// layers that incremented their own counters would name the same node differently and the edge between
	/// them could not be written at all. What this clock owns is only what is per-layer: the ledger's
	/// generation and the retirement of ITS OWN graph's keys.
	///
	/// Retirement is keyed by FRAME NUMBER, not by call count: the host may step the counter by more than one
	/// (a paused stretch, a fixed-timestep loop that runs several steps per call), and what the ledger needs is
	/// still "everything of frame `F - K` is complete" -- waiting `K` CALLS back would leave a skipped frame in
	/// flight while its accessors were aged out, which is a lost edge, not a slow one.
	/// @param Frame The application's frame number, absolute and monotonic (a slot is not a frame).
	/// @return The frame that is now open (the one passed in).
	/// @note Reported when the frame does not advance: a repeated or decreasing frame would reuse keys.
	std::uint64_t Advance(std::uint64_t Frame)
	{
		if (Frame <= CurrentFrame)
		{
			ReportError("FTaskDispatcher::Advance: the frame did not advance -- its keys would be reused");
		}

		// Wait for every frame that is now K or more back -- and those waits ARE the ledger's time model: once
		// they return, everything of those frames is complete, which is exactly what lets the ledger age entries
		// out by frame instead of being told to forget them.
		const std::uint64_t Width = static_cast<std::uint64_t>(FramesInFlight);
		const std::uint64_t Oldest = Frame > Width ? Frame - Width : 0;
		RetiredFrameNumber = 0;
		while (!Pending.empty() && Pending.front().Frame <= Oldest)
		{
			FPendingFrame Retiring = std::move(Pending.front());
			Pending.pop_front();
			if (!Retiring.Keys.empty())
			{
				Graph.Wait(Retiring.Keys);
			}
			RetiredFrameNumber = Retiring.Frame;
		}

		CurrentFrame = Frame;
		return CurrentFrame;
	}

	/// @brief The frame whose nodes the last `Advance` waited for (0 when nothing was retired).
	///
	/// This is what a per-slot cache needs: the frame whose nodes just completed is the one whose slot came free.
	/// @return The retired frame (0 when nothing was retired).
	[[nodiscard]] std::uint64_t RetiredFrame() const noexcept { return RetiredFrameNumber; }

	/// @brief Tracks one key of the frame being submitted, so `Advance` can retire it later.
	///        `Submit` calls this (it is the one that knows what went in).
	/// @param Key A key this frame submitted.
	void Track(FTaskKey Key)
	{
		if (Pending.empty() || Pending.back().Frame != CurrentFrame)
		{
			Pending.push_back(FPendingFrame{ CurrentFrame, {} });
		}
		Pending.back().Keys.push_back(Key);
	}

	/// @brief SUBMITS a recorded buffer at the frame that is open now -- Vulkan's `vkQueueSubmit`.
	///
	/// This is where the frame-dependent factors are INJECTED: per recorded node, the key becomes the stable
	/// key with this frame folded in (`FrameKey`), and the edges are derived THEN (the only moment the ledger is
	/// touched). The description is COPIED, so the buffer survives and can be submitted again at a later frame
	/// -- which is what makes a recording cacheable across frames.
	///
	/// A TEMPLATE node (one recorded with `Record`, which names only its TYPE) runs on whatever `Bind` gave the
	/// recording: the instances and the contexts are never stored here -- nothing about a frame is -- so the same
	/// recording runs on other instances, at other frames. The keys go to this dispatcher as the frame's own, so
	/// `Advance` retires them; the order of the recorded nodes is the order the derivation sees.
	/// @param Buffer The recording to submit (it must be closed, non-empty, and BOUND).
	/// @return How many nodes went in (0 when the buffer is empty, or a node had no supply).
	/// @note The buffer is not consumed: submitting it twice in ONE frame names the same nodes twice, which the
	///       graph refuses -- a cached buffer is for the NEXT frame.
	std::size_t Submit(FTaskBuffer& Buffer);

private:
	/// @brief The layer's graph.
	FTaskGraph& Graph;

	/// @brief The layer's ledger.
	FMemoryAccessLedger& Ledger;

	/// @brief One frame's keys, in submission order: what `Advance` waits on when the frame falls K behind.
	struct FPendingFrame
	{
		/// @brief The frame these keys belong to.
		std::uint64_t Frame = 0;

		/// @brief The keys it submitted.
		std::vector<FTaskKey> Keys;
	};

	/// @brief The frames still in flight, OLDEST first (a frame leaves when it is K back).
	std::deque<FPendingFrame> Pending;

	/// @brief How many frames may overlap: the window `Advance` retires against.
	std::size_t FramesInFlight = 1;

	/// @brief The frame whose nodes the last `Advance` waited for (0 when the ring was not full yet).
	std::uint64_t RetiredFrameNumber = 0;

	/// @brief The frame that is open now.
	std::uint64_t CurrentFrame = 0;
};

/// @brief A PURE RECORDER of one frame of nodes -- Vulkan's command buffer, and deliberately nothing more.
///
///     FTaskBuffer Frame;                       // a recorder: no graph, no clock, no shared state
///
///     Frame.Begin();                            // vkBeginCommandBuffer
///     Frame.Add<FBinding, FShape>(World, Delta);    // vkCmd*        (records only)
///     Frame.WaitFor(LastFramesKey);                  // a shared edge  (a FULL key: a past frame)
///     Frame.End();                              // vkEndCommandBuffer
///
///     Dispatch.Submit(Buffer);                       // vkQueueSubmit  (the frame is the DISPATCHER's)
///
/// ## Recording is PURE, and that is what the three properties rest on
///
/// A recording keeps only the STABLE half of each node's identity (see `StableKeyOf`) plus its closure; the
/// FRAME and the DERIVED EDGES are computed at `Submit`. Nothing is written to the ledger, to the graph or
/// to any other recorder while recording -- so:
///
///   * **parallel recording**: two recorders share nothing, so N threads may record N frames at once;
///   * **cross-frame reuse**: the structure of a frame is usually the same as the last one, so a recorder can
///     record once and be submitted every frame (a per-slot recorder is exactly Vulkan's per-slot command
///     buffer -- keep one per frame-in-flight and record it only when the structure changes);
///   * **secondary recorders**: `Append` moves another recorder's nodes into this one, in order, which is
///     `vkCmdExecuteCommands` -- record a chunk wherever (and however many) and execute it from a primary.
///
/// ## But submission is NOT free of order
///
/// `Submit` derives and submits node by node, and the ledger is what it derives from: the ORDER of the
/// recorded nodes IS the order the derivation sees. That (plus one graph per clock) is the one serialization
/// point -- record in parallel, submit in one place.
///
/// A recorder is reusable until `Begin` starts a new recording: submitting the same recording again is
/// legal and is the point (a cached recording at a later frame), but submitting it twice in ONE frame names the
/// same nodes twice, which the graph refuses.
class FTaskBuffer
{
public:
	/// @brief One recording, empty and closed. Recording needs nothing but this object.
	FTaskBuffer() = default;

	/// @brief Starts a new recording, discarding what this recorder held.
	///
	/// A discarded recording is REPORTED when losing it would surprise: one that was still open (its nodes were
	/// never closed), and one that was closed but never submitted even once (its nodes never ran).
	void Begin()
	{
		if (bOpen)
		{
			ReportError("FTaskBuffer::Begin: the previous recording was never ended -- it is "
				"dropped");
		}
		else if (!Records.empty() && Submits == 0)
		{
			ReportError("FTaskBuffer::Begin: the previous recording was never submitted -- its "
				"nodes never ran");
		}
		Records.clear();
		Occurrences.clear();
		Supplies.clear();
		Shared.clear();
		Gates.clear();
		Merged.clear();
		Submits = 0;
		bOpen = true;
	}

	/// @brief Records `Count` nodes of a type and NOTHING ELSE -- TEMPLATE records.
	///
	/// A recording is STRUCTURE: which node types run, and in what order. Everything that belongs to a frame --
	/// the owner instance and the contexts (a delta time, a world, a slot's data) -- is supplied at `Submit`
	/// (see `TTaskArgs`), so one recording runs on any instances, in any frame, and no instance address or
	/// context address is ever stored in it.
	///
	/// `Count > 1` is the shape a PARALLEL FOR takes (a run of one type, one node per instance; `TTaskBatch`
	/// supplies them). The nodes of one type are told apart in the key by their OCCURRENCE (the `Sub` term), so
	/// nothing has to be named by hand.
	/// @tparam TBinding The binding (task + owner type + shape): the node TYPE.
	/// @param Count How many nodes of it to record (1 -- the default -- is the ordinary single node).
	template <typename TBinding>
	void Record(std::size_t Count = 1)
	{
		for (std::size_t Index = 0; Index < Count; ++Index)
		{
			if (!Guard())
			{
				return;
			}
			std::size_t& Occurrence = OccurrenceOf(TBinding::TypeToken());
			FRecord Node;
			Node.StableKey = TBinding::StableKeyOf({}, Occurrence++);
			Node.Type = TBinding::TypeToken();
			Node.Derive = &DeriveThunk<TBinding>;
			Node.ReKey = &ReKeyThunk<TBinding>;
			Records.push_back(std::move(Node));
		}
	}

	/// @brief BINDS the recording to what it runs on: one supply per recorded node, in recorded order (or a batch
	///        for a whole type).
	///
	/// APPENDS: the entries given here are added to what the recording already has, so the runtime-length form is
	/// a loop of one-entry calls, and the compile-time form is one call with a pack. `UnbindAll` clears them (and
	/// `Begin` does too), so a recording rebuilt every frame needs nothing extra.
	///
	/// WHEN: independent of `Advance` (this touches the recording, never a frame), and it does NOT have to be
	/// repeated every frame -- a one-time binding serves every later `Submit` (`Submit` resets its own cursors).
	/// The one requirement is that the bound OBJECTS outlive the submits, and that what they hold at execution is
	/// what should run: bind once when the contexts are stable members, re-bind when they are this frame's values.
	///
	/// The supplies are COPIED INTO the recording -- an entry is a pointer plus its context pointers -- so a
	/// temporary `TTaskArgs{...}` is fine; what must outlive the submit is the objects it points at.
	///
	/// This is why `Submit` takes no arguments: a recording is "what runs", this is "on what".
	/// @tparam TEntries The `TTaskArgs` / `TTaskBatch` entries, in recorded order.
	/// @param Entries The supplies.
	template <typename... TEntries>
	void Bind(TEntries&&... Entries)
	{
		(StoreSupply(Entries), ...);
	}

	/// @brief Drops every supply: what a loop that binds at runtime starts with, and what a re-bind needs.
	void UnbindAll() { Supplies.clear(); }

	/// @brief Binds ONE node of a type, spelled without the entry type: `Bind<FLightSwarm>(Light, Dt)`.
	///
	/// This is the form application code uses -- the node type is the only template argument, and the contexts
	/// are whatever that shape takes. `TTaskArgs` is what it builds underneath (and what a caller uses directly
	/// only when it wants to hold the entry).
	/// @tparam TBinding The node type (a binding declared with `MAHO_DECLARE_TASK_BINDING`).
	/// @tparam TArgs The contexts of this node (must match the shape).
	/// @param Owner The instance it runs on.
	/// @param Args The contexts.
	template <typename TBinding, typename... TArgs>
		requires CTaskRunnable<typename TBinding::FTask, typename TBinding::FOwner, TArgs...>
	void Bind(typename TBinding::FOwner& Owner, TArgs&... Args)
	{
		StoreSupply(TTaskArgs<TBinding>{ Owner, Args... });
	}

	/// @brief Binds a RUN of one type -- the parallel-for form: `Bind<FLightSwarm>(std::span{ Lights }, Dt)`.
	/// @tparam TBinding The node type.
	/// @tparam TArgs The contexts shared by the run (must match the shape).
	/// @param Owners The instances, in recorded order.
	/// @param Args The contexts.
	template <typename TBinding, typename... TArgs>
		requires CTaskRunnable<typename TBinding::FTask, typename TBinding::FOwner, TArgs...>
	void Bind(std::span<typename TBinding::FOwner> Owners, TArgs&... Args)
	{
		StoreSupply(TTaskBatch<TBinding>{ Owners, Args... });
	}

	/// @brief How many supplies the recording is bound to (diagnostics, and a submit with none is REFUSED).
	/// @return The supply count.
	[[nodiscard]] std::size_t SupplyCount() const noexcept { return Supplies.size(); }

	/// @brief Records a SHARED edge: every node of this frame waits for that key.
	///
	/// This one is a FULL key (it names a node of a PAST frame, so its frame is known -- compute it with
	/// `KeyOf`). Order does not matter inside the recording: nothing is submitted until `Submit`.
	/// @param Key The earlier key every node of this frame waits for.
	void WaitFor(FTaskKey Key)
	{
		Guard();
		Shared.push_back(Key);
	}

	/// @brief Records the frame's GATE: nothing of this frame is submitted until that key has completed.
	///
	/// Waited in `Submit` (not here), so closing a recording never blocks; an unresolved key is skipped
	/// there, like every other unresolved key.
	/// @param Key The key the frame waits for before anything of it goes in.
	void WaitBefore(FTaskKey Key)
	{
		Guard();
		Gates.push_back(Key);
	}

	/// @brief Executes ANOTHER recorder's nodes from this one, in its recorded order (`vkCmdExecuteCommands`).
	///
	/// The other recorder's nodes are MOVED in (it is left empty), so a chunk can be recorded anywhere -- on
	/// another thread, into a per-slot recorder -- and executed from the primary that is being submitted. The
	/// ORDER matters: it is the order the derivation sees (see the class comment).
	/// @param Other The recorder whose nodes to take (emptied).
	void Append(FTaskBuffer&& Other)
	{
		Guard();
		Records.insert(Records.end(),
			std::make_move_iterator(Other.Records.begin()), std::make_move_iterator(Other.Records.end()));
		Other.Records.clear();
		// What a later `Record` must not collide with: the types this recording now holds, its own and the
		// appended ones (their keys already carry their own occurrences).
		ReindexOccurrences();
	}

	/// @brief How many nodes this recording holds.
	/// @return The node count.
	[[nodiscard]] std::size_t NodeCount() const noexcept { return Records.size(); }

	/// @brief How many times this recording has been submitted (a cached recording is submitted many times).
	/// @return The submit count.
	[[nodiscard]] std::size_t SubmitCount() const noexcept { return Submits; }

	/// @brief Declares WHICH SLOT this recording was made for (`kAnySlot` -- the default -- means "any").
	///
	/// Call it before recording if the recording captures per-slot contexts and is cached per slot: it is then
	/// only submitted on frames of that slot, and `FTaskSystem::Submit` REPORTS a mismatch instead of letting a
	/// cached recording read the wrong slot's context. A recording rebuilt every frame needs nothing here.
	/// @param InSlot The slot (`FTaskSystem::Slot()` at the frame it was recorded for), or `kAnySlot`.
	void RecordedFor(std::uint64_t InSlot) { SlotStamp = InSlot; }

	/// @brief The slot this recording was made for (`kAnySlot` when unbound).
	/// @return The slot stamp.
	[[nodiscard]] std::uint64_t Slot() const noexcept { return SlotStamp; }

	/// @brief Closes the recording: recording stops here, and NOTHING is submitted by this class at all --
	///        submission belongs to `FTaskDispatcher::Submit`, which is what turns a recording into nodes.
	/// @return How many nodes the recording holds (0 when none is open).
	std::size_t End()
	{
		if (!bOpen)
		{
			return 0;
		}
		bOpen = false;
		return Records.size();
	}

private:
	/// @brief The dispatcher reads a recording through here (it is the one that submits it).
	friend class FTaskDispatcher;

	/// @brief One recorded node: its type, its stable identity, and how to derive its edges.
	struct FRecord
	{
		/// @brief The frame-free half of its key.
		FTaskKey StableKey = INDEX_NONE;

		/// @brief Its TYPE (`TTaskBinding::TypeToken`): what `Submit` matches a supply against.
		const void* Type = nullptr;

		/// @brief How to derive its edges -- the shape, erased into a call (the ledger is touched only here).
		FDerivationResult (*Derive)(FMemoryAccessLedger& Ledger, FTaskKey Key, std::uint64_t Frame, const void* Instance) = nullptr;

		/// @brief How to recompute its stable key for another occurrence (`Append` renumbers, so two recordings
		///        merged into one cannot share a key).
		FTaskKey (*ReKey)(std::uint64_t Sub) = nullptr;
	};

	/// @brief The derivation thunk a record carries (one per BINDING -- the shape is inside the binding).
	/// @param Ledger The ledger to derive from.
	/// @param Key The node's key.
	/// @param Frame The frame it belongs to (the ledger ages its state by it).
	/// @return What the shape's declaration derived.
	template <typename TBinding>
	static FDerivationResult DeriveThunk(FMemoryAccessLedger& Ledger, FTaskKey Key, std::uint64_t Frame, const void* Instance)
	{
		return TBinding::Derive(Ledger, Key, Frame, Instance);
	}

	/// @brief The re-key thunk a record carries (one per BINDING): its stable key for a given occurrence.
	/// @param Sub The occurrence within the recording.
	/// @return The stable half.
	template <typename TBinding>
	static FTaskKey ReKeyThunk(std::uint64_t Sub)
	{
		return TBinding::StableKeyOf({}, Sub);
	}

	/// @brief Reports (once per call) a recording attempt outside an open frame, and says whether to go on.
	/// @return Whether a recording is open.
	bool Guard() const
	{
		if (!bOpen)
		{
			ReportError("FTaskBuffer: recording outside an open frame -- call Begin first");
		}
		return bOpen;
	}

	/// @brief Stores one supply entry by VALUE (see `Bind`).
	/// @tparam TEntry The entry type (`TTaskArgs` / `TTaskBatch`).
	/// @param Entry The entry.
	template <typename TEntry>
	void StoreSupply(const TEntry& Entry)
	{
		static_assert(std::is_trivially_destructible_v<TEntry>,
			"a supply entry must be trivially destructible: the recording stores it in place");
		static_assert(sizeof(TEntry) <= FStoredSupply::kInlineBytes,
			"a supply entry is larger than the recording's inline storage");
		FStoredSupply Slot;
		::new (static_cast<void*>(Slot.Bytes.data())) TEntry(Entry);   // the entry's own copy constructor
		Supplies.push_back(std::move(Slot));
		// The view points at the RECORDING's copy, not at the local slot (a deque keeps element addresses).
		FStoredSupply& Stored = Supplies.back();
		Stored.Supply = MakeSupply(*reinterpret_cast<const TEntry*>(Stored.Bytes.data()));
	}

	/// @brief The supplies this recording is bound to (see `Bind`).
	std::deque<FStoredSupply> Supplies;

	/// @brief How many nodes of each TYPE this recording holds so far -- what numbers a record's key
	///        (`Sub`), so recording one type twice records two distinguishable nodes.
	/// @param Type The type token.
	/// @return Its occurrence count (and the slot that count is stored in).
	std::size_t& OccurrenceOf(const void* Type)
	{
		for (auto& Entry : Occurrences)
		{
			if (Entry.first == Type)
			{
				return Entry.second;
			}
		}
		Occurrences.emplace_back(Type, 0);
		return Occurrences.back().second;
	}

	/// @brief Re-derives the per-type counters from what the recording now holds (after `Append`): what a later
	///        `Record` must not collide with.
	void ReindexOccurrences()
	{
		Occurrences.clear();
		for (FRecord& Node : Records)
		{
			// A record that came from another recording keeps its TYPE but gets THIS recording's occurrence:
			// two appended chunks of one type must not share a key.
			Node.StableKey = Node.ReKey(OccurrenceOf(Node.Type));
			++OccurrenceOf(Node.Type);
		}
	}

	/// @brief The recording (reused storage: a recording allocates nothing once it has grown).
	std::vector<FRecord> Records;

	/// @brief The per-type occurrence counters of the OPEN recording (see `OccurrenceOf`).
	std::vector<std::pair<const void*, std::size_t>> Occurrences;

	/// @brief The edges EVERY node of this frame waits for (full keys: they name past frames).
	std::vector<FTaskKey> Shared;

	/// @brief What the frame waits for before anything of it is submitted.
	std::vector<FTaskKey> Gates;

	/// @brief One node's shared+own edges, reused between nodes (capacity is the point).
	std::vector<FTaskKey> Merged;

	/// @brief Whether a recording is open (`Begin` ran, `End` has not).
	bool bOpen = false;

	/// @brief How many times this recording has been submitted.
	std::size_t Submits = 0;

	/// @brief The slot this recording was made for (`kAnySlot` = unbound).
	std::uint64_t SlotStamp = kAnySlot;
};

/// @brief `FTaskDispatcher::Submit` -- defined here because it reads a recording, and a recording is only
///        complete above (the direction is the Vulkan one: the DISPATCHER submits a BUFFER, not the reverse).
inline std::size_t FTaskDispatcher::Submit(FTaskBuffer& Buffer)
{
	if (Buffer.bOpen)
	{
		ReportError("FTaskDispatcher::Submit: the buffer is still open -- call End first");
		return 0;
	}
	if (Buffer.Records.empty())
	{
		return 0;
	}
	if (!Buffer.Gates.empty())
	{
		Graph.Wait(Buffer.Gates);
	}

	// The supplies came with `Bind`: they live in the recording, so this call takes no arguments. `Used` is
	// reset here, so the same recording can be submitted again later (a cached recording, a per-frame one).
	for (FStoredSupply& Slot : Buffer.Supplies)
	{
		Slot.Used = 0;
	}
	std::size_t NextAligned = 0;

	std::size_t Submitted = 0;
	for (const FTaskBuffer::FRecord& Node : Buffer.Records)
	{
		// EVERY node takes its instance and its contexts from a supply. A BATCH of its type takes it first (that
		// is what a batch declares: "every node of this type is mine"); otherwise the next ALIGNED supply is
		// used, and it has to be the type this node is.
		FTaskDesc Desc;
		const void* Instance = nullptr;

		FStoredSupply* Chosen = nullptr;
		for (FStoredSupply& Slot : Buffer.Supplies)
		{
			if (Slot.Supply.bBatch && Slot.Supply.Type == Node.Type && Slot.Used < Slot.Supply.Capacity)
			{
				Chosen = &Slot;
				break;
			}
		}
		if (Chosen == nullptr)
		{
			// The next supply that is not a batch and not used yet.
			while (NextAligned < Buffer.Supplies.size()
				&& (Buffer.Supplies[NextAligned].Supply.bBatch || Buffer.Supplies[NextAligned].Used > 0))
			{
				++NextAligned;
			}
			if (NextAligned == Buffer.Supplies.size())
			{
				ReportError("FTaskDispatcher::Submit: the recording has more nodes than supplies -- Bind one "
					"TTaskArgs per recorded node (in order) or a TTaskBatch for its type");
				continue;
			}
			Chosen = &Buffer.Supplies[NextAligned++];
			if (Chosen->Supply.Type != Node.Type)
			{
				ReportError("FTaskDispatcher::Submit: the next supply is for another node type -- supplies "
					"must follow the recording's order");
				continue;
			}
		}

		Desc = Chosen->Supply.Apply(Chosen->Supply.Args, Chosen->Used++, Instance);
		if (!Desc.Body)
		{
			ReportError("FTaskDispatcher::Submit: a batch ran out of instances -- it must supply one per "
				"recorded node of its type");
			continue;
		}

		// The frame is injected HERE, and nowhere earlier: that is why a recording has no frame in it and can
		// be cached across frames.
		const FTaskKey Key = FrameKey(Node.StableKey, CurrentFrame);

		Buffer.Merged.assign(Buffer.Shared.begin(), Buffer.Shared.end());   // reuses capacity
		const FDerivationResult Edges = Node.Derive(Ledger, Key, CurrentFrame, Instance);
		Buffer.Merged.insert(Buffer.Merged.end(), Edges.Predecessors.begin(), Edges.Predecessors.end());

		Desc.Key = Key;
		Track(Key);
		Graph.Submit(std::move(Desc), Buffer.Merged);
		++Submitted;
	}

	// Nothing is silently ignored: an aligned supply that was never used means the recording has fewer nodes
	// than the caller thought.
	for (FStoredSupply& Slot : Buffer.Supplies)
	{
		if (!Slot.Supply.bBatch && Slot.Used == 0)
		{
			ReportError("FTaskDispatcher::Submit: an aligned supply was not used -- the recording has fewer "
				"nodes than supplies (a batch takes every node of its type)");
			break;
		}
	}

	++Buffer.Submits;
	return Submitted;
}

/// @brief ONE-STOP execution environment for a layer: it owns the graph, the ledger and the dispatcher, and it
///        is the only thing a layer has to hold.
///
///     FTaskSystem System{ Pool, K };      // the POOL stays outside: it is what layers SHARE
///     System.Initialize();
///
///     // every frame (on the driver thread)
///     System.Advance(Frame);              // waits Frame-K, advances, broadcasts OnAdvance (and its slot)
///     Buffer.Begin(); ... Buffer.End();
///     System.Submit(Buffer);              // injects the frame, derives, submits, tracks for retirement
///
/// ## What it buys over holding the three pieces yourself
///
///   * **one K**: the dispatcher's `FramesInFlight` and the ledger's `FramesInFlight` are the same constructor
///     argument, so "keep them in step" stops being a rule nobody can check -- a mismatched pair cannot exist;
///   * **one place for the invariants**: advance-before-submit, wait `Frame-K`, and retire are the system's
///     verbs, not a sequence the layer has to remember across three headers;
///   * **destruction order is a declaration fact**: the dispatcher holds references to the other two, so the
///     members are declared (graph, ledger, dispatcher) and die in the reverse order -- a layer cannot get the
///     teardown order wrong;
///   * **no bypass**: the graph is private, so `Submit(desc, preds)` and `Wait(keys)` are not reachable from a
///     layer; the public surface is exactly the verbs below.
///
/// ## What it owns and what it does NOT
///
/// It owns EXECUTION STATE (threads, derivation state, the frame/slot, the keys in flight). It does NOT own a
/// layer's DATA: the per-slot contexts and the recordings stay with the layer (only it knows the shapes), which
/// is why it offers `Slot()` rather than a context store, and why a recording needs `RecordedFor`.
class FTaskSystem
{
public:
	/// @brief Builds the environment on a pool (which the CALLER owns, and may share between layers).
	/// @param Pool The thread pool.
	/// @param FramesInFlight How many frames may overlap (at least 1); the dispatcher and the ledger both use it.
	explicit FTaskSystem(FThreadPool& Pool, std::size_t FramesInFlight = 1) noexcept
		: Frames(FramesInFlight == 0 ? 1 : FramesInFlight)
		, Graph(Pool)
		, Ledger(Frames)
		, Dispatcher(Graph, Ledger, Frames)
	{
	}

	/// @brief Not movable, not copyable: it owns threads and the other two members point at each other's
	///        addresses, so it has one place in memory for its whole life (a member of the layer's instance).
	FTaskSystem(const FTaskSystem&) = delete;

	/// @brief Not copy-assignable (see the constructor).
	FTaskSystem& operator=(const FTaskSystem&) = delete;

	/// @brief Starts the graph's scheduler lane.
	/// @return Whether it came up (the graph reports why not).
	bool Initialize()
	{
		bInitialized = Graph.Initialize();
		return bInitialized;
	}

	/// @brief Opens a frame: retires what is `Frame - K` back, commits the new frame and slot, then broadcasts
	///        `OnAdvance` with what the handler needs.
	///
	/// The broadcast happens AFTER the state is committed, so a handler sees a consistent new frame. It runs on
	/// the caller's (driver) thread, synchronously -- a handler must be cheap and must NOT call `Submit`,
	/// `Advance` or `Wait` (that is reentrancy, and this class does not support it).
	/// @param Frame The application's frame number, absolute and monotonic (a slot is not a frame).
	/// @return The frame that is now open.
	std::uint64_t Advance(std::uint64_t Frame)
	{
		// A frame that does not advance would REUSE keys, so the system refuses it instead of carrying on:
		// the debugger stops here (a programmer error), and Shipping -- where the check compiles out -- still
		// gets the safe answers below (no state change, the old frame returned).
		MAHO_ENSURE_BREAK(Frame > Dispatcher.Frame(),
			"FTaskSystem::Advance: the frame must advance (frames start at 1)");
		if (Frame <= Dispatcher.Frame())
		{
			return Dispatcher.Frame();
		}

		bFrameOpen = true;
		const std::uint64_t Opened = Dispatcher.Advance(Frame);
		OnAdvance.Broadcast(Opened, Dispatcher.Slot(), RetiredFrame());
		return Opened;
	}

	/// @brief Submits a recorded buffer at the frame that is open now.
	///
	/// Refuses a buffer recorded for ANOTHER slot (see `FTaskBuffer::RecordedFor`): that is the one mistake
	/// per-slot caching makes, and this is the only place that can see it.
	/// @tparam (the supplies belong to the recording -- see `FTaskBuffer::Bind`)
	/// @param Buffer The recording to submit.
	/// @return How many nodes went in (0 when it was refused or empty).
	std::size_t Submit(FTaskBuffer& Buffer)
	{
		// The two things that would make a submission meaningless: a graph that never started (the work would
		// be dropped), and no open frame (the keys would be tracked against a slot nobody retires). Both are
		// programmer errors -- the debugger stops -- and both REFUSE cheaply, so a Shipping build is safe too.
		MAHO_ENSURE_BREAK(bInitialized, "FTaskSystem::Submit: Initialize() was never called");
		if (!bInitialized)
		{
			return 0;
		}
		MAHO_ENSURE_BREAK(bFrameOpen, "FTaskSystem::Submit: no frame is open -- call Advance first");
		if (!bFrameOpen)
		{
			return 0;
		}

		if (Buffer.Slot() != kAnySlot && Buffer.Slot() != Dispatcher.Slot())
		{
			ReportError("FTaskSystem::Submit: the buffer was recorded for another slot -- a per-slot recording "
				"may only be submitted on its own frames");
			return 0;
		}
		return Dispatcher.Submit(Buffer);
	}

	/// @brief Fences: blocks until everything this layer submitted is quiet (shutdown, and tests).
	void Wait() { Dispatcher.Wait(); }

	/// @brief The frame that is open now (0 before the first `Advance`).
	/// @return The frame number.
	[[nodiscard]] std::uint64_t Frame() const noexcept { return Dispatcher.Frame(); }

	/// @brief The SLOT this frame occupies (`Frame % K`) -- what a layer indexes its per-slot data with.
	/// @return The slot.
	[[nodiscard]] std::uint64_t Slot() const noexcept { return Dispatcher.Slot(); }

	/// @brief How many frames may overlap (the K a layer sizes its per-slot arrays with).
	/// @return K.
	[[nodiscard]] std::size_t FramesInFlight() const noexcept { return Frames; }

	/// @brief How many accessors the derivation remembers (diagnostics: it must not grow with the frame count).
	/// @return The entry count.
	[[nodiscard]] std::size_t Remembered() const noexcept { return Ledger.Count(); }

	/// @brief Broadcast when a frame opens: `(Frame, Slot, RetiredFrame)`.
	///
	/// Fired every `Advance` (not only when the slot changes: with K = 1 the slot never changes), after the
	/// state is committed, on the driver thread. `RetiredFrame` is the frame whose nodes were just awaited
	/// (0 when nothing was retired yet), which is what a layer needs to invalidate a per-slot cache.
	TMulticastEvent<void(std::uint64_t Frame, std::uint64_t Slot, std::uint64_t RetiredFrame)> OnAdvance;

private:
	/// @brief The frame whose nodes this advance waited for (0 when none had been tracked yet).
	[[nodiscard]] std::uint64_t RetiredFrame() const noexcept
	{
		return Dispatcher.RetiredFrame();
	}

	/// @brief How many frames may overlap: ONE number, used by the ledger (ageing) and the dispatcher (waiting),
	///        declared first so both members can be built with it.
	std::size_t Frames = 1;

	/// @brief Whether `Initialize` came up (a submission before that would be dropped by the pool).
	bool bInitialized = false;

	/// @brief Whether a frame is open (`Advance` ran and the system is still alive for it).
	bool bFrameOpen = false;

	/// @brief The graph -- declared after `Frames` (which the two members below are built with) so that it
	///        outlives them; the dispatcher is declared LAST because it points into the other two.
	FTaskGraph Graph;

	/// @brief The derivation state (its K is the dispatcher's K: one argument, not two).
	FMemoryAccessLedger Ledger;

	/// @brief The dispatcher: it holds references to the two above, so it must die first (declared last).
	FTaskDispatcher Dispatcher;
};

}
}
