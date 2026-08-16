#include <RDG/RDGResources.h>

namespace Maho
{

FRDGBuffer::FRDGBuffer(const char* Name, const FRHIBufferDesc& InDesc,
                       bool bInExternal, bool bInTransient)
	: FRDGResource(Name, bInExternal, bInTransient)
	, Desc(InDesc)
{
}

FRDGTexture::FRDGTexture(const char* Name, const FRHITextureDesc& InDesc,
                         bool bInExternal, bool bInTransient)
	: FRDGResource(Name, bInExternal, bInTransient)
	, Desc(InDesc)
{
}

} // namespace Maho
