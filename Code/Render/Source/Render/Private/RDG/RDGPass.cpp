#include <RDG/RDGPass.h>

namespace Maho
{

FRDGPass::FRDGPass(const char* Name, ERDGPassType Type)
	: Name(Name)
	, Type(Type)
{
}

void FRDGPass::AddRead(FRDGResource* Resource, ERHIResourceState State)
{
	if (Resource == nullptr)
	{
		return;
	}
	FRDGResourceAccess Access;
	Access.Resource = Resource;
	Access.RequiredState = State;
	Reads.push_back(Access);
}

void FRDGPass::AddWrite(FRDGResource* Resource, ERHIResourceState State)
{
	if (Resource == nullptr)
	{
		return;
	}
	FRDGResourceAccess Access;
	Access.Resource = Resource;
	Access.RequiredState = State;
	Writes.push_back(Access);
}

} // namespace Maho
