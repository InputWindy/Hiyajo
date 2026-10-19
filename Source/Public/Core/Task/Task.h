/// @file
/// @brief The unit this backend translates: a task declaration -- a NAMED set of overloads of ONE method, and
///        the INTERFACE every implementation of them is written against.
///
/// A task is a DECLARATION and an INTERFACE. The declaration type carries the identity (a string literal:
/// stable across a module boundary, readable in a dump), the shapes the method can be called with, and the
/// call. As an INTERFACE it is the abstract base an owner inherits: `MAHO_DECLARE_TASK_METHOD_OVERRIDE` emits
/// one pure virtual per shape, all named after the method, so the compiler -- not a convention -- is what
/// says "this class may use this task". What memory a shape touches is NOT here: that belongs to the
/// implementation's BINDING (`MAHO_DECLARE_TASK_BINDING` in `Core/Task/TaskDispatch.h`).
///
///     // ── the METHOD's accesses first: a task is a function before it is a task ──
///     struct FMyBufferRegion {};
///     MAHO_DECLARE_REGION(FMyBufferRegion);
///     MAHO_DECLARE_ACCESSES(FMyTask_Execute, MAHO_MEMORY_ACCESS_WRITE(FMyBuffer));   // the memory; `Region` is implied
///
///     // ── the task: an INTERFACE for one method, declared one SHAPE at a time ──
///     MAHO_BEGIN_DECLARE_TASK(Execute)                    // declares `IExecute`
///         MAHO_DECLARE_TASK_METHOD_OVERRIDE(Execute, (int&))
///         MAHO_DECLARE_TASK_METHOD_OVERRIDE(Execute, (int&, float))
///     MAHO_END_DECLARE_TASK(Execute)
///
///     // ── an owner IMPLEMENTS it: every declared shape, named after the method ──
///     struct FMyOwner : IExecute
///     {
///         void Execute(int& DeltaMs) override { ... }
///         void Execute(int& DeltaMs, float Scale) override { ... }
///     };
///
///     // ── a builder runs one: the DECLARED shapes are the interface ──
///     IExecute::Run(Owner, Delta, Scale);
///
/// Three steps, each one the compiler's job: the accesses belong to the FUNCTION (whatever it is called
/// through), each SHAPE states one signature the call can take, and the access list of a shape is composed
/// from the fragments its BINDING was given. Every one of them is checked, so a spelling mistake is a
/// compile error rather than a silently empty set -- and a call shaped like no declaration is not a viable
/// overload at all.
///
/// ## A shape's identity IS its signature
///
/// Nothing about a shape is named by hand. `MAHO_DECLARE_TASK_METHOD_OVERRIDE(Execute, (int&, float))`
/// specializes a member template declared by `MAHO_BEGIN_DECLARE_TASK`, so the compiler builds the shape's
/// identity out of what is written: `IExecute::FMethod<void(int&, float)>`. That one type is simultaneously
///
///   * the shape's IDENTITY (what a builder names, and what its access rows are keyed by),
///   * its CONTRACT (`FSignature`, and `FContexts` the compiler derived from it), and
///   * the DECLARATION (`List`, filed by the binding) --
///     so a builder feeds the ledger with the shape itself: `Ledger.Record<IExecute::FMethod<void(int&, float)>>(Key)`.
///
/// The signature is written as the method's PARAMETER LIST, in parentheses (`(int&, float)`), and the macro
/// completes it: the parentheses keep a comma out of the preprocessor's argument list, the parameter list is
/// exactly what a pure virtual needs, and the function type (`void(int&, float)`) is what the identity is
/// built from. The method name is written once per shape, because it is what the virtual is NAMED -- a macro
/// carries no state, so this is the one token no argument can imply.
///
/// Two shapes of one method may touch DIFFERENT memory, which is exactly why each binding declares its own
/// accesses. The method name lives on the TASK (that is what makes them shapes of one method), and the TASK's
/// name is derived from it too (`I` + `Execute`), because that is what the declaration means: the interface
/// FOR that method. The call is a plain call resolved per OWNER type -- the owner is a template parameter, so
/// the call site never looks anything up, and the virtual is what makes "may use this task" a check the
/// compiler performs when the owner is defined.
///
/// ## The name is `I<Method>`, so it is declared where it is USED
///
/// Being an interface, the name is a plain C++ type name in the NAMESPACE the declaration is written in --
/// and the macros can only spell it unqualified, so the task, its owners and its bindings belong to one
/// namespace (a layer's own). That is also what keeps it clear of `Maho::ITick`, the ENGINE's stage interface
/// (`Source/Public/Engine/Engine.h`): a layer's `ITick` is its own contract, and the two never meet.
///
/// The one thing still written by hand is a FUNCTION's own declaration (`MAHO_DECLARE_ACCESSES(FMyTask_Execute,
/// ...)`): it is a type at namespace scope, and no type's name can be derived from anything -- a shape's can,
/// because a shape is keyed by a signature, and a signature is a type the compiler already has.
///
/// ## What a task is NOT
///
///   * **not a key**: a caller folds `StaticName()` with an ABSOLUTE round to build one (see the key contract
///     in `Core/Task/TaskBuilder.h`). A task type names the work; a key names one RUN of it.
///   * **not a lifetime**: creating and destroying belongs to whoever owns the objects (see
///     `Core/Task/AccessDerivation.h` -- a ledger orders, it never judges).
///   * **not an ordering policy**: a shape declares WHAT it touches (memory accesses, `Core/Task/Access.h`);
///     what that implies for ordering -- and what is exempt from it -- is the caller's decision.
///   * **not stateful in itself**: a task carries no instance state. The CONTEXTS are what a run is given and
///     the OWNER is what implements them.
///
/// ## How a task relates to the rest of this folder
///
///   | piece | says |
///   |---|---|
///   | `MAHO_BEGIN_DECLARE_TASK` | "an interface for this method exists: its identity is `I` + the name" |
///   | `MAHO_DECLARE_TASK_METHOD_OVERRIDE` | "and this is one SHAPE of it: a pure virtual here, an access declaration at the binding" |
///   | `MAHO_END_DECLARE_TASK` | "that is all of them" |
///   | `FMemoryAccessLedger` | "so these earlier keys must run first" |
///   | `FTaskDesc` | "here is the closure to run, under this key" -- what the graph consumes |
#pragma once


