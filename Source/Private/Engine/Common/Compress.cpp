#include <Engine/Common/Compress.h>

#include <zstd.h>

#include <utility>

namespace Maho::Compress
{

std::optional<std::vector<std::uint8_t>> Compress(
	const std::vector<std::uint8_t>& Data,
	int Level)
{
	if (Data.empty())
	{
		return std::vector<std::uint8_t>{};
	}

	const std::size_t Bound = ZSTD_compressBound(Data.size());
	if (ZSTD_isError(Bound))
	{
		return std::nullopt;
	}

	std::vector<std::uint8_t> Out(Bound);
	const std::size_t Written = ZSTD_compress(
		Out.data(), Out.size(), Data.data(), Data.size(), Level);
	if (ZSTD_isError(Written))
	{
		return std::nullopt;
	}

	Out.resize(Written);
	return Out;
}

std::optional<std::vector<std::uint8_t>> Decompress(
	const std::vector<std::uint8_t>& Data)
{
	const auto SizeOpt = GetDecompressedSize(Data);
	if (!SizeOpt)
	{
		return std::nullopt;
	}

	std::vector<std::uint8_t> Out(*SizeOpt);
	const std::size_t Written = ZSTD_decompress(
		Out.data(), Out.size(), Data.data(), Data.size());
	if (ZSTD_isError(Written))
	{
		return std::nullopt;
	}

	Out.resize(Written);
	return Out;
}

std::optional<std::size_t> GetDecompressedSize(
	const std::vector<std::uint8_t>& Data)
{
	const unsigned long long Size = ZSTD_getFrameContentSize(Data.data(), Data.size());
	if (ZSTD_isError(Size) || Size == ZSTD_CONTENTSIZE_ERROR || Size == ZSTD_CONTENTSIZE_UNKNOWN)
	{
		return std::nullopt;
	}
	return static_cast<std::size_t>(Size);
}

} // namespace Maho::Compress
