#include <Core/ECS/ComponentType.h>

namespace Maho
{
namespace Internal
{

// Single DLL-owned counter so template GetComponentTypeId<T>() (which may be
// instantiated in the EXE or other consumers) still produces globally unique
// ids.  Without this the inline static in the header would be duplicated per
// translation unit that includes ComponentType.h.
static FComponentTypeId GCounter = 0;

FComponentTypeId NextComponentTypeId()
{
	return GCounter++;
}

// DLL-owned size registry: shared with all consumers (e.g. EXE that defines
// FTransformComponent) so that ECS chunk layout can size columns correctly.
std::vector<std::size_t>& GetComponentSizeRegistry()
{
	static std::vector<std::size_t> Registry;
	return Registry;
}

void RegisterComponentSize(FComponentTypeId Id, std::size_t Size)
{
	auto& Registry = GetComponentSizeRegistry();
	if (Registry.size() <= Id)
	{
		Registry.resize(Id + 1, static_cast<std::size_t>(-1));
	}
	Registry[Id] = Size;
}

std::size_t GetComponentSize(FComponentTypeId Id)
{
	auto& Registry = GetComponentSizeRegistry();
	if (Id >= Registry.size())
	{
		return 0;
	}
	std::size_t Size = Registry[Id];
	return (Size == static_cast<std::size_t>(-1)) ? 0 : Size;
}

} // namespace Internal
} // namespace Maho
