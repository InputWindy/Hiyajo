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

#include <cstdint>
#include <functional>
#include <iterator>
#include <span>
#include <string_view>
#include <type_traits>
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
///     // -> using FWorldTick = TTaskBinding<FWorld, ITick, ITick::FMethod<void(FEntity&, int&)>>;
///     //    + the TBindingAccess specialization that declares the two accesses
///
/// The fragments cannot be split across several calls: the declaration IS one type list, and a list cannot be
/// grown by separate macro invocations.
/// @param Name The node kind's name (what a recorder names; see `MAHO_TASK_BINDING` for the type-level form).
/// @param Owner The owner type (must be a namespace-scope type: the specialization is written there, and it
///        must INHERIT the task -- a task is an interface, and a binding is an implementation of it).
/// @param Method The method (its task is derived: `Tick` -> `ITick`).
/// @param Signature The shape's parameter list, in parentheses.
/// @param ... Its access fragments (`MAHO_MEMORY_ACCESS_READ/WRITE`/`INCLUDE`/`NONE`).
#define MAHO_DECLARE_TASK_BINDING(Name, Owner, Method, Signature, ...)                                   \
	/** @brief The node KIND this declaration names (what a recorder records). */                        \
	using Name = MAHO_TASK_BINDING(Owner, Method, Signature);                                            \
                                                                                                         \
	static_assert(std::is_base_of_v<MAHO_TASK_NAME(Method), Owner>,                                      \
		"MAHO_DECLARE_TASK_BINDING: the owner must IMPLEMENT the task's interface (inherit from it)");   \
	template <>                                                                                          \
	struct ::Maho::Core::TBindingAccess<Owner, MAHO_TASK_NAME(Method),                                   \
		typename MAHO_TASK_NAME(Method)::template FMethod<void (MAHO_TASK_UNPAREN(Signature))>>          \
	{                                                                                                    \
		/** @brief The memory this implementation touches (composed by the compiler). */                  \
		using List = typename ::Maho::TCatch<__VA_ARGS__>::Type;                                         \
                                                                                                         \
		/** @brief The declaration's readable name (what its metadata rows are keyed by). */              \
		inline static const std::string TagName = std::string(#Owner "|" #Method "|" #Signature);        \
                                                                                                         \
		/** @brief Files the diagnostic rows, once, when this type is instantiated. */                    \
		inline static const bool bFiled = ::Maho::Core::RegisterDeclaredAccesses(TagName, List{});        \
	}

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
/// @tparam TOwner The owner type.
/// @tparam TTaskType The task declaration.
/// @tparam TShape The task's shape (a specialization of its `FMethod`).
template <typename TOwner, typename TTaskType, typename TShape>
struct TBindingAccess;

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
/// `MAHO_TASK_BINDING`), and let `MAHO_DECLARE_TASK_BINDING` file its row.
/// @tparam TOwner The owner type whose method the shape is called on.
/// @tparam TTaskType The task declaration.
/// @tparam TShape One of that task's declared shapes.
template <typename TOwner, typename TTaskType, typename TShape>
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

	/// @brief Folds one more value into a key (see `MixKey`; kept here so a policy reads as one expression).
	/// @param Key The key being built (updated in place).
	/// @param Value The value to fold in.
	static void Mix(FTaskKey& Key, std::uint64_t Value) noexcept { MixKey(Key, Value); }

	/// @brief The STABLE half of a node's key: everything that does NOT change from frame to frame.
	///
	/// This is what a RECORDING keeps. The frame is folded in at SUBMIT (see `KeyOf`), which is what makes a
	/// recording reusable across frames at all: the same recorded frame submitted in one frame and in the next
	/// names two different nodes with one recording.
	///
	/// Its first term is the BINDING's own token -- the row of `(owner, task, shape)` -- so the owner TYPE is
	/// inside it already (one task serves any number of owners, and two of them are two node kinds), and a
	/// binding nobody declared has no token at all: it cannot even be named.
	/// @param Id The instance identity (default: the only instance of this owner type).
	/// @param Sub A discriminator for a second node of the same shape, instance and frame.
	/// @return The stable half.
	[[nodiscard]] static FTaskKey StableKeyOf(FInstanceId Id = {}, std::uint64_t Sub = 0) noexcept
	{
		FTaskKey Key = 0xcbf29ce484222325ull;
		Mix(Key, reinterpret_cast<std::uintptr_t>(
			DeclarationToken<TBindingAccess<TOwner, TTaskType, TShape>>()));
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
		return Ledger.Record<TBindingAccess<TOwner, TTaskType, TShape>>(Key, Frame, Instance);
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
		Out.DebugKey = TBindingAccess<TOwner, TTaskType, TShape>::TagName;
#endif
		// By reference, both: the contexts belong to the caller (see the file comment). A pack capture, so a
		// shape of any arity forwards to the same static call the task declares.
#if MAHO_DO_CHECK
		// The DECLARATION is honest? The body's handle accesses are observed here and compared with what the
		// binding declared -- both directions, once each. Diagnostics only: the derivation happened at submit.
		Out.Body = [&Owner, &Args...]()
		{
			::Maho::Core::FAccessObservation Scope;
			DeclareAll(Scope, typename TBindingAccess<TOwner, TTaskType, TShape>::List{});
			TTaskType::Run(Owner, Args...);
			Scope.Report(TBindingAccess<TOwner, TTaskType, TShape>::TagName);
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
		, Recent(FramesInFlight == 0 ? 1 : FramesInFlight)
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
		return CurrentFrame % static_cast<std::uint64_t>(Recent.size());
	}

	/// @brief Fences: blocks until everything this layer submitted is quiet.
	///
	/// The ONE graph verb a layer still needs (shutdown, and a test that wants everything done). It is forwarded
	/// rather than exposed as a graph, see the class comment.
	void Wait() { Graph.Wait(); }

	/// @brief Ends the frame that is `FramesInFlight` back, then opens the one the HOST says is current.
	///
	/// The frame number is PASSED IN, never invented here: it is the application's frame counter (the one a
	/// frame loop owns and every layer reads), and it has to be -- a key names a node across graphs, so two
	/// layers that incremented their own counters would name the same node differently and the edge between
	/// them could not be written at all. What this clock owns is only what is per-layer: the ledger's
	/// generation and the retirement of ITS OWN graph's keys.
	/// @param Frame The application's frame number, absolute and monotonic (a slot is not a frame).
	/// @return The frame that is now open (the one passed in).
	/// @note Reported when the frame does not advance: a repeated or decreasing frame would reuse keys.
	std::uint64_t Advance(std::uint64_t Frame)
	{
		if (Frame <= CurrentFrame)
		{
			ReportError("FTaskDispatcher::Advance: the frame did not advance -- its keys would be reused");
		}

		TrackedSlot = (TrackedSlot + 1) % Recent.size();

		// Wait for the frame that is K back -- and that wait IS the ledger's time model: once it returns,
		// everything of that frame is complete, which is exactly what lets the ledger age entries out by frame
		// instead of being told to forget them.
		const std::uint64_t Width = static_cast<std::uint64_t>(Recent.size());
		std::vector<FTaskKey>& Keys = Recent[TrackedSlot];
		RetiredFrameNumber = Frame > Width ? Frame - Width : 0;
		if (!Keys.empty())
		{
			Graph.Wait(Keys);
			Keys.clear();
		}

		CurrentFrame = Frame;
		return CurrentFrame;
	}

	/// @brief The frame whose nodes the last `Advance` waited for (0 when the ring was not full yet).
	///
	/// This is what a per-slot cache needs: the slot that just came free is the one that frame occupied.
	/// @return The retired frame (0 when nothing was retired).
	[[nodiscard]] std::uint64_t RetiredFrame() const noexcept { return RetiredFrameNumber; }

	/// @brief Tracks one key of the frame being submitted, so `Advance` can retire it later.
	///        `Submit` calls this (it is the one that knows what went in).
	/// @param Key A key this frame submitted.
	void Track(FTaskKey Key) { Recent[TrackedSlot].push_back(Key); }

	/// @brief SUBMITS a recorded buffer at the frame that is open now -- Vulkan's `vkQueueSubmit`.
	///
	/// This is where the frame-dependent factors are INJECTED: per recorded node, the key becomes the stable
	/// key with this frame folded in (`FrameKey`), and the edges are derived THEN (the only moment the ledger is
	/// touched). The description is COPIED, so the buffer survives and can be submitted again at a later frame
	/// -- which is what makes a recording cacheable across frames.
	///
	/// The keys go to this dispatcher as the frame's own, so `Advance` retires them; the order of the recorded
	/// nodes is the order the derivation sees (see `FTaskBuffer`).
	/// @param Buffer The recording to submit (it must be closed, and non-empty).
	/// @return How many nodes went in (0 when the buffer is empty).
	/// @note The buffer is not consumed: submitting it twice in ONE frame names the same nodes twice, which the
	///       graph refuses -- a cached buffer is for the NEXT frame.
	std::size_t Submit(FTaskBuffer& Buffer);

private:
	/// @brief The layer's graph.
	FTaskGraph& Graph;

	/// @brief The layer's ledger.
	FMemoryAccessLedger& Ledger;

	/// @brief One list of keys per frame that may be in flight (reused: a frame allocates nothing).
	std::vector<std::vector<FTaskKey>> Recent;

	/// @brief The slot the frame being submitted tracks into (also the slot the next `Advance` retires).
	std::size_t TrackedSlot = 0;

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
		Shared.clear();
		Gates.clear();
		Merged.clear();
		Submits = 0;
		bOpen = true;
	}

	/// @brief Records one node: its stable identity, how to derive its edges, and what to run.
	///
	/// The key is NOT computed here (the frame is not known yet); the edges are NOT derived here (the ledger
	/// must not be touched while recording). Both happen in `Submit`.
	/// @tparam TDispatch The binding: task + owner type.
	/// @tparam TShape One of that task's declared shapes.
	/// @tparam TArgs The contexts of this run (must match the shape).
	/// @param Owner The owner instance the closure will run on.
	/// @param Args The contexts of this run.
	/// @return The node's STABLE key -- fold a frame into it (`FrameKey`) to name the node of that frame.
	template <typename TBinding, typename... TArgs>
		requires CTaskRunnable<typename TBinding::FTask, typename TBinding::FOwner, TArgs...>
	FTaskKey Add(typename TBinding::FOwner& Owner, TArgs&... Args)
	{
		const FTaskKey StableKey = TBinding::StableKeyOf();
		Record(StableKey, &DeriveThunk<TBinding>, TBinding::Node(INDEX_NONE, Owner, Args...), &Owner);
		return StableKey;
	}

	/// @brief Records one node under a STABLE key the CALLER computed (its own key policy, still frame-free).
	/// @tparam TBinding The binding (task + owner type + shape).
	/// @tparam TArgs The contexts of this run (must match the shape).
	/// @param StableKey The stable half of the node's key (see `TTaskBinding::StableKeyOf`).
	/// @param Owner The owner instance the closure will run on.
	/// @param Args The contexts of this run.
	template <typename TBinding, typename... TArgs>
		requires CTaskRunnable<typename TBinding::FTask, typename TBinding::FOwner, TArgs...>
	void Add(FTaskKey StableKey, typename TBinding::FOwner& Owner, TArgs&... Args)
	{
		Record(StableKey, &DeriveThunk<TBinding>, TBinding::Node(INDEX_NONE, Owner, Args...), &Owner);
	}

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

	/// @brief One recorded node: its stable identity, how to derive its edges, and what to run.
	struct FRecord
	{
		/// @brief The node (closure and diagnostic name); its `Key` is filled at submit.
		FTaskDesc Desc;

		/// @brief The frame-free half of its key.
		FTaskKey StableKey = INDEX_NONE;

		/// @brief The owner INSTANCE it runs on: the instance token its per-instance regions take.
		const void* Instance = nullptr;

		/// @brief How to derive its edges -- the shape, erased into a call (the ledger is touched only here).
		FDerivationResult (*Derive)(FMemoryAccessLedger& Ledger, FTaskKey Key, std::uint64_t Frame, const void* Instance) = nullptr;
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

	/// @brief Files one recorded node.
	/// @param StableKey The frame-free half of its key.
	/// @param Derive How to derive its edges.
	/// @param Desc The node (its key is filled at submit).
	/// @param Instance The owner instance it runs on (what its per-instance regions key on).
	void Record(FTaskKey StableKey, FDerivationResult (*Derive)(FMemoryAccessLedger&, FTaskKey, std::uint64_t, const void*),
		FTaskDesc Desc, const void* Instance)
	{
		if (!Guard())
		{
			return;
		}
		Records.push_back(FRecord{ std::move(Desc), StableKey, Instance, Derive });
	}

	/// @brief The recording (reused storage: a recording allocates nothing once it has grown).
	std::vector<FRecord> Records;

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

	for (const FTaskBuffer::FRecord& Node : Buffer.Records)
	{
		// The frame is injected HERE, and nowhere earlier: that is why a recording has no frame in it and can
		// be cached across frames.
		const FTaskKey Key = FrameKey(Node.StableKey, CurrentFrame);

		Buffer.Merged.assign(Buffer.Shared.begin(), Buffer.Shared.end());   // reuses capacity
		const FDerivationResult Edges = Node.Derive(Ledger, Key, CurrentFrame, Node.Instance);
		Buffer.Merged.insert(Buffer.Merged.end(), Edges.Predecessors.begin(), Edges.Predecessors.end());

		FTaskDesc Desc = Node.Desc;                       // a COPY: the recording stays re-submittable
		Desc.Key = Key;
		Track(Key);
		Graph.Submit(std::move(Desc), Buffer.Merged);
	}

	++Buffer.Submits;
	return Buffer.Records.size();
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
