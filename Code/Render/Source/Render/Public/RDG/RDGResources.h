#pragma once

#include "RenderApi.h"
#include <RHI/RHIEnums.h>
#include <RHI/RHIResources.h>

namespace Maho
{

/**
 * RDG virtual resource base.
 * Represents a resource participating in the render graph.
 * External resources are owned outside the graph (e.g. FRenderScene buffers);
 * Transient resources are created within the graph (frame lifetime, memory-aliased).
 */
class MAHO_RENDER_API FRDGResource
{
public:
	FRDGResource(const FRDGResource&) = delete;
	FRDGResource& operator=(const FRDGResource&) = delete;
	FRDGResource(FRDGResource&&) = default;
	FRDGResource& operator=(FRDGResource&&) = default;
	virtual ~FRDGResource() = default;

	[[nodiscard]] const char* GetName() const
	{
		return Name;
	}
	[[nodiscard]] bool IsExternal() const
	{
		return bExternal;
	}
	[[nodiscard]] bool IsTransient() const
	{
		return bTransient;
	}

	// Barrier tracking (populated during Compile)
	ERHIResourceState CurrentState = ERHIResourceState::Common;
	std::uint32_t LastTouchedPass = UINT32_MAX;

protected:
	FRDGResource(const char* InName, bool bInExternal, bool bInTransient)
		: Name(InName)
		, bExternal(bInExternal)
		, bTransient(bInTransient)
	{
	}

	const char* Name = nullptr;
	bool bExternal = false;
	bool bTransient = false;
};

class MAHO_RENDER_API FRDGBuffer final : public FRDGResource
{
public:
	FRDGBuffer(const char* Name, const FRHIBufferDesc& InDesc,
	           bool bInExternal, bool bInTransient);
	[[nodiscard]] const FRHIBufferDesc& GetDesc() const
	{
		return Desc;
	}
	[[nodiscard]] FRHIBuffer* GetRHI() const
	{
		return Handle;
	}
	void SetRHI(FRHIBuffer* InRHI)
	{
		Handle = InRHI;
	}

private:
	FRHIBufferDesc Desc;
	FRHIBuffer* Handle = nullptr;
};

class MAHO_RENDER_API FRDGTexture final : public FRDGResource
{
public:
	FRDGTexture(const char* Name, const FRHITextureDesc& InDesc,
	            bool bInExternal, bool bInTransient);
	[[nodiscard]] const FRHITextureDesc& GetDesc() const
	{
		return Desc;
	}
	[[nodiscard]] FRHITexture* GetRHI() const
	{
		return Handle;
	}
	void SetRHI(FRHITexture* InRHI)
	{
		Handle = InRHI;
	}

private:
	FRHITextureDesc Desc;
	FRHITexture* Handle = nullptr;
};

} // namespace Maho
