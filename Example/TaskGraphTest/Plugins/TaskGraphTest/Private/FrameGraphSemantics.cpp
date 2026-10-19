// Semantics test for FFrameGraph -- the node scheduler.
//
// Driven from FTaskGraphTest::Main() when MAHO_TEST_SCHED is set. It uses FFrameGraph
// DIRECTLY with closures: no FLayerBase, no FFrameBuilder, no FEngineBase loop. Going
// through those would exercise the OLD scheduler (FFrameBuilder -> FLayerTaskGraph ->
// FTaskGraph), which is precisely the thing this must NOT accidentally test.

#include <Core/FrameGraph.h>
#include <Core/Task/ThreadPool.h>
#include <Engine/Frame.h>   // MAHO_FRAME_STAGE_CHAIN / MAHO_FRAME_SELF_EDGES: the frames below declare
                            // their own structural edges (the bridge no longer invents them)

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <typeindex>
#include <vector>

namespace Maho
{

namespace
{
	int GFailures = 0;

	void Check(const char* What, bool bOk)
	{
		std::printf("  %s %s\n", bOk ? "ok  " : "FAIL", What);
		if (!bOk)
		{
			++GFailures;
		}
		std::fflush(stdout);
	}

	/** Completion rendezvous with a timeout, so a hang shows up as a FAIL. */
	struct FRendezvous
	{
		std::mutex              M;
		std::condition_variable Cv;
		int                     Remaining = 0;

		void Expect(int Count)
		{
			std::lock_guard<std::mutex> Lock(M);
			Remaining = Count;
		}

		void Arrive()
		{
			std::lock_guard<std::mutex> Lock(M);
			--Remaining;
			Cv.notify_all();
		}

		bool WaitFor(int Milliseconds)
		{
			std::unique_lock<std::mutex> Lock(M);
			return Cv.wait_for(Lock, std::chrono::milliseconds(Milliseconds),
				[this]() { return Remaining <= 0; });
		}
	};

	// A stage identity is a NAME now (a stage's `StaticName()`), not a `type_index`: the graph keys
	// nodes, edges and diagnostics by `(frame name, stage name)`, so these two tags stay names.
	const std::string_view TagA{ "A" };
	const std::string_view TagB{ "B" };

	/** Names must be string literals: FTaskKey holds a string_view.
	 *
	 *  The PHASE argument is the caller's raw identity value, and `Submit` overwrites it with the
	 *  round's own slot (`PhaseOfFrame(Frame)`) -- so it no longer carries authority, it is just
	 *  what the batch is authored with. A DEPENDENCY's phase is the caller's for real (it names
	 *  another round), which is why the cases below spell those out. */
	FTask Make(std::string_view Name, std::string_view Stage, std::int32_t Phase,
	           std::vector<FDependency> Deps, std::function<void()> Body)
	{
		FTask T;
		T.Key = FTaskKey{ Name, Stage, Phase };
		T.Dependencies = std::move(Deps);
		T.Closure = std::move(Body);
		return T;
	}

	/** A dependency IS the identity of its target instance -- no delta, no weak flag. */
	FDependency Dep(std::string_view Name, std::string_view Stage, std::int32_t Phase)
	{
		FDependency D;
		D.Target = FTaskKey{ Name, Stage, Phase };
		return D;
	}
}

int RunFrameGraphSemanticsTest()
{
	std::printf("[fg] FFrameGraph semantics test\n");

	FThreadPool Pool(4);
	FFrameGraph Graph(Pool);

	// T1: Submit before Initialize must be refused.
	{
		std::vector<FTask> Batch;
		Batch.push_back(Make("A", TagA, 0, {}, []() {}));
		std::string Reason;
		Check("T1  Submit before Initialize() is refused",
		      Graph.Submit(Batch, 0, &Reason) == false);
	}

	Graph.Initialize();
	Check("T1b scheduler thread is running", Graph.IsRunning());

	// T2: fan-out. One completion must release BOTH waiters, and only after A.
	{
		FRendezvous R;
		R.Expect(3);
		std::atomic<int> Order{ 0 };
		int AOrder = -1;
		int BOrder = -1;
		int COrder = -1;

		std::vector<FTask> Batch;
		Batch.push_back(Make("A", TagA, 0, {},
		                     [&]() { AOrder = Order.fetch_add(1); R.Arrive(); }));
		Batch.push_back(Make("B", TagB, 0, { Dep("A", TagA, 0) },
		                     [&]() { BOrder = Order.fetch_add(1); R.Arrive(); }));
		Batch.push_back(Make("C", TagA, 0, { Dep("A", TagA, 0) },
		                     [&]() { COrder = Order.fetch_add(1); R.Arrive(); }));

		Check("T2  batch accepted", Graph.Submit(Batch, 0));
		Check("T2  all three ran (both waiters released)", R.WaitFor(3000));
		Check("T2  A ran before B", AOrder >= 0 && BOrder > AOrder);
		Check("T2  A ran before C", AOrder >= 0 && COrder > AOrder);
	}

	// T3: an EXPLICIT cross-round dependency. Frame 1's P waits for frame 0's P -- which is
	// the "a layer pipelines with itself across frames" property, now spelled out as an
	// edge the bridge emits rather than as an implicit rule. P0 sets its flag only AFTER a
	// sleep, so "frame 1 saw it" can only come from the dependency actually delaying it.
	{
		FRendezvous R;
		R.Expect(2);
		std::atomic<bool> P0Done{ false };
		std::atomic<bool> SawPrevious{ false };

		std::vector<FTask> Frame0;
		Frame0.push_back(Make("P", TagA, 0, {}, [&]()
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			P0Done.store(true);
			R.Arrive();
		}));
		Check("T3  frame 0 accepted", Graph.Submit(Frame0, 0));

		std::vector<FTask> Frame1;
		Frame1.push_back(Make("P", TagA, 1, { Dep("P", TagA, 0) }, [&]()
		{
			SawPrevious.store(P0Done.load());
			R.Arrive();
		}));
		Check("T3  frame 1 accepted", Graph.Submit(Frame1, 1));
		Check("T3  both ran", R.WaitFor(3000));
		Check("T3  frame 1 waited for frame 0 to finish", SawPrevious.load());
	}

