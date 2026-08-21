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

	// IAssembly::Main — the one coarse entry — is subdivided into the host's
	// EEngineStage. Its child layers (runtime instances) are built here, then
	// driven by stage: tools by compile-time type (T::Get()), layers by
	// instance (each IAssembly* in this->Layers).
	CreateLayers();

	Execute<EEngineStage::Init, FTools>([](auto Tag, EEngineStage)
	{
		using T = typename decltype(Tag)::Type;
		// TODO: per-tool Init capability (e.g. T::Get().Initialize()).
	});
	Execute<EEngineStage::Init>(Layers, [](Maho::IAssembly* Layer, EEngineStage Stage)
	{
		// The host maps (instance, Stage) → that layer's capability methods.
		(void)Layer;
		(void)Stage;
	});

	while (ShouldContinue())
	{
		Execute<EEngineStage::Tick>(Layers, [](Maho::IAssembly* Layer, EEngineStage Stage)
		{
			(void)Layer;
			(void)Stage;
		});
	}

	Execute<EEngineStage::Shutdown>(Layers, [](Maho::IAssembly* Layer, EEngineStage Stage)
	{
		(void)Layer;
		(void)Stage;
	});
	Execute<EEngineStage::Shutdown, FTools>([](auto Tag, EEngineStage)
	{
		using T = typename decltype(Tag)::Type;
		// TODO: per-tool Shutdown capability.
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
