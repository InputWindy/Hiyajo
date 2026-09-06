#pragma once

// CPU texture image decode: raw file bytes -> FDecodedImage (RGBA8 pixels). The
// texture importer feeds the image into an FTexture. Only raster formats (WIC)
// for now; KTX2 can be layered in later behind the same DecodeFromMemory entry.
// Implementation detail: used only inside this plugin's own importer.

#include "AssetTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Maho
{
namespace Resource
{

/** A decoded CPU image payload (the importer copies its fields into an FTexture
 *  via the texture payload constructor). Internal to this plugin. */
struct FDecodedImage
{
	ETextureDimension Dimension = ETextureDimension::Tex2D;
	ETexturePixelFormat Format = ETexturePixelFormat::RGBA8;
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
	/** Decode an in-memory raster image into Out (RGBA8). SourcePath is used only
	 *  as a format hint (extension sniff); the bytes carry the payload. */
	[[nodiscard]] bool DecodeFromMemory(
		const std::uint8_t* Bytes,
		std::size_t ByteCount,
		std::string_view SourcePath,
		FDecodedImage& Out);

	/** Lower-cased file extension (".png" / ".jpg" ...); empty when none. */
	[[nodiscard]] std::string GetExtensionLower(std::string_view Path);

	/** Is this a raster extension the WIC codec can decode? */
	[[nodiscard]] bool IsRasterExtension(std::string_view Ext);
} // namespace TextureImageCodec

} // namespace Resource
} // namespace Maho
