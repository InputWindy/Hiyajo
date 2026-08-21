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

	// Execute is a parallel traverse base (from FParallelScheduler). Lifeycle is
	// the host's job: call it once per phase (init / per frame / shutdown), each
	// with a visitor deciding what each target does. Tools are singletons
	// (T::Get()); child layers are runtime instance collections.
	//
	//   Execute<FTools>([](auto& Tool) { Tool.Initialize(); });
	//   Execute(Layers, [](IAssembly* L) { L->...; });
	//
	// (EngineBase has no tools/layers — nothing to drive.)

	while (ShouldContinue())
	{
	}

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
