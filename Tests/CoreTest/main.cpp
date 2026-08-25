// Maho engine demo — a root layer over parallel subsystems, driven by the
// standard lifecycle only.
//
//   FGameEngine : FLayer<FRenderer, FResourceManager, FNetWork, FGameWorld>, IPlugin<IExit>
//     └─ each subsystem layer implements its own interface (IRenderer/INetwork/
//        IGameWorld) and refines its driven Main into finer stages
//
// The root owns the loop and stops via IExit; per frame it drives every child
// through Query<IMain> (each child's Main = one frame of its own work). Stage
// refinement lives inside each subsystem — the root never knows IRenderer.
#include <Maho.h>
#include <Core/Query.h>
#include <Engine/Layer.h>
#include "Gen/Closure.gen.h"   // code-gen: MAHO_CLOSURE_0_<Class>_<Key>, then MAHO_SORT_LEVEL
#include "Gen/main.gen.h"      // code-gen: MAHO_DEPS_<Class>_<Key> dependency macros

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace Maho;

// ── dependency table sample: FA ← FB ← FC (code-gen: MAHO_EXTEND_DEPS → .gen.h) ──
struct FA : FLayer<> { MAHO_EXTEND_DEPS(FA, FDefaultSlot, (FNoParent)); };
struct FB : FLayer<> { MAHO_EXTEND_DEPS(FB, FDefaultSlot, (FNoParent, FA)); };
struct FC : FLayer<> { MAHO_EXTEND_DEPS(FC, FDefaultSlot, (FNoParent, FA, FB)); };
// levels via Topo: FC = {FA, FB(level1)} + FC(level2) → FC level 2
static_assert(Topo::TNodeLevel<TTypeList<FA, FB, FC>, FDefaultSlot, FC>::Value == 2,
	"FC depends on FA and FB");

// ── subsystem interfaces ──
struct IRenderer
{
	virtual void PreRender() = 0;
	virtual void Render() = 0;
	virtual void PostRender() = 0;
};
struct INetwork { virtual void Poll() = 0; };
struct IGameWorld { virtual void Tick() = 0; };

// ── singleton services (driven by T::Get(), fixed ISingleton lifecycle) ──
struct FLog : TSingleton<FLog>, IPlugin<IInitialize, IShutdown>
{
	static FLog& Get() { static FLog I; return I; }   // test-local: no DLL boundary
	using FDepends = TTypeList<FDefaultSlot, TTypeList<>>;   // root singleton
	int Initiated = 0;
	void Initiate(int, char**) override { ++Initiated; }
	void Shutdown() override {}
};

struct FAudioService : TSingleton<FAudioService>, IPlugin<IInitialize, IShutdown>
{
	static FAudioService& Get() { static FAudioService I; return I; }
	using FDepends = TTypeList<FDefaultSlot, TTypeList<FLog>>;    // audio after log
	int Initiated = 0;
	void Initiate(int, char**) override { ++Initiated; }
	void Shutdown() override {}
};

// ── leaf render features ──
struct FRenderFeature : FLayer<>, IPlugin<IRenderer>
{
	std::atomic<int> Pre{ 0 }, Mid{ 0 }, Post{ 0 };
	void PreRender() override { Pre.fetch_add(1, std::memory_order_relaxed); }
	void Render() override { Mid.fetch_add(1, std::memory_order_relaxed); }
	void PostRender() override { Post.fetch_add(1, std::memory_order_relaxed); }
};
struct FSSAO : FRenderFeature
{
	static std::string_view GetModulePath() { return "unused/SSAO.dll"; }
};
struct FTonemap : FRenderFeature
{
	static std::string_view GetModulePath() { return "unused/Tonemap.dll"; }
};

// ── subsystems: each refines its driven Main into its own stages ──
struct FRenderer : FLayer<FSSAO, FTonemap>, IPlugin<IMain, IRenderer>
{
	// installing the renderer installs its render features
	void Initialize(int, char**) override
	{
		Enqueue(FLayerCommand{ FLayerCommand::EOp::Install, new FSSAO(), /*static*/ true });
		Enqueue(FLayerCommand{ FLayerCommand::EOp::Install, new FTonemap(), /*static*/ true });
		Flush();
	}