#include <Core/MetaData.h>
#include <Core/Task/Access.h>
#include <Core/Task/AccessDerivation.h>
#include <Core/Task/TaskBuilder.h>
#include <Core/TypeList.h>

#include <string>
#include <string_view>
#include <type_traits>

namespace Maho
{
namespace Core
{

/// @brief Whether a type is a task declaration: it names itself, and it names the one method its overloads
///        are called through.
///
/// Both clauses are what make it USABLE: a name alone would be a key, not a unit of work.
template <typename TTaskType>
concept CTask = requires
{
	{ TTaskType::StaticName() } -> std::convertible_to<std::string_view>;
	{ TTaskType::MethodName() } -> std::convertible_to<std::string_view>;
};

/// @brief What one overload's SIGNATURE says about its run: the contexts it is handed, and that it returns
///        nothing.
///
/// An entry spells its contract as a function type (`void(FMyContext&)`) rather than as a list of context
/// types, because that is where the compiler can check it: the return type has to be `void` (a node's result
/// is never read), and there has to be at least one parameter (a task with no context would have nothing to
/// hand over). The runner hands every context as an LVALUE, so a parameter may be by value or by reference
/// -- the callee's parameter list is its own business. Spelling the reference (`void(FContext&)`) is the more
/// accurate description of the hand-over, and it is what `FContexts` then reports.
///
/// Two parameters and more are fine -- but the preprocessor reads a comma inside a macro argument as an
/// argument separator, so a multi-parameter signature must be written in PARENTHESES at the declaration
/// (`(int&, float)`, a parameter list) and is a plain type everywhere else (`TTaskSignature<void(int&, float)>`).
/// @tparam TSignature The declared signature.
template <typename TSignature>
struct TTaskSignature;

/// @brief The one case there is: a signature of N parameters, returning `void`.
/// @tparam TReturn The declared return type (must be `void`: a node's result is never read).
/// @tparam TParams The contexts, in the order the callee receives them.
template <typename TReturn, typename... TParams>
struct TTaskSignature<TReturn(TParams...)>
{
	/// @brief Whether the declaration is usable as an overload's contract.
	static_assert(std::is_void_v<TReturn>,
		"a task's signature must return void: a node's result is never read");

	/// @brief Whether there is anything to hand over at all.
	static_assert(sizeof...(TParams) > 0,
		"a task is given at least one context object: declare void(FContext&)");

	/// @brief The contexts a run of this overload is given, in the callee's order.
	using FContexts = TTypeList<TParams...>;

