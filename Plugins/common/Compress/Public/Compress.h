#pragma once

// Compress — zstd (de)compression (engine Common, pure library). No state, no
// singleton — just static helpers. zstd is an ENGINE third-party (compiled C
// lib; Build/CMake/MahoDependencies.cmake populates + links the static lib).
//
//   std::vector<std::uint8_t> Compressed = Compress::Compress(Raw, 3);   // bytes
//   auto Raw = Compress::Decompress(Compressed);                          // raw bytes
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Maho
{
namespace Compress
{

/**
 * Compress a byte buffer with zstd. Returns nullopt on failure (e.g. too small
 * destination, encoder error). Level is zstd's 1..22 (negative/fast modes
 * unsupported); 0 = default.
 */
[[nodiscard]] std::optional<std::vector<std::uint8_t>> Compress(
	const std::vector<std::uint8_t>& Data,
	int Level = 0);

/**
 * Decompress a zstd payload. Returns nullopt when the input isn't valid zstd or
 * the decompressed size is implausible / can't be computed.
 */
[[nodiscard]] std::optional<std::vector<std::uint8_t>> Decompress(
	const std::vector<std::uint8_t>& Data);

/** Decompressed size of a zstd payload, when known (fails on corrupt input). */
[[nodiscard]] std::optional<std::size_t> GetDecompressedSize(
	const std::vector<std::uint8_t>& Data);

} // namespace Compress
} // namespace Maho