	int Main() override
	{
		PreRender();
		Render();
		PostRender();
		return 0;
	}
	void PreRender() override
	{
		Select<IRenderer>().ForEach([](IRenderer& F) { F.PreRender(); });
	}
	void Render() override
	{
		Select<IRenderer>().ForEach([](IRenderer& F) { F.Render(); });
	}
	void PostRender() override
	{
		Select<IRenderer>().ForEach([](IRenderer& F) { F.PostRender(); });
	}
	static std::string_view GetModulePath() { return "unused/Renderer.dll"; }
};

struct FResourceManager : FLayer<>
{
	// no per-frame work — default Main returns immediately
	static std::string_view GetModulePath() { return "unused/Resource.dll"; }
};

// a dynamic layer — type NOT declared in any FLayer<...>, runtime-installed DLL.
// Assumed independent (no topo ordering); driven in the dynamic batch.
struct FAudit : FLayer<>, IPlugin<IMain>
{
	std::atomic<int> Audits{ 0 };
	int Main() override { Audits.fetch_add(1, std::memory_order_relaxed); return 0; }
	static std::string_view GetModulePath() { return "unused/Audit.dll"; }
};

struct FNetWork : FLayer<>, IPlugin<IMain, INetwork>
{
	using FDepends = TTypeList<FDefaultSlot, TTypeList<>>;   // root
	std::atomic<int> Polls{ 0 };
	void Poll() override { Polls.fetch_add(1, std::memory_order_relaxed); }
	int Main() override { Poll(); return 0; }
	static std::string_view GetModulePath() { return "unused/Net.dll"; }
};

struct FGameWorld : FLayer<>, IPlugin<IMain, IGameWorld>
{
	using FDepends = TTypeList<FDefaultSlot, TTypeList<FNetWork>>;  // world after network
	std::atomic<int> Ticks{ 0 };
	void Tick() override { Ticks.fetch_add(1, std::memory_order_relaxed); }
	int Main() override { Tick(); return 0; }
	static std::string_view GetModulePath() { return "unused/World.dll"; }
};

// ── the engine root: owns the loop (IExit stops it), drives children per frame ──
struct FGameEngine
	// singletons (FLog/FAudioService) live in the SAME plugin table as layer
	// instances — Select<IService>().ForEach drives T::Get() for them here.
	: FLayer<FRenderer, FResourceManager, FNetWork, FGameWorld, FLog, FAudioService>
	, IPlugin<IMain, IExit>
{
	std::atomic<bool> bExit{ false };
	void Exit() override { bExit.store(true, std::memory_order_release); }

	// installing the engine installs the children it manages (recursive)
	void Initialize(int, char**) override
	{
		// singletons: same syntax as instances — Select then ForEach; the FLayer
		// query drives T::Get() for CRTP-singleton members of the table.
		Select<IPlugin<IInitialize, IShutdown>>().ForEach([](IPlugin<IInitialize, IShutdown>& S) { S.Initiate(0, nullptr); });

		Enqueue(FLayerCommand{ FLayerCommand::EOp::Install, new FRenderer(), /*static*/ true });
		Enqueue(FLayerCommand{ FLayerCommand::EOp::Install, new FResourceManager(), /*static*/ true });
		Enqueue(FLayerCommand{ FLayerCommand::EOp::Install, new FNetWork(), /*static*/ true });
		Enqueue(FLayerCommand{ FLayerCommand::EOp::Install, new FGameWorld(), /*static*/ true });
		// a dynamic layer (type NOT in FChildren → independent, no topo ordering)
		Enqueue(FLayerCommand{ FLayerCommand::EOp::Install, new FAudit(), /*dynamic*/ false });
		Flush(); // apply → each child's Initialize runs (recursive)
	}

	// exit after a fixed frame budget — deterministic, no cross-thread stopper
	int FramesLeft = 200;
	void SetFrames(int N) { FramesLeft = N; }

	int Main() override
	{
		while (FramesLeft-- > 0 && !bExit.load(std::memory_order_acquire))
		{
			// drive only the IMain-capable children (FResourceManager has no Main),
			// level-by-level, parallel within a level.
			Select<IMain>().ForEach([](IMain& C) { C.Main(); });
		}
		return 0;
	}

	static std::string_view GetModulePath() { return "unused/Game.dll"; }
};

