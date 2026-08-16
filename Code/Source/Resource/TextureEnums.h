#pragma once

/**
 * Texture-facing enums used by the project resource types (FTexture family).
 * Asset-side concepts only; the core resource layer must not depend on these.
 */

#include <cstdint>

namespace Maho
{

enum class ETexturePixelFormat : std::uint8_t
{
	Unknown = 0,
	RGBA8,
	RGBA16F,
	RGBA32F,
	R8,
	RG8,
	RGB8,
	BlockCompressed,
	R16F,
	DXT1,
	DXT5,
	BC7,
	Count,
};

enum class ETextureDimension : std::uint8_t
{
	Unknown = 0,
	Tex1D,
	Tex2D,
	Tex3D,
	TexCube,
	Cube = TexCube,
	TexCubeArray,
	Tex2DArray,
	CubeArray = TexCubeArray, // Project alias
	Count,
};

} // namespace Maho
