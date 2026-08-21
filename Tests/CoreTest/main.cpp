// Maho core demo — a scheduler, a few Layers wired with dependencies, leveled
// parallel dispatch. Runs a real layered system with a thread pool.
//
//   Layers declare deps via MAHO_EXTEND_DEPS. A scheduler (FParallelScheduler,
//   parameterized over its extension scan table) holds the Layer instances.
//   MAHO_SORT_LEVELS computes the closure-level bands; each band runs in
//   parallel (barrier between bands).
//
#include <Maho.h>
#include <Engine/Schedulers.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <vector>

using namespace Maho;

// ── interfaces (Query Select bases + dependency Keys) ──
struct IRender { virtual void Render() = 0; };
struct IPhysics { virtual void Step() = 0; };
struct IAudio { virtual void Play() = 0; };

// ── concrete Layer base: satisfies every pure virtual of ILayer's capabilities ──
struct FLayerBase : ILayer
{
	void Initialize(int, char**) override {}
	void Tick() override {}
	void Shutdown() override {}
	int MainLoop(int, char**) override { return 0; }
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

// ── the app: a Layer host — an Assembly + a scheduler whose scan table IS the
//    full layer list. It owns the instances and drives them by runtime type. ──
using FLayerTypes = TTypeList<FWindow, FScene, FPhysics, FAudio, FPlayer>;

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

struct FAppLayer
	: ILayer
	, Parallel::FParallelScheduler<FLayerTypes>
{
	void Initialize(int, char**) override {}
	void Tick() override {}
	void Shutdown() override {}
	int MainLoop(int, char**) override
	{
		// every layer in the scan table (each ILayer* here) is a candidate;
		// Execute dispatches each instance to its concrete type.
		FCountVisitor V{ Calls };
		this->Execute<FLayerTypes, ILayer>(Insts, V);
		return 0;
	}

	std::vector<ILayer*> Insts;
	std::atomic<int> Calls{ 0 };
};

// ── layered dispatch through the host app layer ──
int main()
{
	FAppLayer App;

	// runtime instances, scrambled on purpose
	FWindow W; FScene S; FPhysics P; FAudio A; FPlayer Pl;
	App.Insts = { &Pl, &A, &W, &P, &S };

	App.MainLoop(0, nullptr);   // drives every layer in the scan table (5)

	if (App.Calls.load() != 5)
	{
		std::puts("[FAIL] app layer drove the wrong number of layers");
		return 1;
	}

	// separate: dispatch only the IRender subset at runtime
	std::atomic<int> RenderCalls{ 0 };
	auto OnRender = [&](IRender&) { RenderCalls.fetch_add(1, std::memory_order_relaxed); };
	App.Execute<TTypeList<FWindow, FScene, FPlayer>>(App.Insts, OnRender);

	if (RenderCalls.load() != 3)
	{
		std::puts("[FAIL] IRender dispatch count");
		return 1;
	}
	std::printf("ok: app drove %d layers, IRender subset x%d\n",
		App.Calls.load(), RenderCalls.load());
	return 0;
}