	/// @brief The same list, NORMALIZED: top-level references dropped. This is the form a call site's
	///        arguments deduce to (a function parameter of type `T&...` deduces `T`), so it is what the
	///        per-overload `Run` overload is constrained against -- a call whose shapes no entry declared does
	///        not resolve at all.
	using FNormalized = TTypeList<std::remove_reference_t<TParams>...>;
};

/// @brief The metadata axis a TASK's own protocol row is filed under (one row per task: its identity and the
///        method its overloads share). The per-overload rows are the access declarations' (`KTaskAccess`).
inline constexpr std::string_view KTaskProtocol = "Task";

/// @brief Files one task's protocol row. A registrar OBJECT at namespace scope, so a declaration becomes a row
///        without anyone calling anything (the same shape as the Engine's stage protocol row).
class FTaskProtocolRegistrar
{
public:
	/// @brief Files `(KTaskProtocol, Task, Method)`.
	/// @param Task The task's identity.
	/// @param Method The method its overloads are called through.
	FTaskProtocolRegistrar(std::string_view Task, std::string_view Method) noexcept
	{
		GetMetaData().Register(KTaskProtocol, Task, Method);
	}
};

/// @brief A task's IDENTITY TOKEN: the address of its protocol row. The same rule as
///        `DeclarationToken` (see `Core/Task/AccessDerivation.h`) -- the store is one object per process, so
///        its rows' addresses are too, which is what makes a token usable inside a key. A shape's token
///        already implies the task it belongs to; this one is for a policy that keys per task instead.
/// @tparam TTaskType The task declaration.
/// @return The token (never dereferenced, only compared and hashed).
template <typename TTaskType>
	requires CTask<TTaskType>
[[nodiscard]] const void* TaskToken() noexcept
{
	static const void* const Cached = GetMetaData().Find(KTaskProtocol, TTaskType::StaticName());
	return Cached;
}

/// @brief Strips the parentheses a shape's PARAMETER LIST is written in (see
///        `MAHO_DECLARE_TASK_METHOD_OVERRIDE`): the preprocessor only sees a comma-free argument when the
///        list is parenthesized, and the C++ parser must then see the parameters themselves, not the
///        parentheses around them.
///
/// Variadic on purpose -- the argument it is given is exactly a list of the method's parameters, so a
/// non-variadic form would be split by the very comma the parentheses are there to hide.
/// @param T The parenthesized parameter list.
#define MAHO_TASK_UNPAREN_IMPL(...) __VA_ARGS__
/// @brief `MAHO_TASK_UNPAREN_IMPL` applied to a parenthesized parameter list (the spelling callers write).
/// @param T The parenthesized parameter list (`(FContext&)`).
#define MAHO_TASK_UNPAREN(T) MAHO_TASK_UNPAREN_IMPL T

/// @brief Token pasting in two steps, so an argument is EXPANDED before it is pasted (the one-step form
///        would paste the argument's own text). This is how a task's name is derived from the method name:
///        `I` + `Tick`.
/// @param A The prefix (a bare token).
/// @param B The token to append (macro-expanded first).
#define MAHO_TASK_JOIN_IMPL(A, B) A##B
/// @brief `MAHO_TASK_JOIN_IMPL` applied to a pair of tokens (the spelling callers write).
/// @param A The prefix (a bare token).
/// @param B The token to append (macro-expanded first).
#define MAHO_TASK_JOIN(A, B) MAHO_TASK_JOIN_IMPL(A, B)

/// @brief Stringizes the EXPANDED form of an argument (the two-step form: the one-step one would stringize the
///        argument's own text, not what the macros turn it into).
/// @param X The tokens to stringize.
#define MAHO_TASK_STRINGIZE(X) MAHO_TASK_STRINGIZE_IMPL(X)

/// @brief The implementation half of `MAHO_TASK_STRINGIZE`.
/// @param X The expanded tokens.
#define MAHO_TASK_STRINGIZE_IMPL(X) #X

/// @brief The token a task is named by, derived from the METHOD it is declared for: `I` + the method --
///        `Tick` gives `ITick`, `Execute` gives `IExecute`.
///
/// The name is the interface's because the type IS the interface (each declared shape is one of its pure
/// virtuals). It is unqualified on purpose -- a namespace cannot be derived from a method name -- so a task,
/// its owners and its bindings are declared in ONE namespace: the layer's own (see the note at the top of
/// this file; the engine's stage interfaces live in `Maho`, a layer's tasks do not).
/// @param Method The method name the task is declared for.
#define MAHO_TASK_NAME(Method) MAHO_TASK_JOIN(I, Method)

/// @brief Opens a task declaration for a FUNCTION: the task's identity is derived from the method name
///        (`MAHO_BEGIN_DECLARE_TASK(Tick)` declares `ITick`, `Execute` gives `IExecute`), so declaring a task
///        reads as what it is -- the interface AND the metadata FOR that method.
///
/// The struct is left OPEN -- `MAHO_DECLARE_TASK_METHOD_OVERRIDE` adds one shape per parameter list and
/// `MAHO_END_DECLARE_TASK` closes it -- because a shape is a MEMBER, and that is what lets a declaration be
/// written inside a type at all (an explicit specialization cannot).
///
/// The type is ABSTRACT on purpose: every shape contributes a pure virtual, so an owner says "I implement
/// this task" by inheriting the type and overriding each shape it declared -- and a shape left out is a
/// compile error at the owner, not a silent no-op at the call.
///
/// The single place the method name is turned into a CALL is the private `Invoke` below: it is a pack, so any
/// declared shape can forward through it, and it is private, so the only way in is one of the `Run` overloads
/// the shapes declare.
/// @param Method The method an owner implements -- one name for the whole task, and the source of its identity.
#define MAHO_BEGIN_DECLARE_TASK(Method)                                                                  \
	struct MAHO_TASK_NAME(Method)                                                                        \
	{                                                                                                    \
		/** @brief The task's identity (a literal: stable, readable, RTTI-free -- and the same rule the     \
		 *  type's name comes from, so the two cannot drift). */                                          \
		[[nodiscard]] static constexpr std::string_view StaticName() noexcept                             \
		{ return MAHO_TASK_STRINGIZE(MAHO_TASK_NAME(Method)); }                                          \
                                                                                                         \
		/** @brief The method every overload of this task is called through: that ONE name is what makes \
		 *  them overloads of one task (and it is what the task's protocol row files). */                \
		[[nodiscard]] static constexpr std::string_view MethodName() noexcept { return #Method; }        \
                                                                                                         \
		/** @brief The task's OWN type, so a shape declared below can name the task it belongs to: the     \
		 *  method name is the ONE token that cannot be spelled from a signature, and a shape's identity   \
		 *  must not depend on it. */                                                                    \
		using FTask = MAHO_TASK_NAME(Method);                                                             \
                                                                                                         \
		/** @brief The interface's destructor: the type declares virtuals (one per shape, below), so it    \
		 *  needs one -- and an owner destroyed through this base is destroyed correctly. */             \
		virtual ~MAHO_TASK_NAME(Method)() = default;                                                      \
                                                                                                         \
		/** @brief ONE shape of this method, keyed by its SIGNATURE -- a shape's identity IS the signature,  \
		 *  so nothing about it is written twice: `FMethod<void(int&, float)>`. Declared here, specialized  \
		 *  in this same class by `MAHO_DECLARE_TASK_METHOD_OVERRIDE` (a member template, which is what    \
		 *  lets the specialization live inside the type). The primary is COMPLETE and answers `bShape ==  \
		 *  false`, which is what makes "is this one of MY shapes?" a question a concept can ask without    \
		 *  touching a type that was never declared. */                                                    \
		template <typename TSignature> struct FMethod { static constexpr bool bShape = false; };           \
                                                                                                         \
		/** @brief Files the task's own protocol row, once, when this type is instantiated (see            \
		 *  `KTaskProtocol`): a dump can say what the task IS without knowing any of its overloads. */    \
		inline static const ::Maho::Core::FTaskProtocolRegistrar Registration{ StaticName(), MethodName() }; \
                                                                                                         \
	private:                                                                                             \
		/** @brief Runs the method on an owner -- the ONE place the method name becomes a call. The entries'  \
		 *  `Run` overloads are the interface; this is what they forward to (any shape, one pack).         \
		 *  @param Owner The object implementing the overload.                                           \
		 *  @param Args The contexts of this run. */                                                     \
		template <typename TOwner, typename... TArgs>                                                   \
		static void Invoke(TOwner& Owner, TArgs&... Args) { Owner.Method(Args...); }                     \
                                                                                                         \
	public:

/// @brief Declares ONE shape of the method being declared: the pure virtual an owner overrides, the `Run`
///        overload that is the only way to reach it, and the shape type a builder names.
///
/// Nothing about the shape is named by hand: its identity is its SIGNATURE (`ITick::FMethod<void(int&, float)>`),
/// which the compiler composes from what is written, and it is simultaneously the contract (`FSignature` /
/// `FContexts`) and the handle a builder feeds the ledger with --
/// `Ledger.Record<ITick::FMethod<void(int&, float)>>(Key)`. Its access rows are keyed by
/// `ITick` + the signature, so two shapes (or two tasks) never share a key by accident.
///
/// The virtual is what makes the task an INTERFACE: an owner inherits the task type and overrides the shape,
/// so "may use this task" is a compiler check at the owner -- and a declared shape it leaves out keeps the
/// owner abstract, which is the error you want at the place the omission is.
///
/// The `Run` it declares is a pack CONSTRAINTED to this shape's context types: the parameter list stays the
/// compiler's (so the arguments are forwardable without the caller naming them), while the shape is fixed --
/// a call whose argument types no shape declared is not a viable overload at all, which is what makes the
/// declared set the interface.
/// @param Method The method the shape's virtual is named after (one macro carries no state, so this is the
///        one token the parameter list cannot imply).
/// @param Signature The method's PARAMETER LIST, ALWAYS in parentheses -- `(FContext&)`,
///        `(FContext&, float)`: the parentheses are what keeps a comma out of the macro's argument list
///        (see `TTaskSignature`), and the same rule then holds for one parameter and for several.
/// @note A shape declares NO accesses: what memory a node touches belongs to the IMPLEMENTATION, so it is the
///       BINDING (owner + task + shape) that declares it -- see `MAHO_DECLARE_TASK_BINDING`.
#define MAHO_DECLARE_TASK_METHOD_OVERRIDE(Method, Signature)                                             \
	/** @brief This shape, as the interface an owner implements: the declared parameter list, pure (an     \
	 *  owner that inherits the task must override every shape it declares, or it stays abstract). */      \
	virtual void Method(MAHO_TASK_UNPAREN(Signature)) = 0;                                                \
                                                                                                         \
	/** @brief Reaches this shape, and only this shape: the declared contexts, resolved per OWNER type.    \
	 *  @param Owner The object implementing it.                                                          \
	 *  @param Args The contexts of this run. */                                                          \
	template <typename TOwner, typename... TArgs>                                                        \
		requires std::is_same_v<::Maho::TTypeList<std::remove_reference_t<TArgs>...>,                    \
			typename ::Maho::Core::TTaskSignature<void (MAHO_TASK_UNPAREN(Signature))>::FNormalized>     \
	static void Run(TOwner& Owner, TArgs&... Args) { Invoke(Owner, Args...); }                            \
                                                                                                         \
	/** One shape, keyed by its SIGNATURE: the contract of a call, and the handle a builder names. (What   \
	 *  it touches is the binding's business -- two owners implementing this signature may touch           \
	 *  different memory.) */                                                                              \
	template <>                                                                                           \
	struct FMethod<void (MAHO_TASK_UNPAREN(Signature))>                                                   \
	{                                                                                                     \
		/** @brief Whether this is a declared shape (see the primary: it answers false). */                \
		static constexpr bool bShape = true;                                                              \
                                                                                                         \
		/** @brief The signature, as derived from the parameter list (the contract, readable in a dump). */ \
		using FSignature = void (MAHO_TASK_UNPAREN(Signature));                                          \
                                                                                                         \
		/** @brief The contexts a run of this shape is given (see `TTaskSignature`). */                    \
		using FContexts = typename ::Maho::Core::TTaskSignature<void (MAHO_TASK_UNPAREN(Signature))>::FContexts; \
	}

/// @brief Closes a task declaration, checking that it is the task the declaration was opened for. The
///        protocol row was already filed by the static member `MAHO_BEGIN_DECLARE_TASK` declares.
/// @param Method The method the task was opened for (named exactly as at `MAHO_BEGIN_DECLARE_TASK`).
#define MAHO_END_DECLARE_TASK(Method)                                                                    \
	};                                                                                                   \
	static_assert(MAHO_TASK_NAME(Method)::MethodName() == #Method,                                        \
		"the task closed here is not the one this declaration opened");

/// @brief Runs a task for an owner -- the spelling a builder uses when it holds the task as a template
///        parameter (it forwards to the declaration's `Run`, so the method name never appears twice).
/// @tparam TTaskType The task declaration.
/// @tparam TOwner The owner implementing the overload.
/// @tparam TArgs The contexts of this run.
/// @param Owner The owner.
/// @param Args The contexts.
template <typename TTaskType, typename TOwner, typename... TArgs>
	requires CTask<TTaskType>
void RunTask(TOwner& Owner, TArgs&... Args)
{
	TTaskType::Run(Owner, Args...);
}

}
}
