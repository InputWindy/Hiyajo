#include "EngineBase.h"

namespace Maho
{

namespace EngineBase
{

Maho::IAssembly* FEngineBase::CreateExtension()
{
	return new FEngineBase();
}

bool FEngineBase::ShouldContinue() const
{
	// TODO: poll the platform window / app state (e.g. FPlatformTool::ShouldClose()).
	return true;
}

int FEngineBase::Main(int Argc, char** Argv)
{
	(void)Argc;
	(void)Argv;

	// Execute is a parallel traverse base — no stage semantics. Lifecycle is the
	// host's job: call it once per phase (init once, tick per frame, shutdown
	// once). Tools are compile-time singletons (TTag<T> → T::Get()); child layers
	// are runtime instances (each IAssembly* in this->Layers).
	CreateLayers();

	Execute<FTools>([](auto Tag)
	{
		using T = typename decltype(Tag)::Type;
		// Init phase — per-tool capability (e.g. T::Get().Initialize()).
	});

	Execute(Layers, [](Maho::IAssembly* Layer)
	{
		// The host dispatches each instance's per-phase work here.
		(void)Layer;
	});

	while (ShouldContinue())
	{
		Execute(Layers, [](Maho::IAssembly* Layer)
		{
			// Per-frame tick work per instance.
			(void)Layer;
		});
	}

	Execute(Layers, [](Maho::IAssembly* Layer)
	{
		// Shutdown per instance.
		(void)Layer;
	});
	Execute<FTools>([](auto Tag)
	{
		using T = typename decltype(Tag)::Type;
		// Shutdown phase — per-tool capability.
	});

	return 0;
}

} // namespace EngineBase

} // namespace Maho

#if defined(_WIN32)
#	define MAHO_GAME_EXPORT __declspec(dllexport)
#else
#	define MAHO_GAME_EXPORT __attribute__((visibility("default")))
#endif

extern "C"
{
	MAHO_GAME_EXPORT Maho::IAssembly* CreateExtension()
	{
		return Maho::EngineBase::FEngineBase::CreateExtension();
	}
}
