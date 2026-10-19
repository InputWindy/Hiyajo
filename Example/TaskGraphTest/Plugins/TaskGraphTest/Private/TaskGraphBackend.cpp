// The execution backend's own tests (`Core/Task/TaskGraph.h`) and the model built on it: the builder
// protocol, the task/memory-access vocabulary and the edge derivation. Run with MAHO_TEST_SCHED=1.
//
// The vocabulary under test is KEYS, not handles and not strings: the caller computes a 64-bit key from
// what it already has, names work by it, names other work to order against it, and gets nothing back.
#include <Core/Task/Access.h>
#include <Core/Task/AccessDerivation.h>
#include <Core/Task/Task.h>
#include <Core/Task/TaskBuilder.h>
#include <Core/Task/TaskDispatch.h>
#include <Core/Task/TaskGraph.h>
#include <Core/Task/ThreadPool.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

// B17: the task FOR a method -- `MAHO_BEGIN_DECLARE_TASK(Tick)` declares `ITick`, so nothing is named twice,
// and the declaration reads as what it is. Then one line per SHAPE, and the shape's identity is its
// SIGNATURE: the compiler composes `ITick::FMethod<void(int&, float)>` out of the parameter list written
// there, so there is no name to invent, keep unique, or get wrong. Each shape also declares the pure virtual
// an owner overrides -- that is what makes the task an INTERFACE: whoever inherits `ITick` implements every
// shape below, or stays abstract.
//
// Nothing here is a function pointer and nothing is erased: `ITick` is the interface, each `Run` is a function
// template resolved per OWNER type and constrained to ONE declared shape -- so the DECLARED shapes ARE the
// contract, and a call shaped like no shape is not viable at all. Erasure happens only at the graph's
// boundary, where a builder turns one of these into an `FTaskDesc::Body`.
//
// The name is `I` + the method, at whatever scope the declaration is written in. THIS test writes it at global
// scope, because the repo already has a `Maho::ITick` -- the ENGINE's stage interface (`Engine/Engine.h`) --
// and one name may only mean one thing per namespace. A layer that is not the engine may keep its task in its
// own namespace; its bindings must be declared in a namespace ENCLOSING `Maho::Core` (they are explicit
// specializations of it), which `Maho` and global scope both are.
MAHO_BEGIN_DECLARE_TASK(Tick)
	MAHO_DECLARE_TASK_METHOD_OVERRIDE(Tick, (int&));
	MAHO_DECLARE_TASK_METHOD_OVERRIDE(Tick, (int&, float));
	MAHO_DECLARE_TASK_METHOD_OVERRIDE(Tick, (int&, float&, double));
MAHO_END_DECLARE_TASK(Tick)

namespace Maho
{

/** A stand-in for an UPPER LAYER's own type -- a frame, in the real thing: it owns its vocabulary (which
 *  nodes a round has, what they wait for) and its own reusable buffers, and the graph knows none of it.
 *
 *  It lives in its own namespace because that is the shape the rule below is about: the TYPE is a layer's,
 *  the BUILDER is a Core specialization on it. */
namespace TaskGraphTest
{
	struct FFakeFrame
	{
		std::vector<Core::FTaskDesc> Descs;          // the instance's OWN storage: reused every round, so a
		std::vector<Core::FTaskKey>  Predecessors;   //  batch costs no allocation once these have grown
		std::vector<Core::FTaskKey>  Completed;
		std::atomic<int>*            Ran = nullptr;
		bool                         bMounted = true;
	};

	/** B16's regions: four tag types. A region is "a piece of memory", and its identity IS its type -- so a
	 *  typo is a compile error, and two same-named types in different namespaces are two regions. */
	struct FB16BufferRegion {};
	struct FB16ChannelRegion {};
	struct FB16InputRegion {};
	struct FB16OutputRegion {};
}

// A region's readable name (the identity is the type itself; this is what a dump prints). It has to come
// BEFORE any declaration that USES the region: `MAHO_MEMORY_ACCESS_WRITE(FB16Buffer)` is checked against the name
// the moment it is written, so a name declared later is a constraint failure, not a late binding.
MAHO_DECLARE_REGION(TaskGraphTest::FB16BufferRegion);
MAHO_DECLARE_REGION(TaskGraphTest::FB16ChannelRegion);
MAHO_DECLARE_REGION(TaskGraphTest::FB16InputRegion);
MAHO_DECLARE_REGION(TaskGraphTest::FB16OutputRegion);

namespace TaskGraphTest
{
	// The declarations: what each work item touches, as TYPE LISTS (composed by the compiler). Each one IS a
	// type -- the macro DEFINES it, which is why the declarations live in this namespace (a namespace member
	// cannot be defined from the outside) and why nothing is forward-declared here.
	MAHO_DECLARE_ACCESSES(B16_Write, MAHO_MEMORY_ACCESS_WRITE(FB16Buffer));
	MAHO_DECLARE_ACCESSES(B16_Read, MAHO_MEMORY_ACCESS_READ(FB16Buffer));
	MAHO_DECLARE_ACCESSES(B16_Destroy, MAHO_MEMORY_ACCESS_WRITE(FB16Buffer));
	MAHO_DECLARE_ACCESSES(B16_ChannelWrite, MAHO_MEMORY_ACCESS_WRITE(FB16Channel));

	// A HELPER (a plain function) and a CALLER that pulls it in: the caller's list now carries the helper's
	// write, and a declaration that was never made does not compile.
	MAHO_DECLARE_ACCESSES(B16_Helper, MAHO_MEMORY_ACCESS_WRITE(FB16Output));
	MAHO_DECLARE_ACCESSES(B16_Caller,
		MAHO_MEMORY_ACCESS_READ(FB16Input),
		MAHO_ACCESS_INCLUDE(B16_Helper));

	// A DIAMOND: two paths to the same declaration. Reaching one access twice must stay harmless.
	MAHO_DECLARE_ACCESSES(B16_DiamondLeft, MAHO_ACCESS_INCLUDE(B16_Helper));
	MAHO_DECLARE_ACCESSES(B16_Diamond,
		MAHO_ACCESS_INCLUDE(B16_DiamondLeft),
		MAHO_ACCESS_INCLUDE(B16_Helper));

	// A declaration that touches NOTHING: the shape `MAHO_ACCESS_NONE` exists for (an item that is a unit of
	// work but has no ordering to derive).
	MAHO_DECLARE_ACCESSES(B16_Nothing, MAHO_ACCESS_NONE);

	// B17: the METHOD's accesses first (a task is a function before it is a task)...
	MAHO_DECLARE_ACCESSES(B17_Tick_Execute, MAHO_MEMORY_ACCESS_WRITE(FB16Buffer));
}

// B24: a PROPERTY (storage private, Get/Set only) and its declaration. Its region is the nested tag the macro
// generates (`NameRegion : FRegionTag`), so there is no name to write by hand -- and the declaration below
// names that tag, which is exactly what the accessors report.
// A global piece of memory, declared as a BINDING of its own: a region (per-instance, marked) + a property, so
// the only way in is Get()/Set() and every access is observed.
MAHO_GLOBAL_PROPERTY(B29_Counter, int);
MAHO_DECLARE_ACCESSES(B29_Counter_Write, MAHO_MEMORY_ACCESS_WRITE(B29_Counter));

struct B24_PropertyHolder
{
	MAHO_PROPERTY(int, Position);
};
MAHO_DECLARE_ACCESSES(B24_PropertyWrite,
	MAHO_MEMORY_ACCESS_WRITE(B24_PropertyHolder::Position));

// B18-B23: the owners those blocks run nodes on, and the bindings that declare what each of them touches.
// The bindings are declared HERE, at `Maho` scope, because they are explicit specializations of
// `Maho::Core::TBindingAccess` -- and a specialization may only live in a namespace that ENCLOSES the
// template's (global scope and `Maho` both qualify; a leaf namespace like `TaskGraphTest` does not).
namespace TaskGraphTest
{
	/** One counter, one shape: the ordinary owner of those blocks. It IMPLEMENTS `ITick` -- which the
	 *  binding declaration requires, and which an interface costs: every declared shape is overridden here
	 *  (the two this owner never runs included), so what it MAY be asked to run is the task's, not a habit. */
	struct FIntTickOwner : ITick
	{
		int Ran = 0;
		void Tick(int& Delta) override { Ran += Delta; }
		void Tick(int&, float) override {}
		void Tick(int&, float&, double) override {}
	};

	/** A SECOND owner type with the SAME shape -- which is why the access declaration belongs to the binding:
	 *  the two types may touch completely different memory. */
	struct FOtherTickOwner : ITick
	{
		int Ran = 0;
		void Tick(int& Delta) override { Ran += Delta; }
		void Tick(int&, float) override {}
		void Tick(int&, float&, double) override {}
	};
}

MAHO_DECLARE_TASK_BINDING(FIntTickNode, TaskGraphTest::FIntTickOwner, Tick, (int&),
	MAHO_MEMORY_ACCESS_WRITE(TaskGraphTest::FB16Buffer));
MAHO_DECLARE_TASK_BINDING(FOtherTickNode, TaskGraphTest::FOtherTickOwner, Tick, (int&),
	MAHO_MEMORY_ACCESS_WRITE(TaskGraphTest::FB16Buffer));

// ═══════════════════════════════════════════════════════════════════════════════════════════════════════
// B30: THE SHAPE A MAHO DEVELOPER WRITES -- application code, end to end:
//
//   (1) DECLARE THE INTERFACE      one line per call shape; the type `ISwarm` IS the contract
//   (2) IMPLEMENT IT ON A CLASS    `: ISwarm` = "I can run this", `override` = the compiler checking it
//   (3) DECLARE WHAT IT TOUCHES    the BINDING: this implementation, this shape, these pieces of memory
//   (4) RUN IT                     record into a buffer, submit it at a frame -- no key, no edge, no thread
//
// Note what is ABSENT: no `MAHO_DECLARE_REGION` (the memory is a property, which carries its own region), and
// no declaration for `Drag` at all (a plain member is not monitored). `MAHO_DECLARE_REGION` is only for
// memory that no object owns (a raw buffer, an arena, a resource handle).
// ═══════════════════════════════════════════════════════════════════════════════════════════════════════

// (1) the interface for the method `Swarm`: `I` + the method, one pure virtual per declared shape.
// Declared at global scope only because this repo's engine already owns `Maho::ITick`-style stage names;
// a layer's own tasks normally live in the layer's namespace (the binding must sit in `Maho` or global).
MAHO_BEGIN_DECLARE_TASK(Swarm)
	MAHO_DECLARE_TASK_METHOD_OVERRIDE(Swarm, (float));
	MAHO_DECLARE_TASK_METHOD_OVERRIDE(Swarm, (float, int));
MAHO_END_DECLARE_TASK(Swarm)

namespace DemoLayer
{
	/** (2) A class that implements the task. Inheriting is the statement; `override` per shape is what makes
	 *  a missing one an abstract class rather than a node that quietly does nothing.
	 *
	 *  Its memory is PROPERTIES: the value is private, `Get()`/`Set()` are the only way in (and the only way
	 *  an access can be OBSERVED), and each property already carries the metadata a binding names -- which is
	 *  why nothing below declares a region. */
	struct FLight : ISwarm
	{
		MAHO_PROPERTY(float, Position);
		MAHO_PROPERTY(float, Velocity);

