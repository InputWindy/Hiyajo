#pragma once

#include "RHIAPI.h"
#include <RHI/RHIEnums.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Maho
{

struct FRHIMemoryAllocation
{
	void* Native = nullptr;
	void* Mapped = nullptr;
};

class MAHO_RHI_API IDynamicRHIMemoryAllocator
{
public:
	virtual ~IDynamicRHIMemoryAllocator() = default;

	virtual void Free(FRHIMemoryAllocation& Alloc) = 0;
	virtual void* Map(FRHIMemoryAllocation& Alloc) = 0;
	virtual void Unmap(FRHIMemoryAllocation& Alloc) = 0;
};

class MAHO_RHI_API FRHIResource
{
public:
	virtual ~FRHIResource() = default;

	FRHIResource(const FRHIResource&) = delete;
	FRHIResource& operator=(const FRHIResource&) = delete;

	[[nodiscard]] virtual const char* GetTypeName() const = 0;
	[[nodiscard]] virtual ERHIResourceType GetType() const = 0;
	[[nodiscard]] const std::string& GetDebugName() const
	{
		return DebugName;
	}

protected:
	FRHIResource() = default;

	std::string DebugName;
	std::uint32_t RefCount = 1;
};

struct FRHIBufferDesc
{
	std::uint64_t Size = 0;
	ERHIBufferUsage Usage = ERHIBufferUsage::None;
	ERHIMemoryUsage MemoryUsage = ERHIMemoryUsage::GPUOnly;
};

class MAHO_RHI_API FRHIBuffer : public FRHIResource
{
public:
	[[nodiscard]] const char* GetTypeName() const override
	{
		return "FRHIBuffer";
	}
	[[nodiscard]] ERHIResourceType GetType() const override
	{
		return ERHIResourceType::Buffer;
	}
	[[nodiscard]] virtual const FRHIBufferDesc& GetDesc() const = 0;

	/** GPU device address (uint64) when FRHIBufferDesc::Usage has DeviceAddress; 0 otherwise. */
	[[nodiscard]] virtual std::uint64_t GetDeviceAddress() const
	{
		return 0;
	}
};

struct FRHIStructuredBufferDesc
{
	std::uint64_t Size = 0;
	std::uint32_t Stride = 0;
	std::uint32_t ElementCount = 0;
	ERHIBufferUsage Usage = ERHIBufferUsage::Storage;
	ERHIMemoryUsage MemoryUsage = ERHIMemoryUsage::GPUOnly;
};

class MAHO_RHI_API FRHIStructuredBuffer : public FRHIResource
{
public:
	[[nodiscard]] const char* GetTypeName() const override
	{
		return "FRHIStructuredBuffer";
	}
	[[nodiscard]] ERHIResourceType GetType() const override
	{
		return ERHIResourceType::StructuredBuffer;
	}
	[[nodiscard]] virtual const FRHIStructuredBufferDesc& GetDesc() const = 0;
	[[nodiscard]] virtual FRHIBuffer* GetUnderlyingBuffer() = 0;
};

struct FRHIBufferViewDesc
{
	FRHIBuffer* Buffer = nullptr;
	std::uint64_t Offset = 0;
	std::uint64_t Range = 0;
	ERHIFormat Format = ERHIFormat::Unknown;
	std::uint32_t Stride = 0;
};

class MAHO_RHI_API FRHIBufferView : public FRHIResource
{
public:
	[[nodiscard]] const char* GetTypeName() const override
	{
		return "FRHIBufferView";
	}
	[[nodiscard]] ERHIResourceType GetType() const override
	{
		return ERHIResourceType::BufferView;
	}
};

struct FRHIExtent3D
{
	std::uint32_t Width = 1;
	std::uint32_t Height = 1;
	std::uint32_t Depth = 1;
};

struct FRHITextureDesc
{
	ERHIFormat Format = ERHIFormat::Unknown;
	ERHITextureDimension Dimension = ERHITextureDimension::Tex2D;
	FRHIExtent3D Extent{};
	std::uint32_t MipLevels = 1;
	std::uint32_t ArrayLayers = 1;
	ERHITextureUsage Usage = ERHITextureUsage::None;
	ERHIMemoryUsage MemoryUsage = ERHIMemoryUsage::GPUOnly;
};

class MAHO_RHI_API FRHITexture : public FRHIResource
{
public:
	[[nodiscard]] const char* GetTypeName() const override
	{
		return "FRHITexture";
	}
	[[nodiscard]] ERHIResourceType GetType() const override
	{
		return ERHIResourceType::Texture;
	}
	[[nodiscard]] virtual const FRHITextureDesc& GetDesc() const = 0;
};

struct FRHITextureViewDesc
{
	FRHITexture* Texture = nullptr;
	ERHIFormat Format = ERHIFormat::Unknown;
	std::uint32_t BaseMip = 0;
	std::uint32_t MipCount = 1;
	std::uint32_t BaseArrayLayer = 0;
	std::uint32_t ArrayLayerCount = 1;
};

class MAHO_RHI_API FRHITextureView : public FRHIResource
{
public:
	[[nodiscard]] const char* GetTypeName() const override
	{
		return "FRHITextureView";
	}
	[[nodiscard]] ERHIResourceType GetType() const override
	{
		return ERHIResourceType::TextureView;
	}
};

struct FRHISamplerDesc
{
	ERHIFilter MinFilter = ERHIFilter::Linear;
	ERHIFilter MagFilter = ERHIFilter::Linear;
	ERHIAddressMode AddressU = ERHIAddressMode::Repeat;
	ERHIAddressMode AddressV = ERHIAddressMode::Repeat;
	ERHIAddressMode AddressW = ERHIAddressMode::Repeat;
	float LodBias = 0.0f;
	float MinLod = 0.0f;
	float MaxLod = 1000.0f;
};

class MAHO_RHI_API FRHISampler : public FRHIResource
{
public:
	[[nodiscard]] const char* GetTypeName() const override
	{
		return "FRHISampler";
	}
	[[nodiscard]] ERHIResourceType GetType() const override
	{
		return ERHIResourceType::Sampler;
	}
	[[nodiscard]] virtual const FRHISamplerDesc& GetDesc() const = 0;
};

struct FRHIShaderModuleDesc
{
	ERHIShaderStage Stage = ERHIShaderStage::None;
	const void* Bytecode = nullptr;
	std::size_t BytecodeSize = 0;
	const char* EntryPoint = "main";
};

class MAHO_RHI_API FRHIShaderModule : public FRHIResource
{
public:
	[[nodiscard]] const char* GetTypeName() const override
	{
		return "FRHIShaderModule";
	}
	[[nodiscard]] ERHIResourceType GetType() const override
	{
		return ERHIResourceType::ShaderModule;
	}
};

struct FRHIDescriptorBinding
{
	std::uint32_t Binding = 0;
	ERHIDescriptorType Type = ERHIDescriptorType::UniformBuffer;
	std::uint32_t Count = 1;
	ERHIShaderStage Stages = ERHIShaderStage::None;
	bool bPartiallyBound = false;
	bool bVariableCount = false;
};

struct FRHIDescriptorSetLayoutDesc
{
	std::vector<FRHIDescriptorBinding> Bindings;
};

class MAHO_RHI_API FRHIDescriptorSetLayout : public FRHIResource
{
public:
	[[nodiscard]] const char* GetTypeName() const override
	{
		return "FRHIDescriptorSetLayout";
	}
	[[nodiscard]] ERHIResourceType GetType() const override
	{
		return ERHIResourceType::DescriptorSetLayout;
	}
};

struct FRHIPushConstantRange
{
	ERHIShaderStage Stages = ERHIShaderStage::None;
	std::uint32_t Offset = 0;
	std::uint32_t Size = 0;
};

struct FRHIPipelineLayoutDesc
{
	std::vector<FRHIDescriptorSetLayout*> SetLayouts;
	std::vector<FRHIPushConstantRange> PushConstants;
};

class MAHO_RHI_API FRHIPipelineLayout : public FRHIResource
{
public:
	[[nodiscard]] const char* GetTypeName() const override
	{
		return "FRHIPipelineLayout";
	}
	[[nodiscard]] ERHIResourceType GetType() const override
	{
		return ERHIResourceType::PipelineLayout;
	}
};

struct FRHIVertexAttribute
{
	std::uint32_t Location = 0;
	ERHIFormat Format = ERHIFormat::Unknown;
	std::uint32_t Offset = 0;
};

// Forward declarations used by FRHIGraphicsPipelineDesc / FRHIFramebufferDesc
class FRHIRenderPass;
class FRHIFramebuffer;
class FRHITextureView;

struct FRHIAttachmentBlend
{
	bool bBlend = false;
	ERHIBlendFactor SrcColorFactor = ERHIBlendFactor::One;
	ERHIBlendFactor DstColorFactor = ERHIBlendFactor::Zero;
	ERHIBlendFactor SrcAlphaFactor = ERHIBlendFactor::One;
	ERHIBlendFactor DstAlphaFactor = ERHIBlendFactor::Zero;
	ERHIBlendOp ColorOp = ERHIBlendOp::Add;
	ERHIBlendOp AlphaOp = ERHIBlendOp::Add;
};

struct FRHIGraphicsPipelineDesc
{
	FRHIShaderModule* VertexShader = nullptr;
	FRHIShaderModule* FragmentShader = nullptr;
	const char* VertexEntryPoint = "main";
	const char* FragmentEntryPoint = "main";
	FRHIPipelineLayout* Layout = nullptr;
	FRHIRenderPass* RenderPass = nullptr;
	ERHIPrimitiveTopology Topology = ERHIPrimitiveTopology::TriangleList;
	std::uint32_t VertexStride = 0;
	std::vector<FRHIVertexAttribute> Attributes;
	ERHICullMode CullMode = ERHICullMode::Back;
	ERHIFillMode FillMode = ERHIFillMode::Solid;
	ERHIFormat ColorFormat = ERHIFormat::B8G8R8A8_UNORM;
	ERHIFormat DepthFormat = ERHIFormat::Unknown;
	std::uint32_t SampleCount = 1;
	bool bDepthTest = false;
	bool bDepthWrite = false;
	ERHICompareOp DepthCompare = ERHICompareOp::Less;
	std::vector<FRHIAttachmentBlend> AttachmentBlends;
	bool bAlphaToCoverage = false;
};

class MAHO_RHI_API FRHIGraphicsPipeline : public FRHIResource
{
public:
	[[nodiscard]] const char* GetTypeName() const override
	{
		return "FRHIGraphicsPipeline";
	}
	[[nodiscard]] ERHIResourceType GetType() const override
	{
		return ERHIResourceType::GraphicsPipeline;
	}
};

struct FRHIComputePipelineDesc
{
	FRHIShaderModule* ComputeShader = nullptr;
	const char* ComputeEntryPoint = "main";
	FRHIPipelineLayout* Layout = nullptr;
};

class MAHO_RHI_API FRHIComputePipeline : public FRHIResource
{
public:
	[[nodiscard]] const char* GetTypeName() const override
	{
		return "FRHIComputePipeline";
	}
	[[nodiscard]] ERHIResourceType GetType() const override
	{
		return ERHIResourceType::ComputePipeline;
	}
};

struct FRHIRayTracingPipelineDesc
{
	FRHIShaderModule* RayGen = nullptr;
	std::vector<FRHIShaderModule*> Miss;        // miss shaders
	std::vector<FRHIShaderModule*> ClosestHit;  // closest-hit shaders
	std::vector<FRHIShaderModule*> AnyHit;      // any-hit shaders (optional)
	std::vector<FRHIShaderModule*> Intersection; // intersection shaders (optional)
	std::vector<FRHIShaderModule*> Callable;    // callable shaders (optional)
	std::vector<const char*> EntryPoints;       // entry for each module (same order)
	FRHIPipelineLayout* Layout = nullptr;
	std::uint32_t MaxRecursionDepth = 1;
};

class MAHO_RHI_API FRHIRayTracingPipeline : public FRHIResource
{
public:
	[[nodiscard]] const char* GetTypeName() const override
	{
		return "FRHIRayTracingPipeline";
	}
	[[nodiscard]] ERHIResourceType GetType() const override
	{
		return ERHIResourceType::RayTracingPipeline;
	}
};

struct FRHIDescriptorPoolSize
{
	ERHIDescriptorType Type = ERHIDescriptorType::UniformBuffer;
	std::uint32_t Count = 0;
};

struct FRHIDescriptorPoolDesc
{
	std::uint32_t MaxSets = 0;
	std::vector<FRHIDescriptorPoolSize> PoolSizes;
	bool bUpdateAfterBind = false;
};

class MAHO_RHI_API FRHIDescriptorPool : public FRHIResource
{
public:
	[[nodiscard]] const char* GetTypeName() const override
	{
		return "FRHIDescriptorPool";
	}
	[[nodiscard]] ERHIResourceType GetType() const override
	{
		return ERHIResourceType::DescriptorPool;
	}
};

class MAHO_RHI_API FRHIDescriptorSet : public FRHIResource
{
public:
	[[nodiscard]] const char* GetTypeName() const override
	{
		return "FRHIDescriptorSet";
	}
	[[nodiscard]] ERHIResourceType GetType() const override
	{
		return ERHIResourceType::DescriptorSet;
	}
};

class MAHO_RHI_API FRHIFramebuffer : public FRHIResource
{
public:
	[[nodiscard]] const char* GetTypeName() const override
	{
		return "FRHIFramebuffer";
	}
	[[nodiscard]] ERHIResourceType GetType() const override
	{
		return ERHIResourceType::Framebuffer;
	}
};

class MAHO_RHI_API FRHIRenderPass : public FRHIResource
{
public:
	[[nodiscard]] const char* GetTypeName() const override
	{
		return "FRHIRenderPass";
	}
	[[nodiscard]] ERHIResourceType GetType() const override
	{
		return ERHIResourceType::RenderPass;
	}
};

struct FRHIRenderPassAttachment
{
	ERHIFormat Format = ERHIFormat::B8G8R8A8_UNORM;
	std::uint32_t SampleCount = 1;
	ERHILoadOp LoadOp = ERHILoadOp::Clear;
	ERHIStoreOp StoreOp = ERHIStoreOp::Store;
};

struct FRHIRenderPassDesc
{
	std::vector<FRHIRenderPassAttachment> ColorAttachments;
	ERHIFormat DepthFormat = ERHIFormat::Unknown;
	std::uint32_t SampleCount = 1;
};

struct FRHIFramebufferDesc
{
	FRHIRenderPass* RenderPass = nullptr;
	std::vector<FRHITextureView*> Attachments;
	std::uint32_t Width = 0;
	std::uint32_t Height = 0;
};

class MAHO_RHI_API FRHICommandPool : public FRHIResource
{
public:
	[[nodiscard]] const char* GetTypeName() const override
	{
		return "FRHICommandPool";
	}
	[[nodiscard]] ERHIResourceType GetType() const override
	{
		return ERHIResourceType::CommandPool;
	}
};

class MAHO_RHI_API FRHIFence : public FRHIResource
{
public:
	[[nodiscard]] const char* GetTypeName() const override
	{
		return "FRHIFence";
	}
	[[nodiscard]] ERHIResourceType GetType() const override
	{
		return ERHIResourceType::Fence;
	}
};

class MAHO_RHI_API FRHISemaphore : public FRHIResource
{
public:
	[[nodiscard]] const char* GetTypeName() const override
	{
		return "FRHISemaphore";
	}
	[[nodiscard]] ERHIResourceType GetType() const override
	{
		return ERHIResourceType::Semaphore;
	}
};

class MAHO_RHI_API FRHIQueryPool : public FRHIResource
{
public:
	[[nodiscard]] const char* GetTypeName() const override
	{
		return "FRHIQueryPool";
	}
	[[nodiscard]] ERHIResourceType GetType() const override
	{
		return ERHIResourceType::QueryPool;
	}
};

enum class ERHIRayTracingStructureType : std::uint8_t
{
	TopLevel = 0,       // TLAS: instance array
	BottomLevel = 1,    // BLAS: geometry
};

/** One geometry in a bottom-level AS (triangle data via vertex/index buffers). */
struct FRHIRayTracingGeometry
{
	FRHIBuffer* VertexBuffer = nullptr;
	std::uint64_t VertexBufferOffset = 0;
	std::uint32_t VertexCount = 0;
	std::uint32_t VertexStride = 12;      // bytes per vertex
	FRHIBuffer* IndexBuffer = nullptr;
	std::uint64_t IndexBufferOffset = 0;
	std::uint32_t IndexCount = 0;
	bool bIndex32 = true;
	bool bOpaque = true;                  // no any-hit (VK_GEOMETRY_OPAQUE_BIT)
};

struct FRHIRayTracingGeometryDesc
{
	std::vector<FRHIRayTracingGeometry> Geometries;
	bool bAllowUpdate = false;            // BLAS refit
	bool bAllowCompaction = false;
};

/** One TLAS instance (placement-matrix + AS handle + instance id). */
struct FRHIRayTracingInstance
{
	std::uint32_t InstanceId = 0;
	std::uint32_t InstanceMask = 0xFF;
	std::uint32_t SbtOffset = 0;          // shader-binding-table offset (per-instance)
	FRHIBuffer* AccelerationStructure = nullptr;  // the TLAS to instance
	// 12 floats row-major: column0, column1, column2, column3 (transposed world)
	float Transform[12] = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0
	};
};

