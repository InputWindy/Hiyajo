#include <Core/ECS/SystemGroup.h>
#include <Core/ECS/EntityCommandBuffer.h>
#include <Core/ECS/World.h>

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

void FECBSystem::OnUpdate(float DeltaTime, FWorld& World)
{
	ECB.Playback(World.GetEntityManager());
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
}

FSystemGroup::~FSystemGroup() = default;

// --- FSystemGroup lifecycle ---

void FSystemGroup::OnCreate(FWorld& World)
{
	DispatchNoDT(World, &ISystem::OnCreate);
}

void FSystemGroup::OnDestroy(FWorld& World)
{
	DispatchNoDT(World, &ISystem::OnDestroy);
}

void FSystemGroup::OnBeginFrame(FWorld& World)
{
	DispatchNoDT(World, &ISystem::OnBeginFrame);
}

void FSystemGroup::OnFixedUpdate(float DeltaTime, FWorld& World)
{
	DispatchWithDT(World, DeltaTime, &ISystem::OnFixedUpdate);
}

void FSystemGroup::OnUpdate(float DeltaTime, FWorld& World)
{
	if (BeginECBSystem)
	{
		BeginECBSystem->OnUpdate(DeltaTime, World);
	}

	DispatchWithDT(World, DeltaTime, &ISystem::OnUpdate);

	if (EndECBSystem)
	{
		EndECBSystem->OnUpdate(DeltaTime, World);
	}
}

void FSystemGroup::OnLateUpdate(float DeltaTime, FWorld& World)
{
	DispatchWithDT(World, DeltaTime, &ISystem::OnLateUpdate);
}

void FSystemGroup::OnEndFrame(FWorld& World)
{
	DispatchNoDT(World, &ISystem::OnEndFrame);
}

void FSystemGroup::OnPreRender(FWorld& World)
{
	DispatchNoDT(World, &ISystem::OnPreRender);
}

void FSystemGroup::OnPostRender(FWorld& World)
{
	DispatchNoDT(World, &ISystem::OnPostRender);
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
