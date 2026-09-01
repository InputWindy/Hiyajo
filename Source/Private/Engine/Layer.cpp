#include <Engine/Layer.h>

namespace Maho
{

FLayerBase::~FLayerBase() = default;

const FLayerBase::FDependencyTable& FLayerBase::GetDependencies() const
{
	return Dependencies;
}

void FLayerBase::WaitFor(std::type_index MyStage, std::string_view OtherName, std::type_index OtherStage)
{
	Dependencies[MyStage].push_back({ std::string(OtherName), OtherStage });
}

void FLayerBase::BlockOn(std::string_view OtherName, std::type_index OtherStage, std::type_index MyStage)
{
	Dependents.push_back({ std::string(OtherName), OtherStage, MyStage });
}

} // namespace Maho