class MAHO_RHI_API FRHIAccelerationStructure : public FRHIResource
{
public:
	[[nodiscard]] const char* GetTypeName() const override
	{
		return "FRHIAccelerationStructure";
	}
	[[nodiscard]] ERHIResourceType GetType() const override
	{
		return ERHIResourceType::AccelerationStructure;
	}

	[[nodiscard]] const FRHIRayTracingGeometryDesc& GetGeometryDesc() const
	{
		return GeometryDesc;
	}

protected:
	FRHIRayTracingGeometryDesc GeometryDesc;
};

struct FRHISbtRecord
{
	FRHIShaderModule* Module = nullptr;   // null 鈫?miss/empty record
	const char* EntryPoint = "main";
};

/** Shader binding table layout (one group per stage). */
struct FRHISbtGroup
{
	ERHIShaderStage Stage = ERHIShaderStage::RayGen;
	std::vector<FRHISbtRecord> Records;   // rayGen/miss/callable: records; hit: per-hit records
};

struct FRHIDescriptorWrite
{
	FRHIDescriptorSet* Set = nullptr;
	std::uint32_t Binding = 0;
	std::uint32_t ArrayIndex = 0;
	ERHIDescriptorType Type = ERHIDescriptorType::UniformBuffer;
	FRHIBuffer* Buffer = nullptr;
	std::uint64_t Offset = 0;
	std::uint64_t Range = 0;
	FRHITextureView* TextureView = nullptr;
	FRHISampler* Sampler = nullptr;
};

} // namespace Maho
