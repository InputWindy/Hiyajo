#include <Core/Extension/World/WorldLayer.h>
#include <Render/RenderSystem.h>

#include <Core/Application/App.h>
#include <Core/System/Log.h>

#include <utility>

namespace Maho
{

FWorldLayer::FWorldLayer(std::string InWorldName)
	: FLayer("WorldLayer")
	, WorldName(std::move(InWorldName))
{
}

bool FWorldLayer::ExecuteStage(Maho::EEngineStage Stage)
{
	switch (Stage)
	{
	case Maho::EEngineStage::Init:
		break;

	case Maho::EEngineStage::Attach:
		if (!bWorldReady)
		{
			// Build the system tree: Initialization → Simulation.
			auto* SimGroup = RootGroup.AddGroup<Maho::FSimulationSystemGroup>();
			RegisterSystems(*SimGroup);
			SpawnInitialEntities(World);

			RootGroup.OnCreate(World);
			bWorldReady = true;
			MAHO_INFO("FWorldLayer: ECS world ready (\"{}\")", WorldName);
		}
		break;

	case Maho::EEngineStage::Detach:
		if (bWorldReady)
		{
			RootGroup.OnDestroy(World);
			bWorldReady = false;
		}
		break;

	case Maho::EEngineStage::BeginFrame:
		if (bWorldReady)
		{
			RootGroup.OnBeginFrame(World);
		}
		break;

	case Maho::EEngineStage::ProcessInput:
		// No ECS system hook for ProcessInput; game logic handles it via other means.
		break;

	case Maho::EEngineStage::FixedUpdate:
		if (bWorldReady && Maho::GApp)
		{
			const float FixedDt = Maho::GApp->GetFixedDeltaSeconds();
			RootGroup.OnFixedUpdate(FixedDt, World);
		}
		break;

	case Maho::EEngineStage::Update:
		if (bWorldReady && Maho::GApp)
		{
			const float Dt = Maho::GApp->GetDeltaSeconds();
			RootGroup.OnUpdate(Dt, World);
			World.GetEntityManager().EndFrame();
		}
		break;

	case Maho::EEngineStage::LateUpdate:
		if (bWorldReady && Maho::GApp)
		{
			const float Dt = Maho::GApp->GetDeltaSeconds();
			RootGroup.OnLateUpdate(Dt, World);
		}
		break;

	case Maho::EEngineStage::EndFrame:
		if (bWorldReady)
		{
			RootGroup.OnEndFrame(World);
		}
		break;

	case Maho::EEngineStage::PreRender:
		if (bWorldReady)
		{
			RootGroup.OnPreRender(World);
			// Gather render feature contexts from the world and hand them to the render thread.
			if (Maho::GApp)
			{
				if (auto* RenderSystem = Maho::GApp->GetExtension<Maho::FRenderSystem>())
				{
					RenderSystem->SubmitFrameContext(RenderSystem->GatherContexts(World));
				}
			}
		}
		break;

	case Maho::EEngineStage::PostRender:
		if (bWorldReady)
		{
			RootGroup.OnPostRender(World);
		}
		break;

	case Maho::EEngineStage::PrepareExit:
	case Maho::EEngineStage::Shutdown:
		if (bWorldReady)
		{
			RootGroup.OnDestroy(World);
			bWorldReady = false;
		}
		break;

	default:
		break;
	}
	return true;
}

} // namespace Maho
