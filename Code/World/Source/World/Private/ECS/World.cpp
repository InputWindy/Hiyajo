#include <ECS/World.h>

namespace Maho
{

FWorld::FWorld()
{
}

FEntityManager& FWorld::GetEntityManager()
{
	return Manager;
}

const FEntityManager& FWorld::GetEntityManager() const
{
	return Manager;
}

FEntityHandle FWorld::CreateEntity()
{
	return Manager.CreateEntity();
}

} // namespace Maho
