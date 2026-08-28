#include <Engine/Layer.h>

namespace Maho
{

const FLayerBase::FDependencyTable& FLayerBase::GetDependencies() const
{
	return Dependencies;
}

void FLayerBase::AddDependency(std::type_index MyStage, std::string_view DepName, std::type_index DepStage)
{
	Dependencies[MyStage].push_back({ std::string(DepName), DepStage });
}

} // namespace Maho
