#pragma once

#include <cstdint>

namespace Maho
{
namespace Render
{
namespace Detail
{

/**
 * Write an 8-bit RGBA image to a PNG file.
 *
 * Deliberately dependency-free: the engine has no image ENCODER (the asset codec only decodes),
 * and a debug screenshot is not worth pulling zlib in. PNG's IDAT is a zlib stream, so this emits
 * the stream by hand with STORED (uncompressed) DEFLATE blocks -- every reader accepts them, the
 * file is just larger than a compressed one. That is the right trade for a screenshot: no new
 * third-party code in the render path, and the bytes are exactly what the GPU handed back.
 *
 * Returns false (after logging) when the file cannot be created or written.
 */
bool WritePngRGBA(const char* Path, std::uint32_t Width, std::uint32_t Height,
	const std::uint8_t* Rgba);

} // namespace Detail
} // namespace Render
} // namespace Maho
