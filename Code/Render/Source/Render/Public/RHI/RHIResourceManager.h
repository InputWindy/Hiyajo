#pragma once

#include "RenderApi.h"
#include <RHI/RHIResources.h>

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Maho
{

class IRHI;

/**
 * Upper-layer entry for GPU resources. Acquire/Release hide pooling and VMA.
 * Call from the RHI thread (FRHIServer).
 */
class MAHO_RENDER_API FRHIResourceManager
{
public:
	explicit FRHIResourceManager(IRHI& Device);
	~FRHIResourceManager();

	FRHIResourceManager(const FRHIResourceManager&) = delete;
	FRHIResourceManager& operator=(const FRHIResourceManager&) = delete;

	[[nodiscard]] FRHITexture* AcquireTexture(const FRHITextureDesc& Desc, const char* Key = nullptr);
	[[nodiscard]] FRHIBuffer* AcquireBuffer(const FRHIBufferDesc& Desc, const char* Key = nullptr);
	[[nodiscard]] FRHISampler* AcquireSampler(const FRHISamplerDesc& Desc, const char* Key = nullptr);
	[[nodiscard]] FRHIShaderModule* AcquireShaderModule(const FRHIShaderModuleDesc& Desc, const char* Key = nullptr);
	[[nodiscard]] FRHIGraphicsPipeline* AcquireGraphicsPipeline(const FRHIGraphicsPipelineDesc& Desc, const char* Key = nullptr);
	[[nodiscard]] FRHIComputePipeline* AcquireComputePipeline(const FRHIComputePipelineDesc& Desc, const char* Key = nullptr);

	[[nodiscard]] FRHIResource* Find(const std::string& Key) const;

	void Release(FRHIResource* Resource, bool bImmediate = false);
	void FlushUnused();
	void Shutdown();

	[[nodiscard]] std::size_t GetLiveCount() const;
	[[nodiscard]] std::size_t GetPooledCount() const;

private:
	struct FPooledEntry
	{
		FRHIResource* Resource = nullptr;
		ERHIResourceType Type = ERHIResourceType::Unknown;
	};

	IRHI& Device;
	std::unordered_map<std::string, FRHIResource*> Named;
	std::vector<FPooledEntry> FreeList;
	std::size_t LiveCount = 0;
};

} // namespace Maho