int main()
{
	// ── Core/Query.h: type-agnostic compile-time LINQ (Select/With/Not) ──
	{
		// FA/FB/FC are FLayer<>; use them as both "source table" and filters.
		// FC : FLayer<FA,FB> ... use a dedicated predicate set instead:
		struct IA {}; struct IB {}; struct IC {};
		struct A : IA {}; struct B : IA, IB {}; struct C : IB, IC {};

		using FTable = TTypeList<A, B, C>;
		using S1 = typename decltype(Query<FTable>().Select<IA>())::FResult;   // A,B derive IA
		static_assert(std::is_same_v<S1, TTypeList<A, B>>, "Select OR fails");
		using S2 = typename decltype(Query<FTable>().Select<IB>().With<IA>())::FResult;  // B (IB+IA)
		static_assert(std::is_same_v<S2, TTypeList<B>>, "With AND fails");
		using S3 = typename decltype(Query<FTable>().Select<IA>().Not<IB>())::FResult;   // A (IA, not IB)
		static_assert(std::is_same_v<S3, TTypeList<A>>, "Not NOR fails");
	}

	// EnqueueCommand via FQueue's minimal command set + FLayerCommand::Callback:
	// any FQueue<FLayerCommand> holder can enqueue a lambda from any thread; Flush
	// executes it. Verify on an independent queue (not a per-frame layer).
	{
		FQueue<FLayerCommand> QCmd;
		int Calls = 0;
		QCmd.Enqueue(FLayerCommand::Callback([&] { Calls += 1; }));
		QCmd.Enqueue(FLayerCommand::Callback([&] { Calls += 2; }));
		QCmd.Flush();
		if (Calls != 3)
		{
			std::printf("[FAIL] EnqueueCommand calls=%d want=3\n", Calls);
			return 1;
		}
	}

	// bootstrap the root: its Initialize recursively installs the subtree
	// (children + FRenderer::Initialize → features). All owned by the subtree.
	FGameEngine Engine;
	Engine.Initialize(0, nullptr); // → singletons Initiate + install children/features

	// singletons were Initiate'd exactly once via TTypeQuery (T::Get(), no array)
	if (FLog::Get().Initiated != 1 || FAudioService::Get().Initiated != 1)
	{
		std::puts("[FAIL] singleton services not Initiate'd exactly once");
		return 1;
	}

	// topology: FGameWorld depends on FNetWork → net is a level before world
	using FEngLevels = typename FGameEngine::FLevels;
	static_assert(FEngLevels::Count > 0, "engine children form at least one level");
	static_assert(std::is_base_of_v<IMain, FNetWork>, "network is a driven child");

	// exit after a fixed 200 frames — deterministic (no live stopper thread)
	const int Frames = 200;
	Engine.SetFrames(Frames);
	Engine.Main();        // runs Frames frames, then exits the loop

	// every subsystem ran exactly Frames frames
	FRenderer* Renderer = Engine.FindChild<FRenderer>();
	FNetWork* Net = Engine.FindChild<FNetWork>();
	FGameWorld* World = Engine.FindChild<FGameWorld>();
	FAudit* Audit = Engine.FindChild<FAudit>();
	const bool Ok = Renderer && Net && World && Audit
		&& Renderer->FindChild<FSSAO>() && Renderer->FindChild<FTonemap>()
		&& Renderer->FindChild<FSSAO>()->Pre.load() == Frames
		&& Renderer->FindChild<FSSAO>()->Mid.load() == Frames
		&& Renderer->FindChild<FSSAO>()->Post.load() == Frames
		&& Renderer->FindChild<FTonemap>()->Pre.load() == Frames
		&& Renderer->FindChild<FTonemap>()->Mid.load() == Frames
		&& Renderer->FindChild<FTonemap>()->Post.load() == Frames
		&& Net->Polls.load() == Frames && World->Ticks.load() == Frames
		&& Audit->Audits.load() == Frames;   // dynamic layer driven in the batch
	if (!Ok)
	{
		std::puts("[FAIL] subsystems did not all run the same frames");
		return 1;
	}
	std::printf("ok: %d frames x 2 features x 3 stages driven in parallel levels; net poll, world tick, dynamic audit\n", Frames);
	// Engine dtor → uninstall all owned children (frees the heap children)
	return 0;
}