		/** A PLAIN member -- and, by the project's convention, NOT monitored: only a property carries a region,
		 *  so a plain member is invisible to ordering (no declaration can name it). State that must be ordered
		 *  against is a property; everything else is private business of the object. */
		float Drag = 1.0f;

		int Steps = 0;

		void Swarm(float Dt) override
		{
			Position.Set() += Velocity.Get() * Dt * Drag;
			++Steps;
		}

		/** The second declared shape: implementing it is part of inheriting the interface, whether or not any
		 *  binding runs it. */
		void Swarm(float Dt, int Times) override
		{
			for (int Index = 0; Index < Times; ++Index)
			{
				Swarm(Dt);
			}
		}
	};
}

// (3) the BINDING: what THIS implementation touches on THAT shape -- write the writes, read the reads.
// It also NAMES the node kind (`FLightSwarm`), so the recorder has nothing else to spell.
// (A second owner of the same signature may touch completely different memory; it would declare its own.)
//
// Only PROPERTIES appear here, which is the convention this project runs on: a property is a region (its tag
// is what its Get()/Set() report), so it can be declared, observed and ordered; a plain member like `Drag`
// has no region at all and is therefore outside the conflict model -- if it must be ordered against, make it
// a property. (`MAHO_DECLARE_REGION` remains for memory no object owns.)
MAHO_DECLARE_TASK_BINDING(FLightSwarm, DemoLayer::FLight, Swarm, (float),
	MAHO_MEMORY_ACCESS_WRITE(DemoLayer::FLight::Position),
	MAHO_MEMORY_ACCESS_READ(DemoLayer::FLight::Velocity));


namespace
{
	// The backend's vocabulary lives in its own namespace (see Core/Task/TaskGraph.h); this test uses it so
	// often that naming it once is enough.
	using Core::FTaskDesc;
	using Core::FTaskGraph;
	using Core::FTaskKey;

	int GBackendFailures = 0;

	void Check(const char* What, bool bOk)
	{
		std::printf("  %s %s\n", bOk ? "ok  " : "FAIL", What);
		if (!bOk)
		{
			++GBackendFailures;
		}
		std::fflush(stdout);
	}

	/** How THIS caller fills a node description -- its own business, so it hides the configuration
	 *  difference (the diagnostic name only exists where diagnostics do). The header deliberately offers
	 *  no such helper: how a caller builds its nodes is not the graph's vocabulary. */
	[[nodiscard]] FTaskDesc Desc(FTaskKey Key, std::string_view DebugName, std::function<void()> Body)
	{
		FTaskDesc Out;
		Out.Key = Key;
#if MAHO_DO_CHECK
		Out.DebugKey = DebugName;
#else
		(void)DebugName;
#endif
		Out.Body = std::move(Body);
		return Out;
	}

	/** The TEST's key policy -- the caller's business, not the graph's. This caller happens to name nodes
	 *  by string, so it hashes the name (FNV-1a) and folds in whatever makes this node distinct. A caller
	 *  keying by a slot or an index would compute keys another way entirely; the graph sees only a number,
	 *  which is why nothing here is stored: `KeyOf` re-derives it on demand. */
	[[nodiscard]] FTaskKey KeyOf(std::string_view Name)
	{
		FTaskKey Hash = 0xcbf29ce484222325ull;
		for (const char Character : Name)
		{
			Hash ^= static_cast<FTaskKey>(static_cast<unsigned char>(Character));
			Hash *= 0x100000001b3ull;
		}
		return Hash;
	}

	[[nodiscard]] FTaskKey KeyOf(std::string_view Name, std::uint64_t Round)
	{
		const FTaskKey Key = KeyOf(Name);
		// The same work, another time: the repetition's identity goes INTO the key.
		return Key ^ (Round + 0x9e3779b97f4a7c15ull + (Key << 6) + (Key >> 2));
	}

}

/// How a FAKE FRAME becomes nodes -- and, more importantly, WHERE this kinds of thing goes: the
/// specialization belongs to the namespace the TEMPLATE is in (`Maho::Core`), not to the one the type is
/// in, even though it is the layer that knows what the type means.
///
/// The body is what a frame's builder will do: name the round's work (the round is IN every key, so it can
/// never collide with an older node), say what it waits for, and hand back VIEWS into its own buffers.
namespace Core
{
	/// The specialization's key is the type AND the argument types (that is the pair `Submit` looks up), so
	/// it lists them explicitly -- which is also why the extension normalizes them with `decay_t`: a caller
	/// passing `Round&` and one passing `Round` must land on the SAME builder.
	template <>
	struct TTaskBuilder<TaskGraphTest::FFakeFrame, std::uint64_t>
	{
		static bool Build(FTaskBatch& Out, TaskGraphTest::FFakeFrame& Frame, std::uint64_t Round)
		{
			if (!Frame.bMounted)
			{
				return false;                     // nothing to submit is an ordinary answer, not an error
			}

			Frame.Descs.clear();
			Frame.Predecessors.clear();
			Frame.Completed.clear();

			const std::string_view Stages[] = { "Pre", "Sim", "Post" };
			for (std::string_view Stage : Stages)
			{
				Frame.Descs.push_back(Desc(KeyOf(Stage, Round), Stage,
					[&Frame]() { Frame.Ran->fetch_add(1); }));
			}

			if (Round > 1)
			{
				// Every stage of this round waits for the LAST stage of the previous one: the cross-round
				// edge is declared by the layer, and the round in the key keeps it unambiguous.
				Frame.Predecessors.push_back(KeyOf("Post", Round - 1));
			}

			Out.Descs = Frame.Descs;
			Out.Predecessors = Frame.Predecessors;
			Out.Completed = Frame.Completed;
			return true;
		}
	};
}

namespace
{
	/** Waits until `Count` threads have arrived, or times out (so a serial execution FAILS instead of
	 *  hanging): the backend's "these two really ran at the same time" oracle. */
	struct FBarrier
	{
		explicit FBarrier(int InCount) : Count(InCount) {}

		bool Arrive(std::chrono::milliseconds Timeout)
		{
			std::unique_lock Lock(Mutex);
			++Arrived;
			if (Arrived == Count)
			{
				CondVar.notify_all();
				return true;
			}
			return CondVar.wait_for(Lock, Timeout, [this] { return Arrived >= Count; });
		}

		int                     Count = 0;
		int                     Arrived = 0;
		std::mutex              Mutex;
		std::condition_variable CondVar;
	};

	/** A latch the TEST opens: it holds a node inside the graph while the test inspects what the graph
	 *  does with that node still in flight. */
	struct FGate
	{
		void Open()
		{
			{
				std::lock_guard Lock(Mutex);
				bOpen = true;
			}
			CondVar.notify_all();
		}

		void WaitOpen()
		{
			std::unique_lock Lock(Mutex);
			// A GENEROUS cap: this is only a hang-breaker, not part of any oracle -- if it expired while the
			// test relied on the gate being closed, it would free the holder and invert the observation.
			CondVar.wait_for(Lock, std::chrono::seconds(60), [this] { return bOpen; });
		}

		std::mutex              Mutex;
		std::condition_variable CondVar;
		bool                    bOpen = false;
	};

	/** A stress probe for the POOL's locking, OFF by default (`MAHO_TEST_STRESS=1` turns it on).
	 *
	 *  Why it exists: the suite saw two shapes of one symptom at high concurrency -- a freshly created
	 *  graph's first `Submit` blocking, and a worker that never came free -- and both fit a single
	 *  explanation: someone holds `FThreadPool`'s ONE mutex and does not return. Every operation this
	 *  reproducer performs goes through that mutex, so it exercises exactly the suspects:
	 *
	 *    * `CreatePersistentLane` / `DestroyPersistentLane` (a lane slot per round, released per round),
	 *    * `Submit` from the CALLER (an idle caller enqueuing into a dead lane),
	 *    * `Submit` from a WORKER (a node body enqueuing its own child -- the completion path's shape),
	 *    * `Flush` (what every `Wait` does first: lock + CV).
	 *
	 *  It is single-threaded ON PURPOSE: if this hangs, the pool's own lock/lane lifecycle is at fault, and
	 *  nothing about the test's concurrency or the graph's scheduling can be blamed. Progress is printed
	 *  every `PrintEvery` rounds, with the phase named before each step, so a hang reports where it died.
	 *  @param Rounds How many whole graph lifetimes to drive.
	 */
	void RunPoolStress(int Rounds)
	{
		std::printf("[stress] %d rounds of graph lifecycles on one pool\n", Rounds);
		std::fflush(stdout);

		std::atomic<int> Ran{ 0 };
		FThreadPool Pool(4);

		for (int Round = 0; Round < Rounds; ++Round)
		{
			if ((Round % 50) == 0)
			{
				std::printf("[stress] round %d (nodes ran: %d)\n", Round, Ran.load());
				std::fflush(stdout);
			}

			{
				FTaskGraph Graph(Pool);
				if (!Graph.Initialize())
				{
					std::printf("[stress] Initialize() failed at round %d\n", Round);
					std::fflush(stdout);
					return;
				}

				const FTaskKey Parent = KeyOf("Stress.Parent", static_cast<std::uint64_t>(Round));
				const FTaskKey Child = KeyOf("Stress.Child", static_cast<std::uint64_t>(Round));

				// The body submits its own child: this is the WORKER-side posting path (a worker takes the
				// pool's lock to enqueue its completion, and here it takes it to enqueue real work).
				Graph.Submit(Desc(Parent, "Stress.Parent", [&Graph, Child, &Ran, Round]()
					{
						Graph.Submit(Desc(Child, "Stress.Child", [&Ran]() { Ran.fetch_add(1); }));
						Ran.fetch_add(1);
						(void)Round;
					}));

				const FTaskKey Keys[] = { Parent, Child };
				Graph.Wait(Keys);          // Flush + resolve + fence: the caller's whole wait path
				Graph.Wait(Keys);
			}   // ~FTaskGraph: Wait + Flush + DestroyPersistentLane (the lane slot comes back)

			if (Ran.load() < (Round + 1))
			{
				std::printf("[stress] round %d saw only %d nodes -- a submission was lost\n", Round, Ran.load());
				std::fflush(stdout);
				return;
			}
		}

		std::printf("[stress] done: %d rounds, %d nodes ran\n", Rounds, Ran.load());
		std::fflush(stdout);
	}

