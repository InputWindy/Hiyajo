#pragma once

#include <cstdint>
#include <type_traits>

namespace Maho
{

enum class ERHIBackend : std::uint8_t
{
	Vulkan = 0,
};

enum class ERHIQueueType : std::uint8_t
{
	Graphics = 0,
	Compute = 1,
	Transfer = 2,
};

enum class ERHICommandListType : std::uint8_t
{
	Graphics = 0,
	Compute = 1,
	Transfer = 2,
};

enum class ERHIResourceType : std::uint16_t
{
	Unknown = 0,
	Buffer,
	StructuredBuffer,
	BufferView,
	Texture,
	TextureView,
	Sampler,
	ShaderModule,
	DescriptorSetLayout,
	PipelineLayout,
	GraphicsPipeline,
	ComputePipeline,
	RayTracingPipeline,
	DescriptorPool,
	DescriptorSet,
	Framebuffer,
	RenderPass,
	CommandPool,
	Fence,
	Semaphore,
	QueryPool,
	AccelerationStructure,
};

enum class ERHIFormat : std::uint16_t
{
	Unknown = 0,
	R8G8B8A8_UNORM,
	B8G8R8A8_UNORM,
	B8G8R8A8_SRGB,
	R32_SFLOAT,
	R32G32_SFLOAT,
	R32G32B32_SFLOAT,
	R16G16_SFLOAT,
	D24_UNORM_S8_UINT,
	D32_SFLOAT,
	// -- texture-mirror formats (asset-side ETexturePixelFormat coverage) --
	R8G8B8A8_SRGB,
	R16G16B16A16_SFLOAT,
	R32G32B32A32_SFLOAT,
	R8_UNORM,
	R8G8_UNORM,
	R8G8B8_UNORM,
	R16_SFLOAT,
};

enum class ERHITextureDimension : std::uint8_t
{
	Tex2D = 0,
	Tex2DArray,
	Cube,
	Tex3D,
};

enum class ERHIBufferUsage : std::uint32_t
{
	None = 0,
	Vertex = 1u << 0,
	Index = 1u << 1,
	Uniform = 1u << 2,
	Storage = 1u << 3,
	TransferSrc = 1u << 4,
	TransferDst = 1u << 5,
	Indirect = 1u << 6,
	DeviceAddress = 1u << 7,   // shader-addressable (VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
	AccelerationStructure = 1u << 8, // BLAS/TLAS storage (VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT)
};

enum class ERHITextureUsage : std::uint32_t
{
	None = 0,
	Sampled = 1u << 0,
	ColorAttachment = 1u << 1,
	DepthStencil = 1u << 2,
	Storage = 1u << 3,
	TransferSrc = 1u << 4,
	TransferDst = 1u << 5,
	Transient = 1u << 6,
};

enum class ERHIMemoryUsage : std::uint8_t
{
	GPUOnly = 0,
	CPUToGPU,
	GPUToCPU,
	CPUOnly,
};

enum class ERHIResourceState : std::uint16_t
{
	Common = 0,
	VertexBuffer,
	IndexBuffer,
	UniformBuffer,
	ShaderResource,
	UnorderedAccess,
	IndirectArgument,
	RenderTarget,
	DepthWrite,
	CopySrc,
	CopyDst,
	Present,
};

enum class ERHIQueryType : std::uint8_t
{
	Occlusion = 0,      // binary occlusion (VK_QUERY_TYPE_OCCLUSION)
	Timestamp = 1,      // GPU timestamp (VK_QUERY_TYPE_TIMESTAMP)
};

enum class ERHIDescriptorType : std::uint8_t
{
	Sampler = 0,
	CombinedImageSampler,
	SampledImage,
	StorageImage,
	UniformBuffer,
	StorageBuffer,
	DynamicUniform,
	DynamicStorage,
	AccelerationStructure,   // readonly AS binding (VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
};

enum class ERHIShaderStage : std::uint32_t
{
	None = 0,
	Vertex = 1u << 0,
	Fragment = 1u << 1,
	Compute = 1u << 2,
	RayGen = 1u << 3,
	AnyHit = 1u << 4,
	ClosestHit = 1u << 5,
	Miss = 1u << 6,
	Intersection = 1u << 7,
	Callable = 1u << 8,
	AllGraphics = Vertex | Fragment,
	AllRayTracing = RayGen | AnyHit | ClosestHit | Miss | Intersection | Callable,

	MAX_COUNT = 9,
};

enum class ERHIPrimitiveTopology : std::uint8_t
{
	TriangleList = 0,
	TriangleStrip,
	LineList,
	PointList,
};

enum class ERHICullMode : std::uint8_t
{
	None = 0,
	Front,
	Back,
};

enum class ERHIFillMode : std::uint8_t
{
	Solid = 0,
	Wireframe,
};

enum class ERHICompareOp : std::uint8_t
{
	Never = 0,
	Less,
	Equal,
	LessOrEqual,
	Greater,
	NotEqual,
	GreaterOrEqual,
	Always,
};

enum class ERHIBlendFactor : std::uint8_t
{
	Zero = 0,
	One,
	SrcColor,
	OneMinusSrcColor,
	DstColor,
	OneMinusDstColor,
	SrcAlpha,
	OneMinusSrcAlpha,
	DstAlpha,
	OneMinusDstAlpha,
};

enum class ERHIBlendOp : std::uint8_t
{
	Add = 0,
	Subtract,
	ReverseSubtract,
	Min,
	Max,
};

enum class ERHILoadOp : std::uint8_t
{
	Load = 0,
	Clear,
	DontCare,
};

enum class ERHIStoreOp : std::uint8_t
{
	Store = 0,
	DontCare,
};

enum class ERHIFilter : std::uint8_t
{
	Nearest = 0,
	Linear,
};

enum class ERHIAddressMode : std::uint8_t
{
	Repeat = 0,
	MirroredRepeat,
	ClampToEdge,
	ClampToBorder,
};

template <typename TEnum>
[[nodiscard]] constexpr TEnum RHIEnumOr(TEnum A, TEnum B)
{
	using U = std::underlying_type_t<TEnum>;
	return static_cast<TEnum>(static_cast<U>(A) | static_cast<U>(B));
}

template <typename TEnum>
[[nodiscard]] constexpr bool RHIEnumHas(TEnum Mask, TEnum Flag)
{
	using U = std::underlying_type_t<TEnum>;
	return (static_cast<U>(Mask) & static_cast<U>(Flag)) != 0;
}

[[nodiscard]] constexpr ERHIBufferUsage operator|(ERHIBufferUsage A, ERHIBufferUsage B)
{
	return RHIEnumOr(A, B);
}

[[nodiscard]] constexpr ERHITextureUsage operator|(ERHITextureUsage A, ERHITextureUsage B)
{
	return RHIEnumOr(A, B);
}

[[nodiscard]] constexpr ERHIShaderStage operator|(ERHIShaderStage A, ERHIShaderStage B)
{
	return RHIEnumOr(A, B);
}

} // namespace Maho