	// T4: a dependency whose target does not exist makes the EDGE disappear. The batch is
	// accepted and the node runs -- it is simply one dependency lighter. It does not need to
	// know anything about why; it only cares whether it can run.
	{
		FRendezvous R;
		R.Expect(1);

		std::vector<FTask> Batch;
		Batch.push_back(Make("W", TagA, 0, { Dep("ZZZ_DoesNotExist", TagA, 0) },
		                     [&]() { R.Arrive(); }));

		Check("T4  batch with a dangling dependency accepted", Graph.Submit(Batch, 0));
		Check("T4  node ran (the edge does not exist)", R.WaitFor(3000));
	}

	// T5: the same target declared twice must be deduplicated. Counting it twice while the
	// event is set once would leave UnmetCount at 1 forever -- a silent hang, reported here
	// as a timeout.
	{
		FRendezvous R;
		R.Expect(1);
		std::vector<FTask> Frame0;
		Frame0.push_back(Make("Q", TagA, 0, {}, [&]() { R.Arrive(); }));
		Check("T5  frame 0 accepted", Graph.Submit(Frame0, 0));
		Check("T5  frame 0 ran", R.WaitFor(3000));

		FRendezvous R2;
		R2.Expect(1);
		std::vector<FTask> Frame1;
		Frame1.push_back(Make("Q", TagA, 1,
		                      { Dep("Q", TagA, 0), Dep("Q", TagA, 0) },   // same target twice
		                      [&]() { R2.Arrive(); }));
		Check("T5  frame 1 accepted", Graph.Submit(Frame1, 1));
		Check("T5  frame 1 ran (duplicate target deduplicated)", R2.WaitFor(3000));
	}

	// T6: a throwing body must still complete its node, or its waiters wait forever.
	{
		FRendezvous R;
		R.Expect(1);

		std::vector<FTask> Batch;
		Batch.push_back(Make("Throws", TagA, 0, {},
		                     []() { throw std::runtime_error("boom"); }));
		Batch.push_back(Make("AfterThrows", TagB, 0, { Dep("Throws", TagA, 0) },
		                     [&]() { R.Arrive(); }));

		Check("T6  batch accepted", Graph.Submit(Batch, 0));
		Check("T6  downstream ran despite the upstream body throwing", R.WaitFor(3000));
	}

	// T7: structural validation. All three rejections are about the DECLARATION, never about
	// execution history.
	{
		std::string Reason;

		std::vector<FTask> Cycle;
		Cycle.push_back(Make("Cy1", TagA, 0, { Dep("Cy2", TagA, 0) }, []() {}));
		Cycle.push_back(Make("Cy2", TagA, 0, { Dep("Cy1", TagA, 0) }, []() {}));
		Check("T7  dependency cycle rejected", Graph.Submit(Cycle, 0, &Reason) == false);

		std::vector<FTask> Dup;
		Dup.push_back(Make("Dup", TagA, 0, {}, []() {}));
		Dup.push_back(Make("Dup", TagA, 0, {}, []() {}));
		Check("T7  duplicate identity rejected", Graph.Submit(Dup, 0, &Reason) == false);

		// A batch's OWN phase is not the caller's to get wrong any more: Submit stamps the round's
		// slot (`PhaseOfFrame(Frame)`) onto every task's key, so a nonsense value there is
		// normalized instead of rejected. That is what makes the phase a graph-internal vocabulary
		// and the frame the only thing a submit site has to produce.
		std::vector<FTask> Stamped;
		Stamped.push_back(Make("Stamped", TagA, 4242, {}, []() {}));
		Check("T7  a batch's phase is stamped from the round, not taken from the caller",
		      Graph.Submit(Stamped, 0));

		// Which leaves exactly one way to author an out-of-range phase: through a TARGET, whose
		// phase names ANOTHER round and is therefore left alone by that stamp.
		std::vector<FTask> BadPhase;
		BadPhase.push_back(Make("Bad", TagA, 0,
		                        { Dep("Bad", TagA, GetFramesInFlight() - 1 + 7) }, []() {}));
		Check("T7  out-of-range phase rejected", Graph.Submit(BadPhase, 0, &Reason) == false);
	}

	// T9: ONE-SIDED reverse declaration. The producer alone says "I block B", and B knows
	// nothing. The edge must still be created, so B runs after A.
	{
		FRendezvous R;
		R.Expect(2);
		std::atomic<int> Order{ 0 };
		int BlockerOrder = -1;
		int BlockedOrder = -1;

		FTask Blocker = Make("Blk9", TagA, 0, {},
		                     [&]() { BlockerOrder = Order.fetch_add(1); R.Arrive(); });
		Blocker.Blocks.push_back(Dep("Tgt9", TagB, 0));   // one-sided: Tgt9 declares nothing

		std::vector<FTask> Batch;
		Batch.push_back(Blocker);
		Batch.push_back(Make("Tgt9", TagB, 0, {},
		                     [&]() { BlockedOrder = Order.fetch_add(1); R.Arrive(); }));

		Check("T9  one-sided reverse declaration accepted", Graph.Submit(Batch, 0));
		Check("T9  both ran", R.WaitFor(3000));
		Check("T9  blocked node ran AFTER the blocker",
		      BlockerOrder >= 0 && BlockedOrder > BlockerOrder);
	}

	
	// that synchronization is built on is already set, so the waiter is satisfied by
	// definition -- "has it run" is read from the event, not interpreted.
	{
		FRendezvous R;
		R.Expect(1);
		std::vector<FTask> First;
		First.push_back(Make("L", TagA, 0, {}, [&]() { R.Arrive(); }));
		Check("T10 earlier batch accepted", Graph.Submit(First, 0));
		Check("T10 earlier batch ran", R.WaitFor(3000));

		FRendezvous R2;
		R2.Expect(1);
		std::vector<FTask> Second;
		Second.push_back(Make("UsesL", TagB, 0, { Dep("L", TagA, 0) },
		                      [&]() { R2.Arrive(); }));
		Check("T10 later batch accepted", Graph.Submit(Second, 0));
		Check("T10 later batch ran without waiting (target already completed)",
		      R2.WaitFor(3000));
	}