	/** The CONCURRENT half of the probe (`MAHO_TEST_STRESS=2`): several threads, each driving its own graph
	 *  lifecycle, on ONE pool. That is the shape nothing in the suite covers -- every other test creates a
	 *  second graph from a single thread -- and it is the shape the two observed symptoms came from: a lane
	 *  being created and released while other threads submit, flush and finish work.
	 *  @param Threads How many callers drive lifecycles at once.
	 *  @param RoundsPerCaller How many whole lifecycles each of them drives.
	 */
	void RunPoolStressConcurrent(int Threads, int RoundsPerCaller)
	{
		std::printf("[stress] %d callers x %d graph lifecycles, concurrent\n", Threads, RoundsPerCaller);
		std::fflush(stdout);

		std::atomic<int> Ran{ 0 };
		std::atomic<int> GraphsUp{ 0 };
		std::atomic<int> GraphsDown{ 0 };
		FThreadPool Pool(4);

		std::vector<std::thread> Callers;
		for (int Caller = 0; Caller < Threads; ++Caller)
		{
			Callers.emplace_back([&Pool, &Ran, &GraphsUp, &GraphsDown, Caller, RoundsPerCaller]()
			{
				for (int Round = 0; Round < RoundsPerCaller; ++Round)
				{
					{
						FTaskGraph Graph(Pool);
						if (!Graph.Initialize())
						{
							std::printf("[stress] caller %d: Initialize() failed\n", Caller);
							std::fflush(stdout);
							return;
						}
						GraphsUp.fetch_add(1);

						const std::uint64_t Which = static_cast<std::uint64_t>(Caller) * 100000
							+ static_cast<std::uint64_t>(Round);
						const FTaskKey Parent = KeyOf("StressC.Parent", Which);
						const FTaskKey Child = KeyOf("StressC.Child", Which);

						Graph.Submit(Desc(Parent, "StressC.Parent", [&Graph, Child, &Ran]()
							{
								Graph.Submit(Desc(Child, "StressC.Child", [&Ran]() { Ran.fetch_add(1); }));
								Ran.fetch_add(1);
							}));

						const FTaskKey Keys[] = { Parent, Child };
						Graph.Wait(Keys);
						Graph.Wait(Keys);
					}
					GraphsDown.fetch_add(1);
				}
			});
		}
		for (std::thread& Caller : Callers)
		{
			Caller.join();
		}

		// Every caller saw every one of its nodes: the child was submitted from INSIDE a body, so this
		// catches a lost worker-side submission too.
		const int Expected = Threads * RoundsPerCaller * 2;
		std::printf("[stress] done: graphs up=%d down=%d, nodes ran=%d (expected %d)\n",
			GraphsUp.load(), GraphsDown.load(), Ran.load(), Expected);
		std::fflush(stdout);
	}
}

int RunTaskGraphBackendTest()
{
	using Core::FDerivationResult;
	using Core::CTaskRunnable;
	using Core::CTaskShape;
	using Core::FMemoryAccessLedger;
	using Core::ForEachDeclaredAccess;
	using Core::FTaskDesc;
	using Core::FTaskGraph;
	using Core::FTaskKey;
	using Core::FInstanceId;
	using Core::FTaskSystem;
	using Core::FTaskBuffer;
	using Core::FTaskDispatcher;
	using Core::RunTask;
	using Core::TTaskBinding;
	using TaskGraphTest::FFakeFrame;

	// The pool-locking probe (see `RunPoolStress`): off unless asked for, and BEFORE the suite, so a hang in
	// it cannot be confused with a hang in a test. `MAHO_TEST_STRESS=1` is the single-threaded loop,
	// `=2` the concurrent one, `=3` both.
	if (const char* const Stress = std::getenv("MAHO_TEST_STRESS"); Stress != nullptr)
	{
		const int Mode = std::atoi(Stress);
		if (Mode == 1 || Mode == 3)
		{
			RunPoolStress(2000);
		}
		if (Mode == 2 || Mode == 3)
		{
			RunPoolStressConcurrent(4, 300);
		}
	}

	std::printf("[tg] FTaskGraph backend test\n");

	FThreadPool Pool(4);
	{
		FTaskGraph Graph(Pool);
		Graph.Initialize();

		// B1: an edge orders the two nodes; the fence opens only after both.
		{
			std::vector<std::string> Order;
			std::mutex OrderMutex;

			const FTaskKey Head = KeyOf("B1.Head");
			Graph.Submit(Desc(Head, "B1.Head", [&]()
				{
					std::lock_guard Lock(OrderMutex);
					Order.push_back("head");
				}));

			const FTaskKey HeadPreds[] = { Head };
			Graph.Submit(Desc(KeyOf("B1.Tail"), "B1.Tail", [&]()
				{
					std::lock_guard Lock(OrderMutex);
					Order.push_back("tail");
				}), HeadPreds);

			Graph.Wait();
			Check("B1  the waited-for node ran first", Order == std::vector<std::string>{ "head", "tail" });
		}

		// B2: two independent nodes really run AT THE SAME TIME. The oracle is the barrier's TIMEOUT: a
		// serial execution makes the first body wait out the timeout (its predicate never comes true),
		// and the count then misses it. Requiring the count -- not "somebody arrived" -- is the whole
		// point: the LAST arriver of a serial run also returns true, so a boolean here would pass either
		// way.
		{
			FBarrier Barrier(2);
			std::atomic<int> Overlapped{ 0 };
			auto Body = [&]()
			{
				if (Barrier.Arrive(std::chrono::milliseconds(2000)))
				{
					Overlapped.fetch_add(1);
				}
			};
			Graph.Submit(Desc(KeyOf("B2.Left"), "B2.Left", Body));
			Graph.Submit(Desc(KeyOf("B2.Right"), "B2.Right", Body));
			Graph.Wait();
			Check("B2  two independent nodes ran concurrently", Overlapped.load() == 2);
		}

		// B2b: the same oracle at the POOL'S WIDTH -- every worker holds a body at once. (Uses the same
		// pool the graph was built on: 4.) Serial execution can only ever produce ONE true here.
		{
			constexpr int Width = 4;
			FBarrier Barrier(Width);
			std::atomic<int> Overlapped{ 0 };
			for (int I = 0; I < Width; ++I)
			{
				Graph.Submit(Desc(KeyOf("B2.Wide", static_cast<std::uint64_t>(I)), "B2.Wide", [&]()
					{
						if (Barrier.Arrive(std::chrono::milliseconds(2000)))
						{
							Overlapped.fetch_add(1);
						}
					}));
			}
			Graph.Wait();
			Check("B2b a full-width batch all ran at the same time", Overlapped.load() == Width);
		}

		// B3: a node waits for ALL of the keys it names, and only for those.
		{
			std::atomic<int> Done{ 0 };
			std::atomic<bool> bAfterBoth{ false };
			Graph.Submit(Desc(KeyOf("B3.First"), "B3.First", [&]() { Done.fetch_add(1); }));
			Graph.Submit(Desc(KeyOf("B3.Second"), "B3.Second", [&]() { Done.fetch_add(1); }));

			const FTaskKey BothPreds[] = { KeyOf("B3.First"), KeyOf("B3.Second") };
			Graph.Submit(Desc(KeyOf("B3.Last"), "B3.Last", [&]() { bAfterBoth.store(Done.load() == 2); }),
				BothPreds);
			Graph.Wait();
			Check("B3  the node with two predecessors ran after both", bAfterBoth.load());
			Check("B3  all three ran", Done.load() == 2);
		}

		// B4: naming a key that is NOT in the graph is SKIPPED (silently) -- its node completed, so the
		// constraint is already satisfied -- and a description whose key was never computed (it still
		// carries INDEX_NONE) is refused.
		//
		// The OTHER half of the key contract -- a key that is still in flight -- blocks the submission on
		// the spot (`ReportFatal`), which is why it has no check here: an abort cannot be asserted in the
		// same process as the rest of the suite.
		{
			std::atomic<bool> bRan{ false };
			const FTaskKey NeverPreds[] = { KeyOf("B4.NeverExisted") };
			Graph.Submit(Desc(KeyOf("B4.Node"), "B4.Node", [&]() { bRan.store(true); }), NeverPreds);
			Graph.Wait();
			Check("B4  a predecessor that is not in the graph is skipped, not obeyed", bRan.load());

			std::atomic<int> Sentinels{ 0 };
			FTaskDesc NoKey;
			NoKey.Body = [&Sentinels]() { Sentinels.fetch_add(1); };
			Graph.Submit(NoKey);
			Graph.Wait();
			Check("B4  a description left at INDEX_NONE is refused", Sentinels.load() == 0);
		}

		// B5: a key is RECOMPUTABLE, not a token -- the caller gets nothing back, so the same key must work
		// again once its previous occupancy has completed. And naming a completed node's key is a no-op
		// edge: the node it names has already run.
		{
			std::atomic<int> Runs{ 0 };
			const FTaskKey Recur = KeyOf("B5.Recur");
			const FTaskKey RecurKeys[] = { Recur };

			Graph.Submit(Desc(Recur, "B5.Recur", [&]() { Runs.fetch_add(1); }));
			Graph.Wait(RecurKeys);
			Check("B5  the node ran", Runs.load() == 1);

			Graph.Submit(Desc(Recur, "B5.Recur", [&]() { Runs.fetch_add(1); }));
			Graph.Wait(RecurKeys);
			Check("B5  the same key works again after its previous occupancy completed", Runs.load() == 2);

			Graph.Submit(Desc(Recur, "B5.Recur", [&]() { Runs.fetch_add(1); }));
			Graph.Wait();

			std::atomic<bool> bNextRan{ false };
			Graph.Submit(Desc(KeyOf("B5.Next"), "B5.Next", [&]() { bNextRan.store(true); }),
				RecurKeys);                                  // completed by now: skipped
			Graph.Wait();
			Check("B5  a completed predecessor is skipped, and the node still runs",
				bNextRan.load() && Runs.load() == 3);
		}

		// B6: the CALLERS are parallel too -- several threads submitting at once, each with keys of its own.
		// (The keys here are computed WITHOUT a string: the key is a number the caller mixes, and this caller
		// only has indices, which is exactly the case the key contract exists to allow.)
		{
			constexpr int CallerCount = 4;
			constexpr int PerCaller = 8;
			std::atomic<int> Ran{ 0 };
			std::vector<std::thread> Callers;
			for (int Caller = 0; Caller < CallerCount; ++Caller)
			{
				Callers.emplace_back([&Graph, &Ran, Caller]()
				{
					for (int I = 0; I < PerCaller; ++I)
					{
						const std::uint64_t Which = static_cast<std::uint64_t>(Caller * PerCaller + I);
						Graph.Submit(Desc(KeyOf("B6.Submitted", Which), "B6.Submitted",
							[&Ran]() { Ran.fetch_add(1); }));
					}
				});
			}
			for (std::thread& Caller : Callers)
			{
				Caller.join();
			}

			Graph.Wait();
			Check("B6  nodes submitted by several threads at once all ran",
				Ran.load() == CallerCount * PerCaller);
		}

		// B7: a throwing node still COMPLETES -- what waited for it runs, and waiting on its key returns.
		{
			std::atomic<bool> bWaiterRan{ false };
			const FTaskKey Thrower = KeyOf("B7.Thrower");
			const FTaskKey ThrowerKeys[] = { Thrower };

			Graph.Submit(Desc(Thrower, "B7.Thrower",
				[]() { throw std::runtime_error("B7 on purpose"); }));
			Graph.Submit(Desc(KeyOf("B7.Waiter"), "B7.Waiter",
				[&]() { bWaiterRan.store(true); }), ThrowerKeys);

			Graph.Wait(ThrowerKeys);
			Check("B7  waiting on a throwing node's key returns (it counts as completed)", true);
			Graph.Wait();
			Check("B7  what waited for it still ran", bWaiterRan.load());
		}

		// B8: `Wait` is a fence over everything submitted so far, and submitting again afterwards works
		// (the graph keeps running, it is not drained).
		{
			std::atomic<int> Ran{ 0 };
			for (int I = 0; I < 8; ++I)
			{
				Graph.Submit(Desc(KeyOf("B8.Batch", static_cast<std::uint64_t>(I)),
					"B8.Batch", [&]() { Ran.fetch_add(1); }));
			}
			Graph.Wait();
			Check("B8  a fence covered the whole batch", Ran.load() == 8);

			Graph.Submit(Desc(KeyOf("B8.More"), "B8.More", [&]() { Ran.fetch_add(1); }));
			Graph.Wait();
			Check("B8  the graph accepted more work after the fence", Ran.load() == 9);
		}

		// B9: `Wait` takes KEYS -- it blocks the caller until those nodes have completed, and only then
		// does the caller hand the next submission over. A completed (or unknown) key does not block.
		{
			std::atomic<bool> bSlowDone{ false };
			const FTaskKey Slow = KeyOf("B9.Slow");
			const FTaskKey SlowKeys[] = { Slow };

			Graph.Submit(Desc(Slow, "B9.Slow", [&]()
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(120));
					bSlowDone.store(true);
				}));

			Graph.Wait(SlowKeys);
			Check("B9  Wait(keys) made the caller wait for that node", bSlowDone.load());

			Graph.Wait(SlowKeys);
			Check("B9  waiting on a completed key returns immediately", bSlowDone.load());

			const FTaskKey UnknownKeys[] = { KeyOf("B9.NeverExisted") };
			Graph.Wait(UnknownKeys);
			Check("B9  an unknown key does not hang the caller", true);
		}

