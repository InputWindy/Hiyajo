#pragma once

/**
 * GPU Scene data structures for the GPU-driven forward renderer.
 * Instance data lives in a storage buffer; the culling compute shader
 * writes DrawIndexedIndirect commands; the raster pass draws indirectly.
 */

#include <cstdint>

namespace Maho
{

/** Max instances the GPU scene can hold (fixed pool, grows later). */
constexpr std::uint32_t GPUSceneMaxInstances = 4096;

/** Max draw commands the indirect buffer can hold (one per visible instance). */
constexpr std::uint32_t GPUSceneMaxDraws = GPUSceneMaxInstances;

/**
 * Per-instance data (storage buffer, std430).
 * 80 bytes — matches GLSL layout:
 *   mat4 LocalToWorld;   // 64
 *   vec4 Color;          // 16
 */
struct FGPUSceneInstance
{
	float LocalToWorld[16];
	float Color[4];
};
static_assert(sizeof(FGPUSceneInstance) == 80, "FGPUSceneInstance must be 80 bytes");

/**
 * DrawIndexedIndirect command (VkDrawIndexedIndirectCommand layout).
 * Written by culling compute, consumed by vkCmdDrawIndexedIndirect.
 */
struct FDrawIndexedIndirectArgs
{
	std::uint32_t IndexCount = 0;
	std::uint32_t InstanceCount = 0;
	std::uint32_t FirstIndex = 0;
	std::int32_t VertexOffset = 0;
	std::uint32_t FirstInstance = 0;
};
static_assert(sizeof(FDrawIndexedIndirectArgs) == 20, "FDrawIndexedIndirectArgs must be 20 bytes");

/**
 * Culling uniforms (push constant).
 * Frustum planes in world space (6 planes, each 4 floats).
 */
struct FGPUCullParams
{
	float FrustumPlanes[6][4];   // 96 bytes
	std::uint32_t InstanceCount; // 4
	std::uint32_t Pad[3];        // 12
};
static_assert(sizeof(FGPUCullParams) == 112, "FGPUCullParams must be 112 bytes");

/** Simple unit-cube vertex (position + color), until real mesh proxy data is wired. */
struct FGPUCubeVertex
{
	float Pos[3];
	float Color[3];
};
static_assert(sizeof(FGPUCubeVertex) == 24, "FGPUCubeVertex must be 24 bytes");

} // namespace Maho
