// Maho engine core smoke test — exercises the post-refactor API surface:
//   Topo levels via MAHO_EXTEND_DEPS / FDefaultSlot (code-gen .gen.h)
//   TQuery compile-time type algebra (Select / With / Not)
//   FLayer construction + ForEach<FLevels> (singleton & instance branches)
//   FQueue catalog lanes (Install / Uninstall command shapes)
//
// The pre-refactor demo (FLayerCommand / Select<> / Flush / FindChild /
// ForEachSingleton) exercised removed machinery and was replaced by this
// minimal compile+run smoke against the current building blocks.
#include <Maho.h>
#include <Core/Query.h>
#include <Core/Queue.h>
#include <Engine/Layer.h>
#include "Gen/Closure.gen.h"   // code-gen: MAHO_CLOSURE_0_<Class>_<Key>, then MAHO_SORT_LEVEL
#include "Gen/main.gen.h"      // code-gen: MAHO_DEPS_<Class>_<Key> dependency macros

#include <cstdio>
#include <string_view>
#include <type_traits>

using namespace Maho;

// ── dependency table sample: FA ← FB ← FC (code-gen: MAHO_EXTEND_DEPS → .gen.h) ──
struct FA : FLayer<> { MAHO_EXTEND_DEPS(FA, FDefaultSlot, (FNoParent)); };
struct FB : FLayer<> { MAHO_EXTEND_DEPS(FB, FDefaultSlot, (FNoParent, FA)); };
struct FC : FLayer<> { MAHO_EXTEND_DEPS(FC, FDefaultSlot, (FNoParent, FA, FB)); };
// levels via Topo: FA = level 0, FB = level 1, FC = level 2
static_assert(Topo::TNodeLevel<TTypeList<FA, FB, FC>, FDefaultSlot, FA>::Value == 0,
	"FA is a root");
static_assert(Topo::TNodeLevel<TTypeList<FA, FB, FC>, FDefaultSlot, FB>::Value == 1,
	"FB depends on FA");
static_assert(Topo::TNodeLevel<TTypeList<FA, FB, FC>, FDefaultSlot, FC>::Value == 2,
	"FC depends on FA and FB");

// ── singleton services (driven by T::Get(), IInit/IShutdown lifecycle) ──
struct FLog : TSingleton<FLog>, IPlugin<IInit, IShutdown>
{
	static FLog& Get() { static FLog I; return I; }   // test-local: no DLL boundary
	int Initiated = 0;
	void Initialize(int, char**) override { ++Initiated; }
	void Shutdown() override {}
};

struct FAudioService : TSingleton<FAudioService>, IPlugin<IInit, IShutdown>
{
	static FAudioService& Get() { static FAudioService I; return I; }
	int Initiated = 0;
	void Initialize(int, char**) override { ++Initiated; }
	void Shutdown() override {}
};

// ── instance layer children — GetModulePath paths are intentionally bogus, so
//    FLayer's ctor Load() fails gracefully and nothing external is touched. ──
struct FSSAO : FLayer<>
{
	static std::string_view GetModulePath() { return "unused/SSAO.dll"; }
};
struct FTonemap : FLayer<>
{
	static std::string_view GetModulePath() { return "unused/Tonemap.dll"; }
};

int main()
{
	// ── Core/Query.h: compile-time LINQ over a TTypeList (Select/With/Not) ──
	{
		struct IA {}; struct IB {}; struct IC {};
		struct A : IA {}; struct B : IA, IB {}; struct C : IB, IC {};

		using FTable = TTypeList<A, B, C>;
		using S1 = typename decltype(TQuery<FTable>{}.Select<IA>())::FResult;   // A,B derive IA
		static_assert(std::is_same_v<S1, TTypeList<A, B>>, "Select OR fails");
		using S2 = typename decltype(TQuery<FTable>{}.Select<IB>().With<IA>())::FResult;  // B (IB+IA)
		static_assert(std::is_same_v<S2, TTypeList<B>>, "With AND fails");
		using S3 = typename decltype(TQuery<FTable>{}.Select<IA>().Not<IB>())::FResult;   // A (IA, not IB)
		static_assert(std::is_same_v<S3, TTypeList<A>>, "Not NOR fails");
	}

	// ── FLayer<...> construction: bogus child DLL paths → Load fails, no
	//    installs; the compile-time child table (FLayers) is the smoke. ──
	{
		using FRenderer = FLayer<FSSAO, FTonemap>;
		FRenderer Renderer;
		static_assert(FRenderer::FLayers::Count == 2, "child type table");
	}

	// ── FLayer::ForEach<FLevels> — singleton branch: every level type is a
	//    TSingleton → the visitor receives T::Get() (level barriers serialize). ──
	{
		FLayer<> Base;
		using FLogLevel = TTypeList<FLog, FAudioService>;   // both roots (level 0)
		Base.ForEach<TTypeList<FLogLevel>>([](auto& S) { S.Initialize(0, nullptr); });
		if (FLog::Get().Initiated != 1 || FAudioService::Get().Initiated != 1)
		{
			std::puts("[FAIL] singleton branch did not Initialize each service exactly once");
			return 1;
		}
	}

	// ── FLayer::ForEach<FLevels> — instance branch: bogus DLLs → no installed
	//    instances to dispatch; the compile-time level table is the smoke. ──
	{
		using FRenderer = FLayer<FSSAO, FTonemap>;
		FRenderer Renderer;
		using FLevels = Topo::TLevels_t<FRenderer::FLayers, FDefaultSlot>;
		Renderer.ForEach<FLevels>([](auto& L) { (void)L; });
	}

	// ── Core/Queue.h: type-erased catalog lanes (Install/Uninstall shapes) ──
	{
		FQueue Q;
		Q.Enqueue(std::make_unique<FInstallCommand>());
		Q.Enqueue(std::make_unique<FUninstallCommand>());
		if (Q.Size() != 2)
		{
			std::printf("[FAIL] queue lane count=%zu want=2\n", Q.Size());
			return 1;
		}
		auto Cmd = Q.Dequeue(static_cast<std::uint64_t>(ELayerCommand::Install));
		if (!Cmd || Cmd->GetCatalogId() != static_cast<std::uint64_t>(ELayerCommand::Install))
		{
			std::puts("[FAIL] install lane FIFO");
			return 1;
		}
		if (Q.Size() != 1)
		{
			std::printf("[FAIL] after dequeue size=%zu want=1\n", Q.Size());
			return 1;
		}
	}

	std::puts("ok: Topo levels + TQuery + FLayer (singleton/instance ForEach) + FQueue lanes");
	return 0;
}
