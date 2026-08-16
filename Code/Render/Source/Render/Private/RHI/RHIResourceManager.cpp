#include <RHI/RHIResourceManager.h>

#include <RHI/RHI.h>

#include <Core/Misc/Log.h>

#include <vector>

namespace Maho
{

FRHIResourceManager::FRHIResourceManager(IRHI& InDevice)
	: Device(InDevice)
{
}

FRHIResourceManager::~FRHIResourceManager()
{
	Shutdown();
}

FRHITexture* FRHIResourceManager::AcquireTexture(const FRHITextureDesc& Desc, const char* Key)
{
	if (Key != nullptr && Key[0] != '\0')
	{
		const auto It = Named.find(Key);
		if (It != Named.end() && It->second != nullptr && It->second->GetType() == ERHIResourceType::Texture)
		{
			auto* Existing = static_cast<FRHITexture*>(It->second);
			const FRHITextureDesc& ExistingDesc = Existing->GetDesc();
			if (ExistingDesc.Format == Desc.Format
				&& ExistingDesc.Dimension == Desc.Dimension
				&& ExistingDesc.Extent.Width == Desc.Extent.Width
				&& ExistingDesc.Extent.Height == Desc.Extent.Height
				&& ExistingDesc.Extent.Depth == Desc.Extent.Depth
				&& ExistingDesc.MipLevels == Desc.MipLevels
				&& ExistingDesc.ArrayLayers == Desc.ArrayLayers
				&& ExistingDesc.Usage == Desc.Usage
				&& ExistingDesc.MemoryUsage == Desc.MemoryUsage)
			{
				++Existing->RefCount;
				return Existing;
			}
			MAHO_CORE_ERROR(
				"FRHIResourceManager::AcquireTexture: key '{}' exists with incompatible Desc — creating unnamed",
				Key);
			Key = nullptr;
		}
	}

	for (std::size_t Index = 0; Index < FreeList.size(); ++Index)
	{
		FPooledEntry& Entry = FreeList[Index];
		if (Entry.Type != ERHIResourceType::Texture || Entry.Resource == nullptr)
		{
			continue;
		}

		auto* Texture = static_cast<FRHITexture*>(Entry.Resource);
		if (Texture->GetDesc().Format != Desc.Format
			|| Texture->GetDesc().Extent.Width != Desc.Extent.Width
			|| Texture->GetDesc().Extent.Height != Desc.Extent.Height
			|| Texture->GetDesc().Usage != Desc.Usage
			|| Texture->GetDesc().MemoryUsage != Desc.MemoryUsage)
		{
			continue;
		}

		FreeList.erase(FreeList.begin() + static_cast<std::ptrdiff_t>(Index));
		Texture->RefCount = 1;
		if (Key != nullptr && Key[0] != '\0')
		{
			Texture->DebugName = Key;
			Named[Key] = Texture;
		}
		++LiveCount;
		return Texture;
	}

	FRHITexture* Created = Device.CreateTexture(Desc);
	if (Created == nullptr)
	{
		MAHO_CORE_ERROR("FRHIResourceManager::AcquireTexture: CreateTexture failed");
		return nullptr;
	}

	Created->RefCount = 1;
	if (Key != nullptr && Key[0] != '\0')
	{
		Created->DebugName = Key;
		Named[Key] = Created;
	}
	++LiveCount;
	return Created;
}

FRHIBuffer* FRHIResourceManager::AcquireBuffer(const FRHIBufferDesc& Desc, const char* Key)
{
	if (Key != nullptr && Key[0] != '\0')
	{
		const auto It = Named.find(Key);
		if (It != Named.end() && It->second != nullptr && It->second->GetType() == ERHIResourceType::Buffer)
		{
			++It->second->RefCount;
			return static_cast<FRHIBuffer*>(It->second);
		}
	}

	for (std::size_t Index = 0; Index < FreeList.size(); ++Index)
	{
		FPooledEntry& Entry = FreeList[Index];
		if (Entry.Type != ERHIResourceType::Buffer || Entry.Resource == nullptr)
		{
			continue;
		}

		auto* Buffer = static_cast<FRHIBuffer*>(Entry.Resource);
		if (Buffer->GetDesc().Size != Desc.Size
			|| Buffer->GetDesc().Usage != Desc.Usage
			|| Buffer->GetDesc().MemoryUsage != Desc.MemoryUsage)
		{
			continue;
		}

		FreeList.erase(FreeList.begin() + static_cast<std::ptrdiff_t>(Index));
		Buffer->RefCount = 1;
		if (Key != nullptr && Key[0] != '\0')
		{
			Buffer->DebugName = Key;
			Named[Key] = Buffer;
		}
		++LiveCount;
		return Buffer;
	}

	FRHIBuffer* Created = Device.CreateBuffer(Desc);
	if (Created == nullptr)
	{
		MAHO_CORE_ERROR("FRHIResourceManager::AcquireBuffer: CreateBuffer failed");
		return nullptr;
	}

	Created->RefCount = 1;
	if (Key != nullptr && Key[0] != '\0')
	{
		Created->DebugName = Key;
		Named[Key] = Created;
	}
	++LiveCount;
	return Created;
}

FRHISampler* FRHIResourceManager::AcquireSampler(const FRHISamplerDesc& Desc, const char* Key)
{
	if (Key != nullptr && Key[0] != '\0')
	{
		const auto It = Named.find(Key);
		if (It != Named.end() && It->second != nullptr && It->second->GetType() == ERHIResourceType::Sampler)
		{
			++It->second->RefCount;
			return static_cast<FRHISampler*>(It->second);
		}
	}

	FRHISampler* Created = Device.CreateSampler(Desc);
	if (Created == nullptr)
	{
		return nullptr;
	}

	Created->RefCount = 1;
	if (Key != nullptr && Key[0] != '\0')
	{
		Created->DebugName = Key;
		Named[Key] = Created;
	}
	++LiveCount;
	return Created;
}

FRHIShaderModule* FRHIResourceManager::AcquireShaderModule(const FRHIShaderModuleDesc& Desc, const char* Key)
{
	if (Key != nullptr && Key[0] != '\0')
	{
		const auto It = Named.find(Key);
		if (It != Named.end() && It->second != nullptr && It->second->GetType() == ERHIResourceType::ShaderModule)
		{
			++It->second->RefCount;
			return static_cast<FRHIShaderModule*>(It->second);
		}
	}

	FRHIShaderModule* Created = Device.CreateShaderModule(Desc);
	if (Created == nullptr)
	{
		return nullptr;
	}

	Created->RefCount = 1;
	if (Key != nullptr && Key[0] != '\0')
	{
		Created->DebugName = Key;
		Named[Key] = Created;
	}
	++LiveCount;
	return Created;
}

FRHIGraphicsPipeline* FRHIResourceManager::AcquireGraphicsPipeline(const FRHIGraphicsPipelineDesc& Desc, const char* Key)
{
	if (Key != nullptr && Key[0] != '\0')
	{
		const auto It = Named.find(Key);
		if (It != Named.end() && It->second != nullptr && It->second->GetType() == ERHIResourceType::GraphicsPipeline)
		{
			++It->second->RefCount;
			return static_cast<FRHIGraphicsPipeline*>(It->second);
		}
	}

	FRHIGraphicsPipeline* Created = Device.CreateGraphicsPipeline(Desc);
	if (Created == nullptr)
	{
		return nullptr;
	}

	Created->RefCount = 1;
	if (Key != nullptr && Key[0] != '\0')
	{
		Created->DebugName = Key;
		Named[Key] = Created;
	}
	++LiveCount;
	return Created;
}

FRHIComputePipeline* FRHIResourceManager::AcquireComputePipeline(const FRHIComputePipelineDesc& Desc, const char* Key)
{
	if (Key != nullptr && Key[0] != '\0')
	{
		const auto It = Named.find(Key);
		if (It != Named.end() && It->second != nullptr && It->second->GetType() == ERHIResourceType::ComputePipeline)
		{
			++It->second->RefCount;
			return static_cast<FRHIComputePipeline*>(It->second);
		}
	}

	FRHIComputePipeline* Created = Device.CreateComputePipeline(Desc);
	if (Created == nullptr)
	{
		return nullptr;
	}

	Created->RefCount = 1;
	if (Key != nullptr && Key[0] != '\0')
	{
		Created->DebugName = Key;
		Named[Key] = Created;
	}
	++LiveCount;
	return Created;
}

FRHIResource* FRHIResourceManager::Find(const std::string& Key) const
{
	const auto It = Named.find(Key);
	if (It == Named.end())
	{
		return nullptr;
	}
	return It->second;
}

void FRHIResourceManager::Release(FRHIResource* Resource, bool bImmediate)
{
	if (Resource == nullptr)
	{
		return;
	}

	if (Resource->RefCount == 0)
	{
		MAHO_CORE_ERROR("FRHIResourceManager::Release: RefCount already 0 ({})", Resource->GetTypeName());
		return;
	}

	--Resource->RefCount;
	if (Resource->RefCount > 0)
	{
		return;
	}

	if (!Resource->DebugName.empty())
	{
		Named.erase(Resource->DebugName);
	}

	if (LiveCount > 0)
	{
		--LiveCount;
	}

	if (bImmediate)
	{
		switch (Resource->GetType())
		{
		case ERHIResourceType::Buffer:
			Device.DestroyBuffer(static_cast<FRHIBuffer*>(Resource));
			break;
		case ERHIResourceType::Texture:
			Device.DestroyTexture(static_cast<FRHITexture*>(Resource));
			break;
		case ERHIResourceType::Sampler:
			Device.DestroySampler(static_cast<FRHISampler*>(Resource));
			break;
		case ERHIResourceType::ShaderModule:
			Device.DestroyShaderModule(static_cast<FRHIShaderModule*>(Resource));
			break;
		case ERHIResourceType::GraphicsPipeline:
			Device.DestroyGraphicsPipeline(static_cast<FRHIGraphicsPipeline*>(Resource));
			break;
		case ERHIResourceType::ComputePipeline:
			Device.DestroyComputePipeline(static_cast<FRHIComputePipeline*>(Resource));
			break;
		default:
			delete Resource;
			break;
		}
		return;
	}

	FPooledEntry Entry;
	Entry.Resource = Resource;
	Entry.Type = Resource->GetType();
	FreeList.push_back(Entry);
}

void FRHIResourceManager::FlushUnused()
{
	for (FPooledEntry& Entry : FreeList)
	{
		if (Entry.Resource == nullptr)
		{
			continue;
		}

		switch (Entry.Type)
		{
		case ERHIResourceType::Buffer:
			Device.DestroyBuffer(static_cast<FRHIBuffer*>(Entry.Resource));
			break;
		case ERHIResourceType::Texture:
			Device.DestroyTexture(static_cast<FRHITexture*>(Entry.Resource));
			break;
		case ERHIResourceType::Sampler:
			Device.DestroySampler(static_cast<FRHISampler*>(Entry.Resource));
			break;
		case ERHIResourceType::ShaderModule:
			Device.DestroyShaderModule(static_cast<FRHIShaderModule*>(Entry.Resource));
			break;
		case ERHIResourceType::GraphicsPipeline:
			Device.DestroyGraphicsPipeline(static_cast<FRHIGraphicsPipeline*>(Entry.Resource));
			break;
		case ERHIResourceType::ComputePipeline:
			Device.DestroyComputePipeline(static_cast<FRHIComputePipeline*>(Entry.Resource));
			break;
		default:
			delete Entry.Resource;
			break;
		}
		Entry.Resource = nullptr;
	}
	FreeList.clear();
}

void FRHIResourceManager::Shutdown()
{
	FlushUnused();

	// Snapshot first — Release() erases from Named and must not run while iterating it.
	std::vector<FRHIResource*> Snapshot;
	Snapshot.reserve(Named.size());
	for (const auto& Pair : Named)
	{
		if (Pair.second != nullptr)
		{
			Snapshot.push_back(Pair.second);
		}
	}
	Named.clear();

	for (FRHIResource* Resource : Snapshot)
	{
		Resource->RefCount = 0;
		switch (Resource->GetType())
		{
		case ERHIResourceType::Buffer:
			Device.DestroyBuffer(static_cast<FRHIBuffer*>(Resource));
			break;
		case ERHIResourceType::Texture:
			Device.DestroyTexture(static_cast<FRHITexture*>(Resource));
			break;
		case ERHIResourceType::Sampler:
			Device.DestroySampler(static_cast<FRHISampler*>(Resource));
			break;
		case ERHIResourceType::ShaderModule:
			Device.DestroyShaderModule(static_cast<FRHIShaderModule*>(Resource));
			break;
		case ERHIResourceType::GraphicsPipeline:
			Device.DestroyGraphicsPipeline(static_cast<FRHIGraphicsPipeline*>(Resource));
			break;
		case ERHIResourceType::ComputePipeline:
			Device.DestroyComputePipeline(static_cast<FRHIComputePipeline*>(Resource));
			break;
		default:
			delete Resource;
			break;
		}
	}
	LiveCount = 0;
}

std::size_t FRHIResourceManager::GetLiveCount() const
{
	return LiveCount;
}

std::size_t FRHIResourceManager::GetPooledCount() const
{
	return FreeList.size();
}

} // namespace Maho
