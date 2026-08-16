#pragma once

/**
 * Private CPU image codec for ResourceIO.
 * Raster: OpenImageIO when MAHO_WITH_OPENIMAGEIO, else Windows WIC.
 * KTX2: KTX-Software (libktx) for Import/Export round-trip.
 * Game-thread only - never touches GPU / RHI.
 * DOTS-aligned: FTexture types instead of UTexture.
 */

#include "ResourceTypes.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Maho
{

struct FDecodedImage
{
	ETextureDimension Dimension = ETextureDimension::Tex2D;
	ETexturePixelFormat Format = ETexturePixelFormat::Unknown;
	std::uint32_t Width = 0;
	std::uint32_t Height = 0;
	std::uint32_t Depth = 1;
	std::uint32_t ArrayLayers = 1;
	std::uint32_t MipCount = 1;
	bool bSRGB = true;
	std::vector<std::uint8_t> Pixels;
};

namespace TextureImageCodec
{

[[nodiscard]] std::string GetExtensionLower(std::string_view Path);
[[nodiscard]] bool IsKtx2Extension(std::string_view Ext);
[[nodiscard]] bool IsRasterExtension(std::string_view Ext);

[[nodiscard]] bool PathLooksLikeCube(std::string_view Path);
[[nodiscard]] bool PathLooksLikeCubeArray(std::string_view Path);
[[nodiscard]] bool PathLooksLikeTexture3D(std::string_view Path);
[[nodiscard]] bool PathLooksLikeTexture2DArray(std::string_view Path);

[[nodiscard]] bool DecodeFromMemory(
	const std::uint8_t* Bytes,
	std::size_t ByteCount,
	std::string_view SourcePathHint,
	FDecodedImage& Out);

[[nodiscard]] bool EncodeToFile(
	const FTexture& Texture,
	const std::string& DestinationPath,
	bool bOverwrite);

[[nodiscard]] bool EncodePngToMemory(const FTexture& Texture, std::vector<std::uint8_t>& OutBytes);

[[nodiscard]] bool ApplyDecodedToTexture(FTexture& Texture, FDecodedImage&& Image);

} // namespace TextureImageCodec
} // namespace Maho
