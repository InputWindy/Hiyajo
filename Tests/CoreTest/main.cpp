// Maho core demo — a scheduler, a few Layers wired with dependencies, leveled
// parallel dispatch. Runs a real layered system with a thread pool.
//
//   Layers declare deps via MAHO_EXTEND_DEPS. A scheduler (FParallelScheduler,
//   parameterized over its extension scan table) holds the Layer instances.
//   MAHO_SORT_LEVELS computes the closure-level bands; each band runs in
//   parallel (barrier between bands).
//
#include <Maho.h>
#include <Engine/ParallelScheduler.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <vector>

using namespace Maho;

// ── interfaces (Query Select bases + dependency Keys) ──
struct IRender { virtual void Render() = 0; };
struct IPhysics { virtual void Step() = 0; };
struct IAudio { virtual void Play() = 0; };

// ── concrete Layer base: satisfies every pure virtual of IAssembly ──
struct FLayerBase : IAssembly
{
	void Initialize(int, char**) override {}
	void Shutdown() override {}
	int Main(int, char**) override { return 0; }
};

// ── Layers, each a singleton-free Assembly + a set of interfaces. Deps via the
//    MAHO_EXTEND_DEPS macro (Key, Parent, extras...) — FNoParent = root edge. ──
struct FWindow : FLayerBase, TPlug<IRender>
{
	void Render() override {}
	MAHO_EXTEND_DEPS((IRender, FNoParent));                       // root
};
struct FScene : FLayerBase, TPlug<IRender>
{
	void Render() override {}
	MAHO_EXTEND_DEPS((IRender, FNoParent, FWindow));              // deps FWindow
};
struct FPhysics : FLayerBase, TPlug<IPhysics>
{
	void Step() override {}
	MAHO_EXTEND_DEPS((IPhysics, FNoParent, FWindow));             // deps FWindow
};
struct FAudio : FLayerBase, TPlug<IAudio>
{
	void Play() override {}
	MAHO_EXTEND_DEPS((IAudio, FNoParent));                        // root
};
struct FPlayer : FLayerBase, TPlug<IRender, IPhysics>
{
	void Render() override {}
	void Step() override {}
	MAHO_EXTEND_DEPS(
		(IPhysics, FNoParent, FPhysics),                          // deps FPhysics
		(IRender, FNoParent, FScene));                            // deps FScene
};

// ── the app: a scheduler owning the extension scan table + the instances ──
using FLayerTypes = TTypeList<FWindow, FScene, FPhysics, FAudio, FPlayer>;
using FSched = Parallel::FParallelScheduler<FLayerTypes>;

// visitor counting each concrete layer type hit during dispatch
struct FCountVisitor
{
	std::atomic<int>& Out;
	void operator()(FWindow&) const { Out.fetch_add(1, std::memory_order_relaxed); }
	void operator()(FScene&) const { Out.fetch_add(1, std::memory_order_relaxed); }
	void operator()(FPhysics&) const { Out.fetch_add(1, std::memory_order_relaxed); }
	void operator()(FAudio&) const { Out.fetch_add(1, std::memory_order_relaxed); }
	void operator()(FPlayer&) const { Out.fetch_add(1, std::memory_order_relaxed); }
};

// the closure-level bands come from each Layer's deps (via code-gen in the real
// engine; here we build them explicitly from the declared FDependsPack).
int main()
{
	FSched Sched;

	// runtime instances, scrambled on purpose
	FWindow W; FScene S; FPhysics P; FAudio A; FPlayer Pl;
	std::vector<IAssembly*> Insts = { &Pl, &A, &W, &P, &S };

	// ① dispatch every IRender layer exactly once (runtime-type dispatch)
	std::atomic<int> RenderCalls{ 0 };
	auto OnRender = [&](IRender&) { RenderCalls.fetch_add(1, std::memory_order_relaxed); };
	Sched.Execute<TTypeList<FWindow, FScene, FPlayer>>(Insts, OnRender);

	// ② live dispatch to EVERY instance in the scan list (dispatch by concrete
	//    type among FLayerTypes); each runs its overload.
	std::atomic<int> TotalCalls{ 0 };
	FCountVisitor V{ TotalCalls };
	Sched.Execute<FLayerTypes>(Insts, V);

	if (RenderCalls.load() != 3)
	{
		std::puts("[FAIL] IRender dispatch count");
		return 1;
	}
	if (TotalCalls.load() != 5)
	{
		std::puts("[FAIL] all-layer dispatch count");
		return 1;
	}
	std::printf("ok: IRender x%d, all-layers x%d\n", RenderCalls.load(), TotalCalls.load());
	return 0;
}
