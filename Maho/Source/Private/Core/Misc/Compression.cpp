#include <Core/Misc/Compression.h>

#include <Core/Misc/Log.h>

#if defined(MAHO_WITH_ZLIB) && MAHO_WITH_ZLIB
#	include <zlib.h>
#endif

namespace Maho
{

bool FCompression::CompressZlib(
	const std::uint8_t* Source,
	std::size_t SourceSize,
	std::vector<std::uint8_t>& OutCompressed)
{
	OutCompressed.clear();
#if !(defined(MAHO_WITH_ZLIB) && MAHO_WITH_ZLIB)
	(void)Source;
	(void)SourceSize;
	MAHO_CORE_ERROR("FCompression::CompressZlib: zlib unavailable");
	return false;
#else
	if (!Source && SourceSize != 0)
	{
		return false;
	}

	uLong Bound = compressBound(static_cast<uLong>(SourceSize));
	OutCompressed.resize(static_cast<std::size_t>(Bound));
	uLongf DestLen = Bound;
	const int Rc = compress2(
		OutCompressed.data(),
		&DestLen,
		Source,
		static_cast<uLong>(SourceSize),
		Z_DEFAULT_COMPRESSION);
	if (Rc != Z_OK)
	{
		OutCompressed.clear();
		MAHO_CORE_ERROR("FCompression::CompressZlib: compress2 failed ({})", Rc);
		return false;
	}
	OutCompressed.resize(static_cast<std::size_t>(DestLen));
	return true;
#endif
}

bool FCompression::DecompressZlib(
	const std::uint8_t* Compressed,
	std::size_t CompressedSize,
	std::size_t UncompressedSize,
	std::vector<std::uint8_t>& OutUncompressed)
{
	OutUncompressed.clear();
#if !(defined(MAHO_WITH_ZLIB) && MAHO_WITH_ZLIB)
	(void)Compressed;
	(void)CompressedSize;
	(void)UncompressedSize;
	MAHO_CORE_ERROR("FCompression::DecompressZlib: zlib unavailable");
	return false;
#else
	if ((!Compressed && CompressedSize != 0) || UncompressedSize == 0)
	{
		return false;
	}

	OutUncompressed.resize(UncompressedSize);
	uLongf DestLen = static_cast<uLongf>(UncompressedSize);
	const int Rc = uncompress(
		OutUncompressed.data(),
		&DestLen,
		Compressed,
		static_cast<uLong>(CompressedSize));
	if (Rc != Z_OK || static_cast<std::size_t>(DestLen) != UncompressedSize)
	{
		OutUncompressed.clear();
		MAHO_CORE_ERROR(
			"FCompression::DecompressZlib: uncompress failed (rc={} dest={})",
			Rc,
			static_cast<std::uint64_t>(DestLen));
		return false;
	}
	return true;
#endif
}

} // namespace Maho
