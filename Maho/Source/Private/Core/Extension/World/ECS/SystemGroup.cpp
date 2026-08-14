#include <Core/Extension/World/ECS/SystemGroup.h>
#include <Core/Extension/World/ECS/EntityCommandBuffer.h>
#include <Core/Extension/World/ECS/World.h>

#include <Core/App.h>
#include <Core/Misc/Log.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Maho
{

// --- FECBSystem ---

FECBSystem::FECBSystem(FEntityCommandBuffer& InECB, const char* InName)
	: ECB(InECB)
	, Name(InName)
{
}

bool FECBSystem::ExecuteStage(EEngineStage Stage, float DeltaTime, FWorld& World)
{
	(void)DeltaTime;
	if (Stage == EEngineStage::Update)
	{
		ECB.Playback(World.GetEntityManager());
	}
	return true;
}

// --- FSystemGroup ---

FSystemGroup::FSystemGroup(const char* InName)
	: Name(InName)
{
	BeginECB = std::make_unique<FEntityCommandBuffer>();
	EndECB = std::make_unique<FEntityCommandBuffer>();

	std::string BeginName = std::string("Begin_") + InName;
	std::string EndName = std::string("End_") + InName;

	BeginECBSystem = std::make_unique<FECBSystem>(*BeginECB, BeginName.c_str());
	EndECBSystem = std::make_unique<FECBSystem>(*EndECB, EndName.c_str());

	// Begin plays before children, End after children. AddSystem/AddGroup insert
	// before EndECBSystem so it always stays last.
	Systems.push_back(BeginECBSystem.get());
	Systems.push_back(EndECBSystem.get());
}

FSystemGroup::~FSystemGroup() = default;

bool FSystemGroup::ExecuteStage(EEngineStage Stage)
{
	switch (Stage)
	{
	case EEngineStage::Attach:
		if (!bWorldReady)
		{
			if (IsRootGroup())
			{
				// Build the system tree: Initialization → Simulation.
				auto* SimGroup = AddGroup<FSimulationSystemGroup>();
				RegisterSystems(*SimGroup);
				SpawnInitialEntities(World);
			}

			ExecuteStage(Stage, 0.0f, World);

			bWorldReady = true;
			if (IsRootGroup())
			{
				MAHO_INFO("FSystemGroup: ECS world ready (\"{}\")", Name);
			}
		}
		return true;

	case EEngineStage::Detach:
	case EEngineStage::Shutdown:
		if (bWorldReady)
		{
			ExecuteStage(Stage, 0.0f, World);
			bWorldReady = false;
		}
		return true;

	default:
		if (bWorldReady)
		{
			float Dt = 0.0f;
			if (GApp)
			{
				if (Stage == EEngineStage::FixedUpdate)
				{
					Dt = GApp->GetFixedDeltaSeconds();
				}
				else if (Stage == EEngineStage::Update || Stage == EEngineStage::LateUpdate)
				{
					Dt = GApp->GetDeltaSeconds();
				}
			}

			ExecuteStage(Stage, Dt, World);

			if (Stage == EEngineStage::Update && IsRootGroup())
			{
				World.GetEntityManager().EndFrame();
			}
		}
		return true;
	}
}

bool FSystemGroup::ExecuteStage(EEngineStage Stage, float DeltaTime, FWorld& World)
{
	for (ISystem* Sys : Systems)
	{
		if (Sys)
		{
			Sys->ExecuteStage(Stage, DeltaTime, World);
		}
	}
	return true;
}

void FSystemGroup::InsertBeforeEnd(ISystem* InSystem)
{
	if (EndECBSystem)
	{
		auto It = std::find(Systems.begin(), Systems.end(), EndECBSystem.get());
		Systems.insert(It, InSystem);
	}
	else
	{
		Systems.push_back(InSystem);
	}
}

void FSystemGroup::UpdateBeforeByName(const char* A, const char* B)
{
	// For now, use a simple topological sort based on registration order.
	// If A should be before B, and A appears after B in the list, swap positions.

	std::size_t IndexA = static_cast<std::size_t>(-1);
	std::size_t IndexB = static_cast<std::size_t>(-1);

	for (std::size_t I = 0; I < Systems.size(); ++I)
	{
		if (std::string(Systems[I]->GetName()) == A)
		{
			IndexA = I;
		}
		if (std::string(Systems[I]->GetName()) == B)
		{
			IndexB = I;
		}
	}

	if (IndexA != static_cast<std::size_t>(-1) && IndexB != static_cast<std::size_t>(-1) && IndexA > IndexB)
	{
		std::swap(Systems[IndexA], Systems[IndexB]);
	}
}

} // namespace Maho