		// B10: the batch form is sugar over `Wait` + `Submit`: waitlist first, then one submission per node,
		// all sharing the same predecessors.
		{
			std::atomic<bool> bGateDone{ false };
			const FTaskKey Gate = KeyOf("B10.Gate");
			const FTaskKey GateKeys[] = { Gate };
			Graph.Submit(Desc(Gate, "B10.Gate", [&]()
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(60));
					bGateDone.store(true);
				}));

			std::atomic<int> Ran{ 0 };
			std::vector<FTaskDesc> Nodes;
			for (int I = 0; I < 4; ++I)
			{
				Nodes.push_back(Desc(KeyOf("B10.Node", static_cast<std::uint64_t>(I)), "B10.Node",
					[&]() { Ran.fetch_add(1); }));
			}
			Graph.Submit(std::span<const FTaskDesc>{ Nodes }, std::span<const FTaskKey>{}, GateKeys);

			Check("B10 the batch waited for its waitlist before submitting", bGateDone.load());
			Graph.Wait();
			Check("B10 every node of the batch ran", Ran.load() == 4);
		}

		// B12: the EXTENSION form -- an upper layer implements `TTaskBuilder` for its OWN type, and the graph
		// submits whatever it produced without ever learning what that type was.
		{
			std::atomic<int> Ran{ 0 };
			FFakeFrame Frame;
			Frame.Ran = &Ran;

			Frame.bMounted = false;
			Check("B12 a builder with nothing to submit answers false",
				!Graph.Submit(Frame, std::uint64_t{ 1 }));
			Graph.Wait();
			Check("B12 ... and nothing ran for it", Ran.load() == 0);

			Frame.bMounted = true;
			Check("B12 round 1 accepted", Graph.Submit(Frame, std::uint64_t{ 1 }));
			Graph.Wait();
			Check("B12 the round's three stages ran", Ran.load() == 3);

			Check("B12 round 2 accepted", Graph.Submit(Frame, std::uint64_t{ 2 }));
			Graph.Wait();
			Check("B12 and the same instance submitted again", Ran.load() == 6);
			Check("B12 the builder's own buffers were REUSED, not grown", Frame.Descs.size() == 3);
		}

		// B13: the CALLERS are parallel too -- `Submit` and `Wait` must be safe from many threads at once.
		// Each caller owns its own key slice (nothing is shared between them) and interleaves waits with
		// submissions, so the graph is posted to while somebody else is flushing its lane and waiting.
		// Every node gets its OWN slot counter, so the final fence shows not just "96 ran" but WHICH one
		// went missing or ran twice: a lost submission shows up as a slot at 0, a mangled one as a slot > 1.
		{
			constexpr int Callers = 4;
			constexpr int Rounds = 3;
			constexpr int PerRound = 8;
			constexpr std::size_t NodeCount = static_cast<std::size_t>(Callers) * Rounds * PerRound;

			std::vector<std::atomic<int>> Runs(NodeCount);
			for (std::atomic<int>& Slot : Runs)
			{
				Slot.store(0);
			}

			std::vector<std::thread> Submitters;
			for (int Caller = 0; Caller < Callers; ++Caller)
			{
				Submitters.emplace_back([&Graph, &Runs, Caller]()
				{
					std::vector<FTaskKey> Mine;                 // this caller's own keys, its own storage
					Mine.reserve(static_cast<std::size_t>(Rounds) * PerRound);
					for (int Round = 0; Round < Rounds; ++Round)
					{
						for (int I = 0; I < PerRound; ++I)
						{
							const std::size_t Which = static_cast<std::size_t>(Round)
								* (Callers * PerRound) + static_cast<std::size_t>(Caller) * PerRound
								+ static_cast<std::size_t>(I);
							const FTaskKey Key = KeyOf("B13.Node", static_cast<std::uint64_t>(Which));
							Mine.push_back(Key);
							Graph.Submit(Desc(Key, "B13.Node",
								[&Runs, Which]() { Runs[Which].fetch_add(1); }));

							if ((I % 4) == 3)
							{
								Graph.Wait(Mine);               // wait while the OTHER callers are submitting
							}
						}
					}
					Graph.Wait(Mine);
				});
			}
			for (std::thread& Submitter : Submitters)
			{
				Submitter.join();
			}
			Graph.Wait();

			std::size_t Missed = 0;
			std::size_t Doubled = 0;
			for (const std::atomic<int>& Slot : Runs)
			{
				const int Value = Slot.load();
				if (Value == 0)
				{
					++Missed;
				}
				if (Value > 1)
				{
					++Doubled;
				}
			}
			Check("B13 every node submitted by 4 threads ran (none lost)", Missed == 0);
			Check("B13 and none of them ran twice", Doubled == 0);
		}

		// B14: an exception that is NOT a `std::exception` is caught too -- a body is CALLER code, and the
		// caller may throw an int or a struct of its own. It still counts as COMPLETED: what waited for it
		// runs, and the scheduler survives it (an escaping exception would take the pool's worker with it).
		{
			std::atomic<bool> bWaiterRan{ false };
			const FTaskKey Thrower = KeyOf("B14.Thrower");
			const FTaskKey ThrowerKeys[] = { Thrower };

			Graph.Submit(Desc(Thrower, "B14.Thrower", []() { throw 42; }));
			Graph.Submit(Desc(KeyOf("B14.Waiter"), "B14.Waiter", [&]() { bWaiterRan.store(true); }),
				ThrowerKeys);

			Graph.Wait(ThrowerKeys);
			Check("B14 waiting on a node that threw 42 returns (it counts as completed)", true);
			Graph.Wait();
			Check("B14 and what waited for it still ran", bWaiterRan.load());
		}

		// B15: ONE POOL, ONE POT -- the design, not a coincidence. The pool does NOT sort work by graph:
		// every graph dispatches into the same work lane, so a node of one graph waits for a free worker
		// exactly like a node of another. What a graph keeps privately is its SCHEDULER (lane, table,
		// fence), which coordinates it without isolating it -- hence the fence of one graph ignores the
		// other's in-flight work, and a full workforce is not reserved for anybody.
		{
			FTaskGraph Second(Pool);
			Check("B15 a second graph comes up on the same pool", Second.Initialize());

			// (a) A graph's fence fences only itself.
			std::atomic<bool> bSlowEntered{ false };
			std::atomic<bool> bSlowDone{ false };
			FGate Release;
			Second.Submit(Desc(KeyOf("B15.Slow"), "B15.Slow", [&]()
				{
					bSlowEntered.store(true);
					Release.WaitOpen();
					bSlowDone.store(true);
				}));
			// Bounded: a test that can hang is a liability (a starved pool must FAIL here, not wait forever),
			// and generous, because the machine can be busy when this runs.
			for (int Spin = 0; Spin < 5000 && !bSlowEntered.load(); ++Spin)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			Check("B15 the other graph's first node started", bSlowEntered.load());

			std::atomic<int> RanInFirst{ 0 };
			for (int I = 0; I < 4; ++I)
			{
				Graph.Submit(Desc(KeyOf("B15.First", static_cast<std::uint64_t>(I)), "B15.First",
					[&]() { RanInFirst.fetch_add(1); }));
			}
			Graph.Wait();                             // returns while the OTHER graph still holds a node
			Check("B15 a graph's fence ignores the other graph's in-flight work",
				RanInFirst.load() == 4 && !bSlowDone.load());

			Release.Open();
			Second.Wait();
			Check("B15 and the other graph finished on the same pool", bSlowDone.load());

			// (b) No worker is reserved for any graph. The observation is POSITIVE on purpose: hold all but
			// one worker with THIS graph's nodes, and the other graph's node must still get the free one. (A
			// negative form -- "hold every worker, then assert the other graph cannot run" -- is a timing
			// observation: any worker being briefly busy for another reason inverts it, while a worker that is
			// genuinely reserved still fails this positive one.)
			constexpr int Held = 3;
			std::atomic<int> HeldEntered{ 0 };
			FGate Busy;
			for (int I = 0; I < Held; ++I)
			{
				Graph.Submit(Desc(KeyOf("B15.Held", static_cast<std::uint64_t>(I)), "B15.Held", [&]()
					{
						HeldEntered.fetch_add(1);
						Busy.WaitOpen();
					}));
			}
			for (int Spin = 0; Spin < 5000 && HeldEntered.load() != Held; ++Spin)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			Check("B15 this graph can occupy all but one worker", HeldEntered.load() == Held);

			std::atomic<bool> bSecondRan{ false };
			Second.Submit(Desc(KeyOf("B15.Free"), "B15.Free", [&]() { bSecondRan.store(true); }));
			Second.Wait();                             // one worker is free: the OTHER graph must be given it
			Check("B15 the free worker is not reserved for this graph", bSecondRan.load());

			Busy.Open();
			Graph.Wait();
			Check("B15 ... and this graph finished once its nodes were released", HeldEntered.load() == Held);
		}

		// B16: the DECLARATION + DERIVATION layer (`Core/Task/AccessDerivation.h`) -- no frame, no stage, no
		// resource: a work item declares which MEMORY it touches (`Core/Task/Access.h`), and a ledger turns
		// that into the predecessor keys. The default orders every hazard; the caller subtracts what it
		// synchronizes itself (an exemption), because only it knows.
		//
		// The ledger eats the DECLARATION's own TYPE LIST (`B16_Read::List`, composed by the compiler through
		// `MAHO_DECLARE_ACCESSES`) -- so the tokens it orders by are exactly the ones that were declared, and a
		// type with no declaration is a compile error, not a run-time miss.
		{
			// A region's token is its DECLARED TYPE's row address: stable, and the same in every module.
			Check("B16 a region's token is stable and TYPE-derived",
				Core::RegionToken<TaskGraphTest::FB16BufferRegion>() == Core::RegionToken<TaskGraphTest::FB16BufferRegion>()
					&& Core::RegionToken<TaskGraphTest::FB16BufferRegion>()
						!= Core::RegionToken<TaskGraphTest::FB16ChannelRegion>());

			// The declaration reached the metadata store: what a query decodes is what was declared.
			std::size_t DeclaredCount = 0;
			// A COPY, not a view: the name points into one snapshot row, and the snapshot dies with the loop.
			std::string DeclaredName{};
			Core::EMemoryAccess DeclaredMode = Core::EMemoryAccess::Read;
			ForEachDeclaredAccess<TaskGraphTest::B16_Write>([&](std::string_view Name, Core::EMemoryAccess Mode)
				{
					++DeclaredCount;
					DeclaredName = Name;
					DeclaredMode = Mode;
				});
			Check("B16 a declared item is found in the metadata store", DeclaredCount == 1);
			Check("B16 the region is named by its DECLARED TYPE, and the mode came with it",
				DeclaredName == "TaskGraphTest::FB16BufferRegion" && DeclaredMode == Core::EMemoryAccess::Write);

			std::size_t EmptyCount = 0;
			ForEachDeclaredAccess<TaskGraphTest::B16_Nothing>(
				[&](std::string_view, Core::EMemoryAccess) { ++EmptyCount; });
			Check("B16 a declaration with no accesses files no row", EmptyCount == 0);

			// The default orders write -> read: a reader waits for the writer.
			{
				FMemoryAccessLedger Ledger(4);
				const FTaskKey Writer = KeyOf("B16.Writer");
				(void)Ledger.Record<TaskGraphTest::B16_Write>(Writer, 0);

				const FDerivationResult Derived =
					Ledger.Record<TaskGraphTest::B16_Read>(KeyOf("B16.Reader"), 0);
				Check("B16 a reader is derived after the writer of the same memory",
					Derived.Predecessors.size() == 1 && Derived.Predecessors[0] == Writer);
			}

			// The derivation names the NEAREST writer only -- it is an edge set, not a barrier. The older
			// writer is already ordered through the newer one (writers are chained), so naming it again would
			// only add an edge that says nothing.
			{
				FMemoryAccessLedger Ledger(4);
				const FTaskKey FirstWriter = KeyOf("B16.Writer1");
				const FTaskKey SecondWriter = KeyOf("B16.Writer2");
				(void)Ledger.Record<TaskGraphTest::B16_Write>(FirstWriter, 0);
				const FDerivationResult Chained =
					Ledger.Record<TaskGraphTest::B16_Write>(SecondWriter, 0);
				Check("B16 a writer waits for the previous writer",
					Chained.Predecessors.size() == 1 && Chained.Predecessors[0] == FirstWriter);

				const FDerivationResult Derived =
					Ledger.Record<TaskGraphTest::B16_Read>(KeyOf("B16.Reader2"), 0);
				Check("B16 a reader names the NEAREST writer only (the older one is transitively ordered)",
					Derived.Predecessors.size() == 1 && Derived.Predecessors[0] == SecondWriter);
			}

			// A write waits for the readers that followed the previous writer (read -> write), and those
			// readers are cleared by it: the write after THAT one waits for the new writer alone.
			{
				FMemoryAccessLedger Ledger(4);
				const FTaskKey Writer = KeyOf("B16.WarWriter");
				const FTaskKey FirstReader = KeyOf("B16.WarReader1");
				const FTaskKey SecondReader = KeyOf("B16.WarReader2");
				(void)Ledger.Record<TaskGraphTest::B16_Write>(Writer, 0);
				(void)Ledger.Record<TaskGraphTest::B16_Read>(FirstReader, 0);
				(void)Ledger.Record<TaskGraphTest::B16_Read>(SecondReader, 0);

				const FTaskKey SecondWriter = KeyOf("B16.WarWriter2");
				const FDerivationResult War =
					Ledger.Record<TaskGraphTest::B16_Write>(SecondWriter, 0);
				Check("B16 a write waits for the writer AND everyone reading since",
					War.Predecessors.size() == 3);

				const FDerivationResult After =
					Ledger.Record<TaskGraphTest::B16_Read>(KeyOf("B16.WarReader3"), 0);
				Check("B16 ... and the readers it waited for are cleared by its own write",
					After.Predecessors.size() == 1 && After.Predecessors[0] == SecondWriter);
			}

			// read -> read is no hazard at all: nothing to order, no edge derived.
			{
				FMemoryAccessLedger Ledger(4);
				(void)Ledger.Record<TaskGraphTest::B16_Read>(KeyOf("B16.ReaderA"), 0);
				const FDerivationResult Derived =
					Ledger.Record<TaskGraphTest::B16_Read>(KeyOf("B16.ReaderB"), 0);
				Check("B16 two readers of the same memory derive no edge", Derived.Predecessors.empty());
			}

			// A destroy IS a write: whoever touches the memory next is ordered after it (the layer decides
			// whether that is legal -- validity is not this ledger's business).
			{
				FMemoryAccessLedger Ledger(4);
				const FTaskKey Destroyer = KeyOf("B16.Destroyer");
				(void)Ledger.Record<TaskGraphTest::B16_Write>(Destroyer, 0);

				const FDerivationResult Reuse =
					Ledger.Record<TaskGraphTest::B16_Write>(KeyOf("B16.Reuser"), 0);
				Check("B16 a destroy is just a write: the next user is ordered after it",
					Reuse.Predecessors.size() == 1 && Reuse.Predecessors[0] == Destroyer);
			}

			// An EXEMPTION is how a layer says "this pair I synchronize myself" (a hand-off channel).
			{
				FMemoryAccessLedger Ledger(4);
				const FTaskKey Producer = KeyOf("B16.Producer");
				const FTaskKey Consumer = KeyOf("B16.Consumer");
				(void)Ledger.Record<TaskGraphTest::B16_ChannelWrite>(Producer, 0);
				Ledger.Exempt(Core::RegionToken<TaskGraphTest::FB16ChannelRegion>(), Producer, Consumer, 0);

				const FDerivationResult Derived =
					Ledger.Record<TaskGraphTest::B16_Read>(Consumer, 0);   // different region!
				const FDerivationResult HandedOff =
					Ledger.Record<TaskGraphTest::B16_ChannelWrite>(KeyOf("B16.Consumer2"), 0);
				Check("B16 an exemption removes exactly the pair it names",
					Derived.Predecessors.empty() && HandedOff.Predecessors.size() == 1);
			}

			// AGEING, not forgetting: the ledger has no clock and no `Forget` -- every call carries its frame,
			// and anything K frames old is dropped (it is provably complete by then). Two frames below with
			// K = 2 is exactly the boundary.
			{
				FMemoryAccessLedger Ledger(2);

				// Frame 10: a writer.
				(void)Ledger.Record<TaskGraphTest::B16_Write>(KeyOf("B16.Old"), 10);

				// Frame 11: a reader still sees it (one frame old).
				const FDerivationResult SameFrame =
					Ledger.Record<TaskGraphTest::B16_Read>(KeyOf("B16.Near"), 11);
				Check("B16 a one-frame-old writer is still ordered against", SameFrame.Predecessors.size() == 1);

				// Frame 12: the writer is K = 2 frames old ⇒ dropped, and the reader that followed it too.
				const FDerivationResult Aged =
					Ledger.Record<TaskGraphTest::B16_Read>(KeyOf("B16.Far"), 12);
				Check("B16 an accessor K frames old ages out without anyone calling Forget",
					Aged.Predecessors.empty() && Ledger.Count() == 2);
			}

			// INCLUDING a declaration is how a caller inherits what it CALLS: the helper's write becomes the
			// caller's own access, so the caller is ordered after whoever wrote that region before.
			{
				FMemoryAccessLedger Ledger(4);
				const FTaskKey Writer = KeyOf("B16.HelperWriter");
				(void)Ledger.Record<TaskGraphTest::B16_Helper>(Writer, 0);

				const FDerivationResult Caller =
					Ledger.Record<TaskGraphTest::B16_Caller>(KeyOf("B16.Caller"), 0);
				Check("B16 a declaration that INCLUDES another inherits its accesses",
					Caller.Predecessors.size() == 1 && Caller.Predecessors[0] == Writer);

				// The metadata store got the included rows too: the caller declares TWO regions (its own
				// read, and the helper's write).
				std::size_t Rows = 0;
				ForEachDeclaredAccess<TaskGraphTest::B16_Caller>(
					[&](std::string_view, Core::EMemoryAccess) { ++Rows; });
				Check("B16 ... and the store files what it pulled in", Rows == 2);
			}

			// A DIAMOND (two paths to one declaration) reaches the same access twice: that must stay harmless.
			{
				FMemoryAccessLedger Ledger(4);
				const FTaskKey Writer = KeyOf("B16.DiamondWriter");
				(void)Ledger.Record<TaskGraphTest::B16_Helper>(Writer, 0);

				const FDerivationResult Diamond =
					Ledger.Record<TaskGraphTest::B16_Diamond>(KeyOf("B16.Diamond"), 0);
				Check("B16 a diamond in a declaration tree stays harmless (one edge, no self-edge)",
					Diamond.Predecessors.size() == 1 && Diamond.Predecessors[0] == Writer);
			}
		}

		// B17: the unit a builder turns into a node. It is a DECLARATION (a name, a method, a signature) and it
		// runs by a plain static call: no vtable, no RTTI, nothing looked up at run time -- and no function
		// pointer either, because `Run` is a template resolved per OWNER type.
		// The runner is a builder's job; here we only prove the declaration is usable, that the signature it
		// spelled is what the contract says it is, and that its NAME is the name its accesses were declared
		// under (a mismatch there is the quiet kind: no accesses means no derived edges at all).
		{
			// The owner implements the three shapes with DIFFERENT parameter lists (one by value, one by
			// reference): which one a call reaches is plain C++ overload resolution inside `Invoke`.
			struct FOwner
			{
				int Calls[3] = { 0, 0, 0 };
				void Tick(int) { ++Calls[0]; }                                  // by value: handed an lvalue
				void Tick(int&, float) { ++Calls[1]; }
				void Tick(int&, float&, double) { ++Calls[2]; }
			};

			// The shapes' contracts, as the compiler sees them: that is what `TTaskSignature` is for -- a typo
			// in the return type or an empty signature is a compile error, not a silent mismatch. The shape IS
			// a type, and its name is derived from the signature it was declared with.
			static_assert(std::is_same_v<ITick::FMethod<void(int&)>::FSignature, void(int&)>);
			static_assert(std::is_same_v<ITick::FMethod<void(int&)>::FContexts, TTypeList<int&>>);
			static_assert(std::is_same_v<
				ITick::FMethod<void(int&, float&, double)>::FContexts, TTypeList<int&, float&, double>>);

			// The task's name is DERIVED from the method's (`F` + the method + `Task`), and its protocol row
			// files both -- the identity reads as the thing it is: the task FOR `Tick`.
			Check("B17 the task's identity is the method's with an `F`...`Task`, and the method name came along",
				ITick::StaticName() == std::string_view("ITick")
					&& ITick::MethodName() == std::string_view("Tick"));

			// ... and the DECLARED shapes are the interface: which calls exist is a compile-time fact, checked
			// here without calling anything. A shape no entry declared -- another type list, or another arity --
			// is not a viable overload at all.
			static_assert(CTaskRunnable<ITick, FOwner, int>);
			static_assert(CTaskRunnable<ITick, FOwner, int, float>);
			static_assert(CTaskRunnable<ITick, FOwner, int, float, double>);
			static_assert(!CTaskRunnable<ITick, FOwner, int, int>);         // (int, int) is no declared shape
			static_assert(!CTaskRunnable<ITick, FOwner, int, float, double, char>);   // nor is another arity

			FOwner Owner;
			int Delta = 16;
			float Scale = 1.5f;
			double Offset = 2.0;

			ITick::Run(Owner, Delta);                           // the 1-argument shape
			RunTask<ITick>(Owner, Delta);                       // the same, through the builder's helper
			ITick::Run(Owner, Delta, Scale);                    // ... and the 2- and 3-argument ones
			ITick::Run(Owner, Delta, Scale, Offset);
			Check("B17 each declared shape reaches its own overload of the method, statically",
				Owner.Calls[0] == 2 && Owner.Calls[1] == 1 && Owner.Calls[2] == 1);

			// The ACCESS declaration belongs to the BINDING (owner + task + shape), not to the shape: what memory
			// a call touches is the implementation's business. Here it is the one the test declared above.
			using FTickRow = Core::TBindingAccess<TaskGraphTest::FIntTickOwner, ITick,
				ITick::FMethod<void(int&)>>;
			std::size_t DeclaredForBinding = 0;
			ForEachDeclaredAccess<FTickRow>(
				[&](std::string_view Name, Core::EMemoryAccess Mode)
				{
					++DeclaredForBinding;
					Check("B17 a binding's row is the row it declared, region and mode intact",
						Name == "TaskGraphTest::FB16BufferRegion" && Mode == Core::EMemoryAccess::Write);
				});
			Check("B17 ... and its name is owner|method|signature",
				DeclaredForBinding == 1
					&& FTickRow::TagName == std::string("TaskGraphTest::FIntTickOwner|Tick|(int&)"));

			// A binding is also the handle the ledger is fed with -- and the binding is what says WHICH memory
			// the call touches, so a builder derives an edge from the binding it is about to run.
			{
				Core::FMemoryAccessLedger Ledger(2);
				const FTaskKey Writer = KeyOf("B17.ShapeWriter");
				(void)Ledger.Record<TaskGraphTest::B17_Tick_Execute>(Writer, 0);

				const Core::FDerivationResult Derived = Ledger.Record<FTickRow>(KeyOf("B17.ShapeRunner"), 0);
				Check("B17 a binding feeds the ledger: the run is ordered after the last writer",
					Derived.Predecessors.size() == 1 && Derived.Predecessors[0] == Writer);
			}

			// The TASK files a protocol row of its own (`KTaskProtocol`): its identity and the one method its
			// shapes share -- so a dump can say what a task IS without knowing any of its shapes.
			const FMetaDataEntry* TaskRow =
				GetMetaData().Find(Core::KTaskProtocol, ITick::StaticName());
			Check("B17 the task filed its protocol row: identity + the shared method name",
				TaskRow != nullptr && TaskRow->Value == "Tick");
		}

		// B18: the reference BINDING (`Core/Task/TaskDispatch.h`) -- the three steps a builder takes for ONE
		// shape of ONE task on ONE owner: a key, the edges the shape implies, and the closure the graph runs.
		{
			using FOwner = TaskGraphTest::FIntTickOwner;

			using FBinding = TTaskBinding<FOwner, ITick, ITick::FMethod<void(int&)>>;                  // this task, on this owner TYPE
			using FIntShape = ITick::FMethod<void(int&)>;                    // one of its shapes

			// A shape's identity is a specialization of the TASK's own member template, so "is this one of my
			// shapes" is exact -- and a signature nobody declared is the primary, which is not a shape.
			static_assert(CTaskShape<ITick, FIntShape>);
			static_assert(!CTaskShape<ITick, ITick::FMethod<void(char)>>);
			static_assert(CTaskRunnable<ITick, FOwner, int>);

			FOwner Owner;
			int Delta = 16;
			const std::uint64_t Round = 3;

			// (1) the key names the node: same shape, same instance, same round is the SAME node -- another
			// round, or another INSTANCE of this owner type, is another one. The instance is a number the
			// layer chose (`FInstanceId`), never the owner's address: an address drifts with the layout and
			// means nothing.
			const FTaskKey Key = FBinding::KeyOf(Round);
			Check("B18 the reference key separates rounds and instance ids, and never an address",
				Key == FBinding::KeyOf(Round)
					&& Key != FBinding::KeyOf(Round + 1)
					&& Key != FBinding::KeyOf(Round, FInstanceId{ 1 }));

			// (2) the edges come from the SHAPE's own declaration (two shapes of one task may touch different
			// memory, so the shape -- not the task -- is what the ledger eats).
			FMemoryAccessLedger Ledger(2);
			const FTaskKey Earlier = KeyOf("B18.Earlier");
			(void)Ledger.Record<TaskGraphTest::B17_Tick_Execute>(Earlier, Round);
			const FDerivationResult Edges = FBinding::Derive(Ledger, Key, Round, nullptr);
			Check("B18 the binding derives the edges from the shape's declaration",
				Edges.Predecessors.size() == 1 && Edges.Predecessors[0] == Earlier);

			// (3) and the closure it builds runs the declared shape on THIS owner, statically.
			Graph.Submit(FBinding::Node(Key, Owner, Delta), Edges.Predecessors);
			Graph.Wait();
			Check("B18 the node the binding built ran the shape on its owner", Owner.Ran == 16);
		}

		// B19: ONE ROUND of nodes (`FTaskBuffer`) -- what the layer's stage body does. Each node goes into
		// the graph with ITS OWN derived edges (a derived edge is not a batch-wide one: flattening them would
		// make two writers of one region wait for each other), plus the edges the layer adds itself.
		{
			using FOwner = TaskGraphTest::FIntTickOwner;

			using FBinding = TTaskBinding<FOwner, ITick, ITick::FMethod<void(int&)>>;
			using FIntShape = ITick::FMethod<void(int&)>;

			using FIntTick = MAHO_TASK_BINDING(FOwner, Tick, (int&));   // the layer's binding: one node kind

			FOwner Owner;
			int Delta = 16;

			FMemoryAccessLedger Ledger(2);

			FTaskDispatcher Clock(Graph, Ledger);
			FTaskBuffer Round;                                 // a recorder: no graph, no clock
			const FTaskKey Gate = KeyOf("B19.Gate");
			const FTaskKey LayerEdge = KeyOf("B19.LayerEdge");

			// RECORDING is PURE: nothing is in flight, and the LEDGER has not been touched (its size is the
			// read-only witness -- a probe would write).
			const std::uint64_t Frame = 1;
			(void)Clock.Advance(Frame);
			Round.Begin();
			const FTaskKey FirstStable = Round.Add<FBinding>(Owner, Delta);
			const FTaskKey SecondStable = FBinding::StableKeyOf(FInstanceId{}, 1);
			Round.Add<FBinding>(SecondStable, Owner, Delta);   // the caller's stable key, verbatim
			Round.WaitFor(LayerEdge);                                // recorded AFTER the nodes: still round-wide
			Round.WaitBefore(Gate);                                  // never submitted: skipped, not waited for
			Check("B19 recording is pure: nothing ran, and the ledger was not touched",
				Round.NodeCount() == 2 && FirstStable != SecondStable && Owner.Ran == 0 && Ledger.Count() == 0);

			// ... and the command-buffer split holds: ENDING the recording still submits nothing; SUBMITTING is
			// what computes keys, derives edges and puts the nodes in.
			Check("B19 ending the recording closes it without submitting",
				Round.End() == 2 && Owner.Ran == 0 && Ledger.Count() == 0);
			Check("B19 submitting puts every recorded node in", Clock.Submit(Round) == 2);
			Graph.Wait();
			Check("B19 the submit fed the ledger and the nodes ran",
				Owner.Ran == 32 && Ledger.Count() > 0);

			// The SAME recording at the NEXT frame: this is the cross-frame cache, and it is legal because the
			// frame and the edges are computed at submit (the recording itself is frame-free).
			(void)Clock.Advance(Frame + 1);
			Check("B19 the same recording submits again at a new frame",
				Clock.Submit(Round) == 2 && Round.SubmitCount() == 2);
			Graph.Wait();
			Check("B19 ... and its nodes ran a second time, under the new frame's keys", Owner.Ran == 64);
		}

		// B20: the CLOCK (`FTaskDispatcher`) retires rounds, and a SHELL hands each member its own context.
		//
		// Two things this pins down: (a) a layer does not keep the keys it submitted -- the clock does, and
		// `Advance` waits for the retired round and forgets it (so the derivation's state does not grow with
		// the round count); (b) the group's SHELL is what produces each member's contexts, which is the job a
		// stage used to get from its slot type.
		{
			using FOwner = TaskGraphTest::FIntTickOwner;
			using FOtherOwner = TaskGraphTest::FOtherTickOwner;

			// A shell: it holds the frame's inputs (here: this round's two contexts) and gives each member
			// the ones it declared -- that is all "a group's shell" is, and it is the layer's code.
			struct FShell
			{
				FOwner   World;
				FOtherOwner Editor;
				int      Delta = 3;
				int      Sim = 5;

				// ... the members, in the order the shell chooses; the edges among them are DERIVED.
				void Run(FTaskBuffer& Round, FTaskDispatcher& Clock)
				{
					Round.Begin();
					(void)Round.Add<TTaskBinding<FOwner, ITick, ITick::FMethod<void(int&)>>>(World, Delta);
					(void)Round.Add<TTaskBinding<FOtherOwner, ITick, ITick::FMethod<void(int&)>>>(
						Editor, Delta);
					(void)Round.End();
					(void)Clock.Submit(Round);
				}
			};

			FMemoryAccessLedger Ledger(2);     // the ledger ages by THIS K: it must match the dispatcher
			FTaskDispatcher Clock(Graph, Ledger, 2);          // two rounds may overlap
			FTaskBuffer Round;
			FShell Shell;

			// Four rounds, each driven by the HOST's frame (that is the point: the number comes from the loop,
			// never from the clock).
			std::uint64_t Frame = 0;
			const auto NextFrame = [&]() { return Clock.Advance(++Frame); };
			(void)NextFrame();
			Shell.Run(Round, Clock);
			(void)NextFrame();
			Shell.Run(Round, Clock);
			(void)NextFrame();
			Shell.Run(Round, Clock);
			const std::size_t Remembered = Ledger.Count();
			(void)NextFrame();
			Shell.Run(Round, Clock);
			Graph.Wait();
			Check("B20 the clock retires an old round (the derivation does not grow with the round count)",
				Ledger.Count() == Remembered);
			Check("B20 the slot is the frame modulo the rounds in flight", Clock.Slot() == Frame % 2);
			Check("B20 the shell's members ran on their own owners, each with its own context",
				Shell.World.Ran == 12 && Shell.Editor.Ran == 12);
		}

		// B21: the FRAME NUMBER is the HOST's, so two graphs driven by the same loop never fall out of step.
		//
		// That is what makes a cross-graph edge writable at all: layer B has to be able to name a node of
		// layer A. Every term of the key is shared (the shape's token, the owner type's token, the instance
		// id), and the frame is the one that is NOT recomputable from anything but the loop -- so it has to be
		// passed in, not counted per graph.
		{
			using FOwner = TaskGraphTest::FIntTickOwner;

			FOwner Owner;
			int Delta = 4;

			FThreadPool OtherPool(2);
			FTaskGraph OtherGraph(OtherPool);
			OtherGraph.Initialize();

			FMemoryAccessLedger LedgerA(4);
			FMemoryAccessLedger LedgerB(4);
			FTaskDispatcher ClockA(Graph, LedgerA);
			FTaskDispatcher ClockB(OtherGraph, LedgerB);
			FTaskBuffer RoundA;
			FTaskBuffer RoundB;

			// One loop, one frame: both layers are told the SAME number.
			const std::uint64_t HostFrame = 7;
			(void)ClockA.Advance(HostFrame);
			(void)ClockB.Advance(HostFrame);

			using FBinding = TTaskBinding<FOwner, ITick, ITick::FMethod<void(int&)>>;
			using FIntShape = ITick::FMethod<void(int&)>;

			RoundA.Begin();
			const FTaskKey KeyInA = RoundA.Add<FBinding>(Owner, Delta);
			(void)RoundA.End();
			(void)ClockA.Submit(RoundA);

			RoundB.Begin();
			const FTaskKey KeyInB = RoundB.Add<FBinding>(Owner, Delta);
			(void)RoundB.End();
			(void)ClockB.Submit(RoundB);                      // same shape/owner/frame ⇒ the SAME key, on purpose
			Graph.Wait();
			OtherGraph.Wait();

			// The two graphs each run their own node, and the keys they were filed under agree -- which is the
			// whole reason the frame number must not be per-graph.
			Check("B21 two layers driven by the same frame name the same node identically", KeyInA == KeyInB);
			Check("B21 ... and each graph still runs its own copy of it",
				Owner.Ran == Delta * 2 && ClockB.Frame() == HostFrame);
		}

		// B22: SECONDARY recorders -- chunks recorded anywhere (here: three threads) and executed from one
		// primary. Recording touches nothing shared (B19 pinned that down), so this needs no lock of its own;
		// the ORDER they are appended in is the order the derivation sees.
		{
			using FChunkOwner = TaskGraphTest::FIntTickOwner;
			using FBinding = TTaskBinding<FChunkOwner, ITick, ITick::FMethod<void(int&)>>;
			using FIntShape = ITick::FMethod<void(int&)>;

			FChunkOwner A;
			FChunkOwner B;
			FChunkOwner C;
			int DeltaA = 1;
			int DeltaB = 2;
			int DeltaC = 3;

			FTaskBuffer ChunkA;
			FTaskBuffer ChunkB;
			FTaskBuffer ChunkC;

			// Three instances of ONE owner type: they must carry distinct instance identities, or all three
			// nodes would be handed the same key (which the graph refuses -- see `FInstanceId`).
			std::thread ThreadA([&] { ChunkA.Begin(); (void)ChunkA.Add<FBinding>(FBinding::StableKeyOf(FInstanceId{ 1 }), A, DeltaA); (void)ChunkA.End(); });
			std::thread ThreadB([&] { ChunkB.Begin(); (void)ChunkB.Add<FBinding>(FBinding::StableKeyOf(FInstanceId{ 2 }), B, DeltaB); (void)ChunkB.End(); });
			std::thread ThreadC([&] { ChunkC.Begin(); (void)ChunkC.Add<FBinding>(FBinding::StableKeyOf(FInstanceId{ 3 }), C, DeltaC); (void)ChunkC.End(); });
			ThreadA.join();
			ThreadB.join();
			ThreadC.join();
			Check("B22 three recorders are filled in parallel (recording shares nothing)",
				ChunkA.NodeCount() == 1 && ChunkB.NodeCount() == 1 && ChunkC.NodeCount() == 1);

			FMemoryAccessLedger Ledger(4);
			FTaskDispatcher Clock(Graph, Ledger);
			(void)Clock.Advance(1);

			FTaskBuffer Primary;
			Primary.Begin();
			Primary.Append(std::move(ChunkA));                     // vkCmdExecuteCommands, in this order
			Primary.Append(std::move(ChunkB));
			Primary.Append(std::move(ChunkC));
			Check("B22 the primary holds every chunk's nodes", Primary.End() == 3);
			Check("B22 ... and submits them in one call", Clock.Submit(Primary) == 3);
			Graph.Wait();
			Check("B22 the primary's submit ran all three chunks on their own owners",
				A.Ran == 1 && B.Ran == 2 && C.Ran == 3);
		}

		// B24: PROPERTIES -- a field whose storage is private, so the only way to touch it is Get()/Set(), and
		// those two carry the mode (Get hands out const). Its region is the nested TAG the macro generates, so
		// nothing is named by hand: two properties of the SAME type are still two regions.
		{
			struct FEntity
			{
				MAHO_PROPERTY(int, Position);
				MAHO_PROPERTY(int, Scale);      // same value type, different property
				MAHO_PROPERTY(float, Blur);
			};

			// The mode is the reference qualification: Get cannot write, Set can.
			static_assert(std::is_same_v<decltype(std::declval<FEntity&>().Position.Get()), const int&>);
			static_assert(std::is_same_v<decltype(std::declval<FEntity&>().Position.Set()), int&>);

			// The identity is the tag: same value type, different names, different regions.
			using FPositionRegion = FEntity::PositionRegion;
			using FScaleRegion = FEntity::ScaleRegion;
			static_assert(!std::is_same_v<FPositionRegion, FScaleRegion>);
			Check("B24 two properties of the same type are two regions",
				Core::RegionToken<FPositionRegion>() != Core::RegionToken<FScaleRegion>()
					&& Core::RegionToken<FPositionRegion>() == Core::RegionToken<FPositionRegion>());

			// ... and each instance knows which piece of memory it is.
			FEntity A;
			FEntity B;
			Check("B24 two instances of one property are two pieces of memory",
				A.Position.Instance() != B.Position.Instance());

			// A namespace-scope memory binding is the same thing for global memory: a region of its own plus a
			// property, so it is nameable on the declaration side and unreachable except through Get/Set.
			B29_Counter.Set() = 5;
			Check("B29 a declared memory binding is a property with a region of its own",
				B29_Counter.Get() == 5
					&& Core::RegionName<B29_CounterRegion>() != std::string_view{}
					&& Core::RegionToken<B29_CounterRegion>() != nullptr);

			// A property region is a region like any other: it can be declared and derived from.
			{
				FMemoryAccessLedger Ledger(2);
				const FTaskKey Earlier = KeyOf("B24.Earlier");
				(void)Ledger.Record<B24_PropertyWrite>(Earlier, 1);

				// (the declaration lives at namespace scope, below -- a local struct cannot be the tag)
				B24_PropertyHolder Holder;
				Holder.Position.Set() = 3;
				const FDerivationResult Derived = Ledger.Record<B24_PropertyWrite>(KeyOf("B24.Later"), 1);
				Check("B24 a declared property region derives an edge", Derived.Predecessors.size() == 1);
			}
		}

		// B25: the guards that belong to `FTaskSystem` itself -- a submission without a running graph or an open
		// frame would be meaningless (dropped work / keys nobody retires), and a frame that does not advance
		// would reuse keys. All three are REFUSED (and stop a debugger); Shipping keeps only the refusal.
		{
			using FBindingRow = TTaskBinding<TaskGraphTest::FIntTickOwner, ITick,
				ITick::FMethod<void(int&)>>;
			TaskGraphTest::FIntTickOwner Owner;
			int Delta = 1;

			FThreadPool GuardPool(2);
			FTaskSystem System(GuardPool);
			FTaskBuffer Buffer;
			Buffer.Begin();
			(void)Buffer.Add<FBindingRow>(Owner, Delta);
			(void)Buffer.End();

			Check("B25 a submission before Initialize is refused",
				System.Submit(Buffer) == 0 && Owner.Ran == 0);
			Check("B25 a submission with no frame open is refused",
				System.Initialize() && System.Submit(Buffer) == 0 && Owner.Ran == 0);

			const std::uint64_t Opened = System.Advance(5);
			Check("B25 a frame cannot go backwards (the open frame is kept)",
				Opened == 5 && System.Advance(4) == 5 && System.Slot() == 5 % 1);

			// ... and with a frame open it goes in, as usual.
			Check("B25 with a frame open the same buffer submits", System.Submit(Buffer) == 1);
			System.Wait();
			Check("B25 ... and ran", Owner.Ran == Delta);
		}

		// B27: the INSTANCE dimension -- what replaced the hand-off marker. Two owners of one type writing the
		// SAME property region are two pieces of memory (no edge); the same owner twice is the same memory (edge).
		{
			FMemoryAccessLedger Ledger(4);
			B24_PropertyHolder A;
			B24_PropertyHolder B;

			// A memory binding is a VIEW of external memory, and the metadata decides what it may be mounted on.
			int Storage = 7;
			Core::TMemoryBinding<B24_PropertyHolder::PositionRegion> Binding(Storage);
			static_assert(std::is_same_v<decltype(Binding.Get()), const int&>);   // the type came from the metadata
			Binding.Set() = 9;
			Check("B27 a memory binding is a view of external memory",
				Binding.Get() == 9 && Binding.Instance() == &Storage);

			const FTaskKey WriteA = KeyOf("B27.A");
			(void)Ledger.Record<B24_PropertyWrite>(WriteA, 1, &A);
			const Core::FDerivationResult ReadB =
				Ledger.Record<B24_PropertyWrite>(KeyOf("B27.B"), 1, &B);
			Check("B27 the same property region on ANOTHER instance does not order (two pieces of memory)",
				ReadB.Predecessors.empty());

			const Core::FDerivationResult ReadA =
				Ledger.Record<B24_PropertyWrite>(KeyOf("B27.A2"), 1, &A);
			Check("B27 ... but on the SAME instance it does",
				ReadA.Predecessors.size() == 1 && ReadA.Predecessors[0] == WriteA);
		}

		// B28: the HONESTY CHECK itself. A node's scope is fed the declaration (by the binding) and the accesses
		// (by the handles); Report() says what does not match -- both directions, and the warning for a branch
		// that never ran.
		{
			const auto Region = [] { return Core::RegionToken<TaskGraphTest::FB16BufferRegion>(); };
			const auto Name = [] { return Core::RegionName<TaskGraphTest::FB16BufferRegion>(); };
			const auto OtherRegion = [] { return Core::RegionToken<TaskGraphTest::FB16ChannelRegion>(); };
			const auto OtherName = [] { return Core::RegionName<TaskGraphTest::FB16ChannelRegion>(); };

			{
				Core::FAccessObservation Scope;
				Scope.Declare(Region(), Name(), Core::EMemoryAccess::Read);
				Scope.Observe(OtherRegion(), OtherName(), nullptr, Core::EMemoryAccess::Write);
				Check("B28 touching memory nobody declared is reported", Scope.Report("B28.Undeclared") >= 1);
			}
			{
				Core::FAccessObservation Scope;
				Scope.Declare(Region(), Name(), Core::EMemoryAccess::Write);
				Check("B28 a declared write nobody made is reported", Scope.Report("B28.NeverWrote") >= 1);
			}
			{
				Core::FAccessObservation Scope;
				Scope.Declare(Region(), Name(), Core::EMemoryAccess::Read);
				Scope.Observe(Region(), Name(), nullptr, Core::EMemoryAccess::Write);
				Check("B28 a declared READ that actually WROTE is reported", Scope.Report("B28.WeakDecl") >= 1);
			}
			{
				Core::FAccessObservation Scope;
				Scope.Declare(Region(), Name(), Core::EMemoryAccess::Write);
				Scope.Observe(Region(), Name(), nullptr, Core::EMemoryAccess::Write);
				Check("B28 an honest node reports nothing", Scope.Report("B28.Honest") == 0);
			}
		}

		// B23: the ONE-STOP environment (`FTaskSystem`) -- graph + ledger + dispatcher in one object, with the
		// advanced frame ANNOUNCED to whoever keeps per-slot data, and per-slot recordings checked at submit.
		{
			using FOwner = TaskGraphTest::FIntTickOwner;
			using FBinding = TTaskBinding<FOwner, ITick, ITick::FMethod<void(int&)>>;
			using FIntShape = ITick::FMethod<void(int&)>;

			FOwner Owner;
			int Delta = 5;

			FThreadPool SystemPool(2);
			FTaskSystem System(SystemPool, 2);          // K = 2 -- ONE number for ledger and dispatcher
			Check("B23 the system comes up on a pool it does not own", System.Initialize());
			Check("B23 ... and reports the one K", System.FramesInFlight() == 2);

			// The layer's per-slot context (here: a per-slot counter) is indexed by the slot it is told about;
			// the delegate is what a layer wraps its "reset the slot that just came free" work in.
			int SlotTicks[2] = { 0, 0 };
			std::uint64_t SeenFrames = 0;
			const FSubscriptionID Subscription = System.OnAdvance.Bind(
				[&](std::uint64_t Frame, std::uint64_t Slot, std::uint64_t RetiredFrame)
				{
					(void)Frame;
					(void)RetiredFrame;
					++SeenFrames;
					++SlotTicks[Slot];
				});
			Check("B23 the advance delegate is bound", Subscription != 0);

			// Two frames: the delegate fires each time and names the slot the frame occupies.
			(void)System.Advance(1);                    // slot 1
			(void)System.Advance(2);                    // slot 0
			Check("B23 OnAdvance names each frame's slot",
				SeenFrames == 2 && SlotTicks[0] == 1 && SlotTicks[1] == 1 && System.Slot() == 0);

			// A recording made for slot 1 may not be submitted at slot 0: that is the per-slot cache mistake,
			// and the system is the one place that can see it.
			FTaskBuffer CachedForSlot1;
			CachedForSlot1.RecordedFor(1);
			CachedForSlot1.Begin();
			(void)CachedForSlot1.Add<FBinding>(Owner, Delta);
			(void)CachedForSlot1.End();
			Check("B23 a recording made for another slot is refused",
				System.Submit(CachedForSlot1) == 0 && Owner.Ran == 0 && System.Remembered() == 0);

			// ... and at its own slot it goes in -- twice, two frames apart, which is the cache.
			(void)System.Advance(3);                    // slot 1
			Check("B23 the same recording is accepted on its own slot", System.Submit(CachedForSlot1) == 1);
			System.Wait();
			(void)System.Advance(4);                    // slot 0
			(void)System.Advance(5);                    // slot 1 -- the cached recording again
			Check("B23 the cached recording is submitted again on its slot", System.Submit(CachedForSlot1) == 1);
			System.Wait();
			Check("B23 both submissions ran, and the derivation did not grow with the frames",
				Owner.Ran == 2 * Delta && System.Remembered() <= 4);
			System.OnAdvance.Unbind(Subscription);
		}

		// B30: the four steps of the block above the test bodies -- interface, implementation, binding, run.
		// Nothing new is shown here on purpose: this is what a developer's code looks like when it works.
		{
			Maho::DemoLayer::FLight Light;
			Light.Velocity.Set() = 3.0f;                  // a property: private value, Get()/Set() only
			float Dt = 0.5f;

			FThreadPool Pool(2);
			FTaskSystem System(Pool);                     // K = 1: the simplest correct policy
			Check("B30 (1-3) the class implements the task and the layer's system comes up",
				System.Initialize());

			// (4) record once...
			FTaskBuffer Frame;
			Frame.Begin();
			(void)Frame.Add<FLightSwarm>(Light, Dt);
			Frame.End();
			Check("B30 one Add is one node; the buffer names no key and derives no edge",
				Frame.NodeCount() == 1);

			(void)System.Advance(1);
			Check("B30 the recording submits at the open frame", System.Submit(Frame) == 1);
			System.Wait();
			Check("B30 the node ran the CLASS's own method (Position = Velocity * Dt)",
				Light.Position.Get() == 1.5f && Light.Steps == 1);

			// ... and submit the SAME recording at the next frame: the frame is injected at submit, so one
			// recording names two nodes. That is the whole reason recording is frame-free.
			(void)System.Advance(2);
			Check("B30 the same recording is submitted again at the next frame",
				System.Submit(Frame) == 1 && Frame.SubmitCount() == 2);
			System.Wait();
			Check("B30 ... and the class ran twice, once per frame's node",
				Light.Position.Get() == 3.0f && Light.Steps == 2);
		}
	}

	std::printf("[tg] RESULT failures=%d\n", GBackendFailures);
	std::fflush(stdout);
	return GBackendFailures == 0 ? 0 : 1;
}

}