	// T11: Submit performs the ring-reuse admission ITSELF -- a submit whose round names a slot
	// that is still busy BLOCKS THE CALLER until that slot drains. This deliberately exercises the
	// edge of the "one submit per phase per occupancy" contract: submitting the SAME round a second
	// time is exactly the case that blocks (the builder's monotonic floor exists to keep a sequence
	// from doing that by accident).
	{
		FRendezvous R;
		R.Expect(1);

		std::vector<FTask> Slow;
		Slow.push_back(Make("Slow11", TagA, 1, {}, [&]()
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
			R.Arrive();
		}));
		Check("T11 slow node accepted", Graph.Submit(Slow, 1));

		// Give the scheduler a moment to actually DISPATCH it, so the phase is genuinely busy
		// rather than merely queued: Submit only enqueues a command, and a check made before
		// the dispatch would see an idle phase (that gap is why IsPhaseIdle is not part of the
		// driving surface).
		std::this_thread::sleep_for(std::chrono::milliseconds(60));

		FRendezvous R2;
		R2.Expect(1);
		std::vector<FTask> Again;
		Again.push_back(Make("Slow11", TagA, 1, {}, [&]() { R2.Arrive(); }));

		const auto Start = std::chrono::steady_clock::now();
		const bool bAccepted = Graph.Submit(Again, 1);
		const auto Elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - Start).count();

		Check("T11 second submit accepted", bAccepted);
		Check("T11 Submit blocked until the busy phase drained", Elapsed >= 100);
		// No "is the phase idle now" check: the second Submit RETURNED, and by the admission
		// contract that already means the phase had drained. A query would add nothing.
		Check("T11 both runs happened", R.WaitFor(3000) && R2.WaitFor(3000));
	}

	// T12: FFrameExtension -- the declaration layer. Pure data, no scheduler involved. This is also
	// the only place the sugar's templates get INSTANTIATED, which is what actually checks
	// them (an uninstantiated class template is barely compiled).
	{
		MAHO_DECLARE_STAGE_TAG(FStageOne)
		MAHO_DECLARE_STAGE_TAG(FStageTwo)
		struct FA : FFrameExtension
		{
			static std::string_view StaticName() { return "FA"; }
			std::string_view GetName() const override { return StaticName(); }
		};
		struct FB : FFrameExtension
		{
			static std::string_view StaticName() { return "FB"; }
			std::string_view GetName() const override { return StaticName(); }
		};

		FA A;
		A.MyStage<FStageOne>().IsWaiting<FB>().ForStage<FStageTwo>();
		A.MyStage<FStageOne>().IsWaiting<FB>().ForStage<FStageTwo>(-1);
		A.MyStage<FStageOne>().IsBlocking<FB>().OnStage<FStageTwo>();
		// Name-addressed: for a consumer that must NOT name the producer's type (naming it would
		// force a build dependency on an optional plugin).
		A.MyStage<FStageTwo>().IsWaiting("FB").ForStage<FStageTwo>();
		A.MyStage<FStageTwo>().IsBlocking("FB").OnStage<FStageTwo>();

		const auto& Deps = A.GetDependencies();
		Check("T12 two stages carry declarations", Deps.size() == 2);
		Check("T12 two waits on that stage", Deps.at(FStageOne::StaticName()).size() == 2);
		Check("T12 first wait has offset 0", Deps.at(FStageOne::StaticName())[0].FrameOffset == 0);
		Check("T12 the offset argument gives offset -1", Deps.at(FStageOne::StaticName())[1].FrameOffset == -1);
		Check("T12 target name is the TARGET frame", Deps.at(FStageOne::StaticName())[0].TargetName == "FB");
		Check("T12 target stage recorded",
		      Deps.at(FStageOne::StaticName())[0].TargetStage == FStageTwo::StaticName());
		Check("T12 name-addressed wait records the name it was given",
		      Deps.at(FStageTwo::StaticName()).size() == 1
		      && Deps.at(FStageTwo::StaticName())[0].TargetName == "FB"
		      && Deps.at(FStageTwo::StaticName())[0].TargetStage == FStageTwo::StaticName()
		      && Deps.at(FStageTwo::StaticName())[0].FrameOffset == 0);

		const auto& Blocks = A.GetDependents();
		Check("T12 one-sided reverse declaration recorded", Blocks.size() == 2);
		Check("T12 reverse declaration keeps its own stage",
		      Blocks[0].first == FStageOne::StaticName());
		Check("T12 name-addressed reverse records its own stage and the name",
		      Blocks[1].first == FStageTwo::StaticName()
		      && Blocks[1].second.TargetName == "FB"
		      && Blocks[1].second.TargetStage == FStageTwo::StaticName());
	}

	// T15: the canonical sugar -- WaitFor/BlockOn with the RELATIVE FRAME OFFSET in the stage
	// selector (0 = this round, -1 = the previous one, -K = one full window back). One selector,
	// one argument: IsWaiting/IsBlocking are still the two directions (aliases of WaitFor/BlockOn).
	{
		MAHO_DECLARE_STAGE_TAG(IStageA)
		MAHO_DECLARE_STAGE_TAG(IStageB)

		struct FSugarTarget : FFrameExtension
		{
			static std::string_view StaticName() { return "SugarTarget"; }
			std::string_view GetName() const override { return StaticName(); }
		};
		struct FSugar : FFrameExtension
		{
			static std::string_view StaticName() { return "SugarDeclarer"; }
			std::string_view GetName() const override { return StaticName(); }
		};

		FSugar S;
		S.MyStage<IStageA>().WaitFor<FSugarTarget>().OnStage<IStageB>();             // this round
		S.MyStage<IStageA>().WaitFor<FSugarTarget>().OnStage<IStageB>(-1);          // one round back
		S.MyStage<IStageA>().WaitFor("SugarTarget").OnStage<IStageB>(-1);           // by NAME, one back
		S.MyStage<IStageA>().BlockOn<FSugarTarget>().OnStage<IStageB>();            // reverse, this round
		S.MyStage<IStageA>().BlockOn<FSugarTarget>().OnStage<IStageB>(-1);          // reverse, one back
		S.MyStage<IStageB>().WaitFor<FSugarTarget>().OnStage<IStageB>(-1);          // another stage

		const auto& Deps = S.GetDependencies();
		const auto& Blocks = S.GetDependents();
		Check("T15 WaitFor().OnStage() records a same-frame edge",
		      Deps.at(IStageA::StaticName())[0].FrameOffset == 0
		      && Deps.at(IStageA::StaticName())[0].TargetName == "SugarTarget"
		      && Deps.at(IStageA::StaticName())[0].TargetStage == IStageB::StaticName());
		Check("T15 WaitFor().OnStage(-1) records a cross-frame edge",
		      Deps.at(IStageA::StaticName())[1].FrameOffset == -1
		      && Deps.at(IStageA::StaticName())[1].TargetStage == IStageB::StaticName());
		Check("T15 WaitFor(\"name\").OnStage(-1) works by name",
		      Deps.at(IStageA::StaticName())[2].FrameOffset == -1
		      && Deps.at(IStageA::StaticName())[2].TargetName == "SugarTarget");
		Check("T15 BlockOn().OnStage() records a same-frame reverse edge",
		      Blocks[0].first == IStageA::StaticName() && Blocks[0].second.FrameOffset == 0
		      && Blocks[0].second.TargetName == "SugarTarget");
		Check("T15 BlockOn().OnStage(-1) records a cross-frame reverse edge",
		      Blocks[1].first == IStageA::StaticName() && Blocks[1].second.FrameOffset == -1);
		Check("T15 the same selector works from another stage",
		      Deps.at(IStageB::StaticName()).size() == 1
		      && Deps.at(IStageB::StaticName())[0].FrameOffset == -1);
	}


	// exactly what makes it inspectable here: every resolution is checked as DATA before the
	// graph ever sees it.
	// T16: the sugar's cross-frame edge really BLOCKS. That is the whole point of the relative
	// frame offset: the only cross-frame ordering a frame extension gets for free is its own SELF
	// edge (a stage against ITSELF, one frame back -- and that one is a declaration too, installed
	// by MAHO_DECLARE_FRAME), so ordering stage Q of frame N+1 after stage P of frame N has to be
	// explicit -- and "explicit" has to mean it waits, not just that it is recorded.
	//
	// Each frame below mounts EXACTLY ONE stage, and that is deliberate: the identity the
	// declarations rest on is "the frame's list IS what it implements" (a node exists iff the frame
	// mounts the stage), so the dispatch here must name one stage per frame. A frame that mounted
	// both would also declare the chain P->Q INSIDE its own frame, and then Q@10 would run after
	// P@10 -- which sets the flag the assertion reads, so the assertion would pass through the
	// chain and no longer be about the explicit edge at all.
	//
	// Two consecutive frames, both built by the BRIDGE (so the sugar's resolution is exercised, not
	// just FEdge's raw form): frame 10's P sleeps before setting a flag, and frame 11's Q declares
	// `WaitFor<FPrev>().OnStage<IStageP>(-1)` -- so Q has to observe that flag.
	{
		MAHO_DECLARE_STAGE_TAG(IStageP)
		MAHO_DECLARE_STAGE_TAG(IStageQ)
		using FSugarStages = TTypeList<IStageP, IStageQ>;

		struct FPrev : FFrameExtension, IPipeline<IStageP>
		{
			FPrev() { MAHO_FRAME_STAGE_CHAIN(FPrev); MAHO_FRAME_SELF_EDGES(FPrev); }
			static std::string_view StaticName() { return "SugarPrev"; }
			std::string_view GetName() const override { return StaticName(); }
		};
		struct FNext : FFrameExtension, IPipeline<IStageQ>
		{
			FNext() { MAHO_FRAME_STAGE_CHAIN(FNext); MAHO_FRAME_SELF_EDGES(FNext); }
			static std::string_view StaticName() { return "SugarNext"; }
			std::string_view GetName() const override { return StaticName(); }
		};

		FRendezvous R;
		// Every (frame in the set) x (stage) of BOTH rounds arrives: 2 frames x 1 stage x 2 rounds.
		// It has to be all of them -- the assertion below is about the EXPLICIT cross-frame edge
		// (Q@11 after P@10), and "some Q saw a finished P" is only meaningful once every node that
		// could set it has run.
		R.Expect(4);
		std::atomic<bool> PrevDone{ false };
		std::atomic<bool> SawPrev{ false };

		struct FSugarDispatch : FFrameBridge::IDispatch
		{
			FRendezvous*       InR = nullptr;
			std::atomic<bool>* InDone = nullptr;
			std::atomic<bool>* InSaw = nullptr;

			bool Implements(const FFrameExtension& Frame, std::string_view Stage) const override
			{
				// One stage per frame, matching each frame's OWN mounted list: the bridge's structural
				// self-check asserts that a node carries its frame's declarations, which can only hold
				// when "what the dispatch says the frame implements" IS "what the frame mounts".
				return Frame.GetName() == FPrev::StaticName() ? Stage == IStageP::StaticName()
				                                              : Stage == IStageQ::StaticName();
			}
			std::function<void()> MakeClosure(FFrameExtension& Frame, std::string_view Stage,
				std::uint64_t Round) override
			{
				if (Stage == IStageP::StaticName())
				{
					return [Done = InDone, R = InR]()
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(250));
						Done->store(true);
						R->Arrive();
					};
				}
				return [Saw = InSaw, Done = InDone, R = InR]()
				{
					// Monotone: the flag says "some Q instance ran after a P completed", which is
					// exactly what the explicit edge is supposed to guarantee.
					if (Done->load()) { Saw->store(true); }
					R->Arrive();
				};
			}
		};
		FSugarDispatch Dispatch;
		Dispatch.InR = &R;
		Dispatch.InDone = &PrevDone;
		Dispatch.InSaw = &SawPrev;

		FPrev Prev;
		FNext Next;
		// The declaring side names the PRODUCER's frame and stage -- and the round before it.
		Next.MyStage<IStageQ>().WaitFor<FPrev>().OnStage<IStageP>(-1);

		std::vector<FFrameExtension*> Frames{ &Prev, &Next };
		auto Frame10 = FFrameBridge::Build(Frames, StageIndicesOf(FSugarStages{}), 10, Dispatch);
		auto Frame11 = FFrameBridge::Build(Frames, StageIndicesOf(FSugarStages{}), 11, Dispatch);
		Check("T16 both frames expand into their stages", Frame11.Tasks.size() == 2);

		// The resolution, asserted as data: frame 11's Q depends on frame 10's P -- i.e. the offset
		// went to the PREVIOUS frame's phase, not to this one's.
		bool bEdgeResolved = false;
		for (const FTask& T : Frame11.Tasks)
		{
			if (T.Key.Name != "SugarNext" || T.Key.Stage != IStageQ::StaticName())
			{
				continue;
			}
			for (const FDependency& D : T.Dependencies)
			{
				if (D.Target.Name == "SugarPrev" && D.Target.Stage == IStageP::StaticName()
					&& D.Target.Phase == FFrameBridge::PhaseOf(10))
				{
					bEdgeResolved = true;
				}
			}
		}
		Check("T16 the cross-frame edge resolved onto the PREVIOUS frame's phase", bEdgeResolved);

		Check("T16 frame 10 accepted", Graph.Submit(std::move(Frame10.Tasks), 10));
		Check("T16 frame 11 accepted", Graph.Submit(std::move(Frame11.Tasks), 11));
		Check("T16 all four nodes ran", R.WaitFor(3000));
		Check("T16 frame 11's Q really waited for frame 10's P", SawPrev.load());
	}


	// exactly what makes it inspectable here: every resolution is checked as DATA before the
	// graph ever sees it.
	// T13: FFrameBridge -- declarations to a batch of FTask. It BUILDS (never submits), which is
	// exactly what makes it inspectable here: every resolution is checked as DATA before the
	// graph ever sees it.
	{
		MAHO_DECLARE_STAGE_TAG(IStageOne)
		MAHO_DECLARE_STAGE_TAG(IStageTwo)
		MAHO_DECLARE_STAGE_TAG(IStageThree)
		using FStages = TTypeList<IStageOne, IStageTwo, IStageThree>;
		const std::array<std::string_view, 3> Stages = StageIndicesOf(FStages{});

		struct FFrameA : FFrameExtension, IPipeline<IStageOne, IStageTwo>
		{
			FFrameA()
			{
				// The frame's OWN structural edges (the in-frame chain and the per-stage self edge).
				// The bridge used to emit both; now they are declarations like any other, which is
				// what the counts below still add up to.
				MAHO_FRAME_STAGE_CHAIN(FFrameA);
				MAHO_FRAME_SELF_EDGES(FFrameA);
			}
			static std::string_view StaticName() { return "BridgeA"; }
			std::string_view GetName() const override { return StaticName(); }
		};
		struct FFrameB : FFrameExtension, IPipeline<IStageTwo>
		{
			FFrameB() { MAHO_FRAME_STAGE_CHAIN(FFrameB); MAHO_FRAME_SELF_EDGES(FFrameB); }
			static std::string_view StaticName() { return "BridgeB"; }
			std::string_view GetName() const override { return StaticName(); }
		};
		struct FMissingFrame : FFrameExtension
		{
			static std::string_view StaticName() { return "BridgeMissing"; }
			std::string_view GetName() const override { return StaticName(); }
		};

		// A implements one and two; B implements only two. Nothing implements three.
		struct FStaticDispatch : FFrameBridge::IDispatch
		{
			bool Implements(const FFrameExtension& Frame, std::string_view Stage) const override
			{
				if (Frame.GetName() == FFrameA::StaticName())
				{
					return Stage == IStageOne::StaticName() || Stage == IStageTwo::StaticName();
				}
				return Stage == IStageTwo::StaticName();
			}
			std::function<void()> MakeClosure(FFrameExtension&, std::string_view, std::uint64_t) override
			{
				return []() {};
			}
		};
		FStaticDispatch StaticDispatch;

		auto FindTask = [](const std::vector<FTask>& Tasks, std::string_view Name,
		                   std::string_view Stage) -> const FTask*
		{
			for (const FTask& T : Tasks)
			{
				if (T.Key.Name == Name && T.Key.Stage == Stage) { return &T; }
			}
			return nullptr;
		};

		// Order-independent dependency lookups: the batch's dependency order follows the frame's
		// declaration order (its structural edges included), which is not this test's subject -- so
		// index into Dependencies would bake an internal order into an assertion about resolution.
		auto HasDep = [](const FTask* T, std::string_view Name, std::string_view Stage,
		                 std::int32_t Phase)
		{
			if (T == nullptr) { return false; }
			for (const FDependency& D : T->Dependencies)
			{
				if (D.Target.Name == Name && D.Target.Stage == Stage && D.Target.Phase == Phase)
				{
					return true;
				}
			}
			return false;
		};
		auto HasDepKey = [](const FTask* T, const FTaskKey& Key)
		{
			if (T == nullptr) { return false; }
			for (const FDependency& D : T->Dependencies)
			{
				if (D.Target == Key) { return true; }
			}
			return false;
		};

		Check("T13 stage sequence stays in order", Stages[0] == IStageOne::StaticName()
		      && Stages[1] == IStageTwo::StaticName() && Stages[2] == IStageThree::StaticName());

		// ── structure: what the declarations resolve to, checked as data ──────────
		{
			FFrameA A;
			FFrameB B;
			A.MyStage<IStageOne>().IsWaiting<FFrameB>().ForStage<IStageTwo>();              // same round
			A.MyStage<IStageOne>().IsWaiting<FFrameB>().ForStage<IStageTwo>(-1);            // previous
			A.MyStage<IStageOne>().IsBlocking<FFrameB>().OnStage<IStageTwo>();              // reverse

			std::vector<FFrameExtension*> Both{ &A, &B };
			const auto Built = FFrameBridge::Build(Both, Stages, 7, StaticDispatch);

			Check("T13 one node per implemented stage only", Built.Tasks.size() == 3);
			Check("T13 unimplemented stage produces no node",
			      FindTask(Built.Tasks, "BridgeA", IStageThree::StaticName()) == nullptr
			      && FindTask(Built.Tasks, "BridgeB", IStageThree::StaticName()) == nullptr);
			Check("T13 every node carries the round's own phase (PhaseOf(7))",
			      Built.Tasks[0].Key.Phase == FFrameBridge::PhaseOf(7)
			      && Built.Tasks[1].Key.Phase == FFrameBridge::PhaseOf(7)
			      && Built.Tasks[2].Key.Phase == FFrameBridge::PhaseOf(7));

			const FTask* AOne = FindTask(Built.Tasks, "BridgeA", IStageOne::StaticName());
			const FTask* ATwo = FindTask(Built.Tasks, "BridgeA", IStageTwo::StaticName());
			Check("T13 forward declarations hang on the declarer's node",
			      AOne != nullptr && AOne->Dependencies.size() == 3);   // 2 declared + the self edge
			Check("T13 offset 0 resolves to this frame's phase",
			      HasDep(AOne, "BridgeB", IStageTwo::StaticName(), FFrameBridge::PhaseOf(7)));
			Check("T13 offset -1 resolves to the previous frame's phase",
			      HasDep(AOne, "BridgeB", IStageTwo::StaticName(), FFrameBridge::PhaseOf(6)));
			Check("T13 the same stage of the PREVIOUS frame is implied, never declared",
			      HasDep(AOne, "BridgeA", IStageOne::StaticName(), FFrameBridge::PhaseOf(6)));

			// FTask::Blocks reads "these are blocked BY me", so the entry belongs on the DECLARER
			// and names the target -- putting it on the target would invert the edge.
			Check("T13 reverse declaration is written on the DECLARER's node",
			      AOne != nullptr && AOne->Blocks.size() == 1);
			Check("T13 reverse declaration names the blocked target",
			      AOne != nullptr && AOne->Blocks[0].Target.Name == "BridgeB"
			      && AOne->Blocks[0].Target.Stage == IStageTwo::StaticName()
			      && AOne->Blocks[0].Target.Phase == FFrameBridge::PhaseOf(7));
			Check("T13 the stage chain links a frame's own stages",
			      ATwo != nullptr && HasDepKey(ATwo, AOne->Key));
			// The cross-frame edge is PER STAGE, not per frame: a mid-chain node still carries its
			// own edge to the previous frame. (Serializing a whole frame against its previous one
			// would hide a missing per-frame context in the render layer -- see FFrameBridge.)
			Check("T13 a chained node carries its own cross-frame self edge too",
			      HasDep(ATwo, "BridgeA", IStageTwo::StaticName(), FFrameBridge::PhaseOf(6)));
			Check("T13 the chained node declares no reverse edge of its own",
			      ATwo != nullptr && ATwo->Blocks.empty());
			Check("T13 no diagnostics for a well-formed batch", Built.Diagnostics.empty());
		}

		// ── the chain crosses a stage the frame does not MOUNT ────────────────────
		//
		// The pruning rule (no node for an unimplemented stage) must not cut a frame's chain in two.
		// The chain is declared over the frame's OWN stage list, so a stage the frame does not mount
		// simply has no pair to interrupt: mounting One and Three chains them DIRECTLY, and the
		// sequence's middle stage contributes no node at all. (Before the declarations moved into
		// the frame, this was spelled "the chain follows the EMITTED nodes"; it is the same rule
		// seen from the side that owns the list.)
		{
			struct FFrameGap : FFrameExtension, IPipeline<IStageOne, IStageThree>
			{
				FFrameGap() { MAHO_FRAME_STAGE_CHAIN(FFrameGap); MAHO_FRAME_SELF_EDGES(FFrameGap); }
				static std::string_view StaticName() { return "BridgeGap"; }
				std::string_view GetName() const override { return StaticName(); }
			};
			struct FGapDispatch : FFrameBridge::IDispatch
			{
				bool Implements(const FFrameExtension&, std::string_view Stage) const override
				{
					return Stage == IStageOne::StaticName() || Stage == IStageThree::StaticName();
				}
				std::function<void()> MakeClosure(FFrameExtension&, std::string_view, std::uint64_t) override
				{
					return []() {};
				}
			};
			FGapDispatch GapDispatch;
			FFrameGap Gap;
			std::vector<FFrameExtension*> Just{ &Gap };
			const auto Built = FFrameBridge::Build(Just, Stages, 7, GapDispatch);

			const FTask* GapOne = FindTask(Built.Tasks, "BridgeGap", IStageOne::StaticName());
			const FTask* GapThree = FindTask(Built.Tasks, "BridgeGap", IStageThree::StaticName());
			Check("T13 a skipped stage produces no node", Built.Tasks.size() == 2);
			Check("T13 the frame's first emitted stage has only its self edge",
			      GapOne != nullptr && GapOne->Dependencies.size() == 1
			      && HasDep(GapOne, "BridgeGap", IStageOne::StaticName(), FFrameBridge::PhaseOf(6)));
			Check("T13 the chain crosses the unmounted stage (no empty node needed)",
			      HasDepKey(GapThree, GapOne != nullptr ? GapOne->Key : FTaskKey{}));
		}

		// ── diagnostics: reported, but the edge is still emitted ──────────────────
		//
		// What is reported is an AUTHORING error THIS BATCH CAN PROVE -- a name that is not in the
		// frame set, a frame that does not implement a stage that IS in this sequence. What is NOT
		// reported is (a) a target that is merely at another PHASE: a target one frame back was built
		// by the previous Build call, and a target at this phase may come from an earlier batch (a
		// one-shot node keeps its identity), so its absence from this batch is expected; and (b) a
		// target STAGE this batch does not drive: `Implements` only answers inside THIS batch's
		// sequence, so such a stage is skipped as another batch's declaration (it has no node here and
		// this batch cannot know whether the batch owning that stage can bind it). Reporting (b) fired
		// on every legal cross-batch declaration -- a frame whose stage list spans several batches
		// declares its in-frame chain across them.
		{
			MAHO_DECLARE_STAGE_TAG(IStageFour)   // deliberately NOT part of FStages, and mounted by nothing at all

			FFrameA Dangler;
			FFrameB Present;
			Dangler.MyStage<IStageTwo>().IsWaiting<FMissingFrame>().ForStage<IStageTwo>();
			Dangler.MyStage<IStageTwo>().IsWaiting<FFrameB>().ForStage<IStageFour>();
			Dangler.MyStage<IStageTwo>().IsWaiting<FFrameB>().ForStage<IStageThree>();
			Dangler.MyStage<IStageThree>().IsWaiting<FFrameB>().ForStage<IStageTwo>();

			std::vector<FFrameExtension*> Just{ &Dangler, &Present };
			const auto Built = FFrameBridge::Build(Just, Stages, 7, StaticDispatch);

			const FTask* DanglerTwo = FindTask(Built.Tasks, "BridgeA", IStageTwo::StaticName());
			Check("T13 a target outside the frame set still emits the edge",
			      DanglerTwo != nullptr && DanglerTwo->Dependencies.size() == 4);   // 2 + chain + self
			Check("T13 the emitted edge names the missing target",
			      HasDep(DanglerTwo, "BridgeMissing", IStageTwo::StaticName(), FFrameBridge::PhaseOf(7)));
			// The declaration at a stage this batch does not drive left NO trace: no diagnostic (it is
			// the other batch's to judge) and no edge (this batch has no node to land on).
			Check("T13 a target stage outside this sequence emits no edge",
			      !HasDep(DanglerTwo, "BridgeB", IStageFour::StaticName(), FFrameBridge::PhaseOf(7)));

			bool bSawFrameSet = false;
			bool bSawStageNotInSeq = false;
			bool bSawNotImplemented = false;
			bool bSawDeclarerStage = false;
			for (const FFrameBridge::FDiagnostic& Diag : Built.Diagnostics)
			{
				if (Diag.Reason == "target frame is not in this frame set") { bSawFrameSet = true; }
				if (Diag.Reason == "target stage is not in the stage sequence") { bSawStageNotInSeq = true; }
				if (Diag.Reason == "target frame does not implement that stage") { bSawNotImplemented = true; }
				if (Diag.Reason == "stage not implemented by this frame") { bSawDeclarerStage = true; }
			}
			Check("T13 unknown target frame reported", bSawFrameSet);
			Check("T13 a target stage outside THIS sequence is the owning batch's business, not reported",
			      !bSawStageNotInSeq);
			Check("T13 target frame not implementing the stage reported", bSawNotImplemented);
			Check("T13 declaration on an unimplemented stage reported", bSawDeclarerStage);
			Check("T13 each problem reported exactly once, and nothing else",
			      Built.Diagnostics.size() == 3);
		}

		// ── end to end: the bridge's batch actually orders the graph ──────────────
		{
			struct FFrameC : FFrameExtension, IPipeline<IStageOne>
			{
				FFrameC() { MAHO_FRAME_STAGE_CHAIN(FFrameC); MAHO_FRAME_SELF_EDGES(FFrameC); }
				static std::string_view StaticName() { return "BridgeC"; }
				std::string_view GetName() const override { return StaticName(); }
			};
			struct FFrameD : FFrameExtension, IPipeline<IStageTwo>
			{
				FFrameD() { MAHO_FRAME_STAGE_CHAIN(FFrameD); MAHO_FRAME_SELF_EDGES(FFrameD); }
				static std::string_view StaticName() { return "BridgeD"; }
				std::string_view GetName() const override { return StaticName(); }
			};

			FRendezvous R;
			R.Expect(2);
			std::atomic<bool> Done{ false };
			std::atomic<bool> SawDone{ false };

			// One node each, so "who ran" is unambiguous.
			struct FSoloDispatch : FFrameBridge::IDispatch
			{
				FRendezvous*       InR = nullptr;
				std::atomic<bool>* InDone = nullptr;
				std::atomic<bool>* InSaw = nullptr;

				bool Implements(const FFrameExtension& Frame, std::string_view Stage) const override
				{
					return Frame.GetName() == FFrameC::StaticName()
						? Stage == IStageOne::StaticName()   // C declares, and does nothing else
						: Stage == IStageTwo::StaticName();  // D is the producer
				}
				std::function<void()> MakeClosure(FFrameExtension& Frame, std::string_view,
					std::uint64_t Round) override
				{
					if (Frame.GetName() == FFrameC::StaticName())
					{
						return [Saw = InSaw, Done = InDone, R = InR]()
						{
							Saw->store(Done->load());
							R->Arrive();
						};
					}
					return [Done = InDone, R = InR]()
					{
						// Only a real edge can make C see this: both nodes are dispatched
						// immediately and the pool has spare threads, so without the edge C would
						// run alongside this sleep and read false.
						std::this_thread::sleep_for(std::chrono::milliseconds(250));
						Done->store(true);
						R->Arrive();
					};
				}
			};
			FSoloDispatch SoloDispatch;
			SoloDispatch.InR = &R;
			SoloDispatch.InDone = &Done;
			SoloDispatch.InSaw = &SawDone;

			FFrameD D;
			FFrameC C;
			// A same-frame edge (resolvable inside the batch) and a cross-frame one (resolvable
			// only if the previous frame's node exists). At frame 7 the latter names frame 6,
			// which no Build call has produced -- so phase absence must NOT be reported.
			C.MyStage<IStageOne>().WaitFor<FFrameD>().OnStage<IStageTwo>();
			C.MyStage<IStageOne>().WaitFor<FFrameD>().OnStage<IStageTwo>(-1);

			std::vector<FFrameExtension*> JustD{ &D };
			const auto OnlyD = FFrameBridge::Build(JustD, Stages, 7, SoloDispatch);
			Check("T13 a batch holds only the frames it was given",
			      OnlyD.Tasks.size() == 1 && OnlyD.Diagnostics.empty());

			std::vector<FFrameExtension*> Both{ &C, &D };
			const auto Built = FFrameBridge::Build(Both, Stages, 7, SoloDispatch);
			Check("T13 a batch is the frame set x the stages it implements", Built.Tasks.size() == 2);
			Check("T13 phase absence is not reported", Built.Diagnostics.empty());

			const FTask* COne = FindTask(Built.Tasks, "BridgeC", IStageOne::StaticName());
			Check("T13 both declared edges resolved, at two different phases",
			      COne != nullptr && COne->Dependencies.size() == 3   // 2 declared + the self edge
			      && HasDep(COne, "BridgeD", IStageTwo::StaticName(), FFrameBridge::PhaseOf(7))
			      && HasDep(COne, "BridgeD", IStageTwo::StaticName(), FFrameBridge::PhaseOf(6)));

			Check("T13 batch accepted", Graph.Submit(std::move(Built.Tasks), 7));
			Check("T13 both nodes ran", R.WaitFor(3000));
			Check("T13 the bridge-built same-frame edge actually delayed the waiter",
			      SawDone.load());
		}
	}

	// T14: Wait() is a SUBMISSION FENCE (vkQueueSubmit + fence), not an "is anything in flight"
	// query. The distinction is the whole reason the fence count is raised by the CALLER before
	// the command is enqueued: the phase in-flight counts rise at DISPATCH time, so they read
	// zero in the window between Submit() returning and the scheduler picking the command up.
	// Waiting on THOSE returns immediately -- and the next frame then dispatches side by side
	// with this one, which is exactly what this test caught in the engine loop.
	{
		FRendezvous R;
		R.Expect(1);
		std::atomic<bool> Ran{ false };

		std::vector<FTask> Batch;
		Batch.push_back(Make("Fence14", TagA, 0, {}, [&]()
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(150));
			Ran.store(true);
			R.Arrive();
		}));

		Check("T14 batch accepted", Graph.Submit(Batch, 0));
		Graph.Wait();
		// Deliberately no rendezvous wait before the check: an idle-query Wait() would have
		// returned at once, with Ran still false.
		Check("T14 Wait() returned only after the submitted node had run", Ran.load());
		Check("T14 the node ran exactly once", R.WaitFor(3000));
	}

	// T8: Wait() drains everything. Its RETURNING is the assertion -- its contract is "everything
	// I submitted has completed", so if anything were still running this would block and
	// the harness would report a HUNG process. The time bound only makes "it did not hang"
	// explicit. (No per-phase query is needed, which is why those are not on the surface.)
	const auto WaitStart = std::chrono::steady_clock::now();
	Graph.Wait();
	const auto WaitMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - WaitStart).count();
	Check("T8  Wait() returned, so the graph drained", WaitMs < 5000);

	// T17: a PERSISTENT lane is served by its OWN dedicated thread -- shared workers never take from
	// it, and every task on it runs on that one thread (this is the property thread affinity needs).
	{
		const FThreadPool::FLane Pinned = Pool.CreatePersistentLane("T17Pinned");
		Check("T17 a persistent lane was created", Pinned != FThreadPool::InvalidLane);

		std::atomic<std::thread::id> PinnedTid{};
		std::atomic<int> OnPinned{ 0 };
		std::atomic<bool> bSameThread{ true };
		for (int I = 0; I < 4; ++I)
		{
			Pool.Submit(Pinned, [&Pool, Pinned, &PinnedTid, &OnPinned, &bSameThread]()
				{
					const std::thread::id Mine = std::this_thread::get_id();
					std::thread::id Expected = PinnedTid.load();
					if (Expected == std::thread::id{})
					{
						PinnedTid.store(Mine);
						Expected = Mine;
					}
					if (Expected != Mine) { bSameThread.store(false); }
					if (!Pool.IsCurrentThread(Pinned)) { bSameThread.store(false); }
					OnPinned.fetch_add(1);
				});
		}
		Pool.Flush(Pinned);
		Check("T17 all four tasks ran", OnPinned.load() == 4);
		Check("T17 every task ran on ONE dedicated thread, and it knows it is that thread",
		      bSameThread.load());

		Pool.DestroyPersistentLane(Pinned);
		Check("T17 the lane is released (its thread is gone)", !Pool.IsCurrentThread(Pinned));
	}

	std::printf("[fg] RESULT failures=%d\n", GFailures);
	std::fflush(stdout);

	Graph.Shutdown();
	return GFailures == 0 ? 0 : 1;
}

} // namespace Maho
