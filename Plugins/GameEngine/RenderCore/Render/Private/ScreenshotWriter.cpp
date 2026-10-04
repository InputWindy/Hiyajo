#include "ScreenshotWriter.h"

#include <Log.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace Maho
{
namespace Render
{
namespace Detail
{

namespace
{

/** PNG chunk CRCs and the zlib stream's Adler-32 checksum. Both are checksums of bytes this file
 *  just produced, so a bug in either shows up immediately as "the image does not open" -- there is
 *  nothing to be clever about, just the polynomial from the spec. */
std::uint32_t Crc32(const std::uint8_t* Data, std::size_t Size)
{
	std::uint32_t Crc = 0xFFFFFFFFu;
	for (std::size_t I = 0; I < Size; ++I)
	{
		Crc ^= Data[I];
		for (int Bit = 0; Bit < 8; ++Bit)
		{
			Crc = (Crc & 1u) ? ((Crc >> 1) ^ 0xEDB88320u) : (Crc >> 1);
		}
	}
	return ~Crc;
}

std::uint32_t Adler32(const std::uint8_t* Data, std::size_t Size)
{
	std::uint32_t A = 1;
	std::uint32_t B = 0;
	for (std::size_t I = 0; I < Size; ++I)
	{
		A = (A + Data[I]) % 65521u;
		B = (B + A) % 65521u;
	}
	return (B << 16) | A;
}

void PushBE32(std::vector<std::uint8_t>& Out, std::uint32_t Value)
{
	Out.push_back(static_cast<std::uint8_t>((Value >> 24) & 0xFFu));
	Out.push_back(static_cast<std::uint8_t>((Value >> 16) & 0xFFu));
	Out.push_back(static_cast<std::uint8_t>((Value >> 8) & 0xFFu));
	Out.push_back(static_cast<std::uint8_t>(Value & 0xFFu));
}

/** One length / type / payload / CRC chunk. */
void PushChunk(std::vector<std::uint8_t>& Out, const char Type[4], const std::uint8_t* Data,
	std::size_t Size)
{
	PushBE32(Out, static_cast<std::uint32_t>(Size));

	const std::size_t CrcStart = Out.size();
	Out.insert(Out.end(), Type, Type + 4);
	if (Size > 0)
	{
		Out.insert(Out.end(), Data, Data + Size);
	}
	PushBE32(Out, Crc32(Out.data() + CrcStart, 4 + Size));
}

/** zlib wrapper around STORED deflate blocks: 0x78 0x01 header, then blocks of at most 64 KiB
 *  (each [final bit][type 00][LEN][~LEN][raw bytes]), then the Adler-32 of the raw data. */
void PushZlibStored(std::vector<std::uint8_t>& Out, const std::uint8_t* Raw, std::size_t Size)
{
	Out.push_back(0x78);
	Out.push_back(0x01);

	constexpr std::size_t kMaxBlock = 65535;
	std::size_t Offset = 0;
	do
	{
		const std::size_t Remaining = Size - Offset;
		const std::size_t BlockSize = Remaining < kMaxBlock ? Remaining : kMaxBlock;
		const bool bFinal = (Offset + BlockSize) >= Size;

		Out.push_back(bFinal ? 0x01 : 0x00);
		const std::uint16_t Len = static_cast<std::uint16_t>(BlockSize);
		const std::uint16_t NLen = static_cast<std::uint16_t>(~Len);
		Out.push_back(static_cast<std::uint8_t>(Len & 0xFFu));
		Out.push_back(static_cast<std::uint8_t>((Len >> 8) & 0xFFu));
		Out.push_back(static_cast<std::uint8_t>(NLen & 0xFFu));
		Out.push_back(static_cast<std::uint8_t>((NLen >> 8) & 0xFFu));
		Out.insert(Out.end(), Raw + Offset, Raw + Offset + BlockSize);

		Offset += BlockSize;
	} while (Offset < Size);

	PushBE32(Out, Adler32(Raw, Size));
}

} // namespace

bool WritePngRGBA(const char* Path, std::uint32_t Width, std::uint32_t Height,
	const std::uint8_t* Rgba)
{
	if (Path == nullptr || Rgba == nullptr || Width == 0 || Height == 0)
	{
		MAHO_LOG_CORE_ERROR("ScreenshotWriter: invalid argument (path/pixels/extent)");
		return false;
	}

	// Raw scanline stream: one filter byte (0 = None) in front of every row, RGBA bytes after it.
	const std::size_t Stride = static_cast<std::size_t>(Width) * 4;
	std::vector<std::uint8_t> Raw;
	Raw.reserve((Stride + 1) * Height);
	for (std::uint32_t Y = 0; Y < Height; ++Y)
	{
		Raw.push_back(0);
		const std::uint8_t* Row = Rgba + static_cast<std::size_t>(Y) * Stride;
		Raw.insert(Raw.end(), Row, Row + Stride);
	}

	std::vector<std::uint8_t> Png;
	Png.reserve(Raw.size() + 1024);
	const std::uint8_t Signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	Png.insert(Png.end(), Signature, Signature + 8);

	std::vector<std::uint8_t> IHdr;
	PushBE32(IHdr, Width);
	PushBE32(IHdr, Height);
	IHdr.push_back(8);   // bit depth
	IHdr.push_back(6);   // colour type 6 = truecolour with alpha
	IHdr.push_back(0);   // compression = deflate
	IHdr.push_back(0);   // filter method 0
	IHdr.push_back(0);   // no interlace
	PushChunk(Png, "IHDR", IHdr.data(), IHdr.size());

	std::vector<std::uint8_t> Idat;
	PushZlibStored(Idat, Raw.data(), Raw.size());
	PushChunk(Png, "IDAT", Idat.data(), Idat.size());
	PushChunk(Png, "IEND", nullptr, 0);

	std::FILE* File = std::fopen(Path, "wb");
	if (File == nullptr)
	{
		MAHO_LOG_CORE_ERROR("ScreenshotWriter: cannot open {} for writing", Path);
		return false;
	}
	const std::size_t Written = std::fwrite(Png.data(), 1, Png.size(), File);
	std::fclose(File);
	if (Written != Png.size())
	{
		MAHO_LOG_CORE_ERROR("ScreenshotWriter: short write to {} ({} of {} bytes)", Path, Written,
			Png.size());
		return false;
	}
	return true;
}

} // namespace Detail
} // namespace Render
} // namespace Maho
