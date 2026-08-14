#pragma once

/**
 * Thin zlib wrappers for package / BulkData compression.
 * Linked against Assimp's zlibstatic when MAHO_WITH_ASSIMP is enabled.
 */

#include <Core/Misc/Export.h>

#include <cstdint>
#include <vector>

namespace Maho
{

class MAHO_API FCompression
{
public:
	/** zlib compress2 (default level). Returns false on failure. */
	[[nodiscard]] static bool CompressZlib(
		const std::uint8_t* Source,
		std::size_t SourceSize,
		std::vector<std::uint8_t>& OutCompressed);

	/**
	 * zlib uncompress into a buffer of exactly UncompressedSize bytes.
	 * Out is resized to UncompressedSize on success.
	 */
	[[nodiscard]] static bool DecompressZlib(
		const std::uint8_t* Compressed,
		std::size_t CompressedSize,
		std::size_t UncompressedSize,
		std::vector<std::uint8_t>& OutUncompressed);
};

} // namespace Maho
