#include <Core/Engine/GameEngine.h>

#include <Core/Misc/Log.h>

#include <memory>
#include <typeindex>

namespace Maho
{

bool FGameEngine::PreInitialize()
{
	RegisterExtension<FRenderSystem>(EExtensionPriority::System);

	std::unique_ptr<FSystemGroup> World(CreateWorld());
	if (!World)
	{
		MAHO_CORE_ERROR("FGameEngine: CreateWorld() returned null");
		return false;
	}

	RegisterExtensionInstance(
		std::unique_ptr<IEngineExtension>(World.release()),
		std::type_index(typeid(FSystemGroup)),
		EExtensionPriority::Layer);

	return true;
}

void FGameEngine::Tick()
{
	FRenderSystem* Render = GetExtension<FRenderSystem>();
	FSystemGroup* World = GetExtension<FSystemGroup>();

	if (Render)
	{
		Render->BeginFrame();
	}

	if (World)
	{
		World->BeginFrame();
		World->Tick();
		World->EndFrame();
	}

	// Editor / script / platform / resource per-frame hooks.
	DispatchStageToExtensions(EEngineStage::Tick);

	if (Render && World)
	{
		Render->SubmitFrameContext(Render->GatherContexts(World->GetWorld()));
	}

	if (Render)
	{
		Render->RenderFrame();
	}
}

} // namespace Maho
