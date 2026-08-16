#include "Resource/ResourceCasset.h"

#include <Core/Misc/Compression.h>
#include <Core/Misc/Log.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace Maho
{

namespace
{

// ── MCAS binary constants (private to this codec) ──────────────

inline constexpr std::uint32_t kCassetMagic =
	static_cast<std::uint32_t>('M')
	| (static_cast<std::uint32_t>('C') << 8)
	| (static_cast<std::uint32_t>('A') << 16)
	| (static_cast<std::uint32_t>('S') << 24);

inline constexpr std::uint32_t kCassetFormatVersion = 1;
inline constexpr std::uint32_t kCassetContentVersion = 1;
inline constexpr std::uint32_t kCassetHeaderSize = 64;
inline constexpr std::uint32_t kCassetFlagBodyZlib = 1u << 0;

inline constexpr std::uint32_t kChunkTagPKG1 =
	static_cast<std::uint32_t>('P')
	| (static_cast<std::uint32_t>('K') << 8)
	| (static_cast<std::uint32_t>('G') << 16)
	| (static_cast<std::uint32_t>('1') << 24);
inline constexpr std::uint32_t kChunkTagDEPS =
	static_cast<std::uint32_t>('D')
	| (static_cast<std::uint32_t>('E') << 8)
	| (static_cast<std::uint32_t>('P') << 16)
	| (static_cast<std::uint32_t>('S') << 24);
inline constexpr std::uint32_t kChunkTagOBJS =
	static_cast<std::uint32_t>('O')
	| (static_cast<std::uint32_t>('B') << 8)
	| (static_cast<std::uint32_t>('J') << 16)
	| (static_cast<std::uint32_t>('S') << 24);

inline constexpr std::uint32_t kChunkMustUnderstand = 1u << 0;

inline constexpr std::uint32_t kRecordHasCpu = 1u << 0;
inline constexpr std::uint32_t kRecordHasRefs = 1u << 1;
inline constexpr std::uint32_t kRecordHasExtras = 1u << 2;

// ── Binary helpers ─────────────────────────────────────────────

class FByteWriter
{
public:
	explicit FByteWriter(std::vector<std::uint8_t>& Out)
		: Out_(Out)
	{
	}

	void WriteU8(std::uint8_t V)
	{
		Out_.push_back(V);
	}
	void WriteU16(std::uint16_t V)
	{
		Out_.push_back(static_cast<std::uint8_t>(V & 0xFF));
		Out_.push_back(static_cast<std::uint8_t>((V >> 8) & 0xFF));
	}
	void WriteU32(std::uint32_t V)
	{
		for (int I = 0; I < 4; ++I)
		{
			Out_.push_back(static_cast<std::uint8_t>((V >> (I * 8)) & 0xFF));
		}
	}
	void WriteBytes(const std::uint8_t* Data, std::size_t Count)
	{
		Out_.insert(Out_.end(), Data, Data + Count);
	}
	void WriteString(const std::string& S)
	{
		WriteU32(static_cast<std::uint32_t>(S.size()));
		if (!S.empty())
		{
			WriteBytes(reinterpret_cast<const std::uint8_t*>(S.data()), S.size());
		}
	}
	void PadTo4()
	{
		while (Out_.size() % 4 != 0)
		{
			Out_.push_back(0);
		}
	}
	std::size_t Tell() const
	{
		return Out_.size();
	}

private:
	std::vector<std::uint8_t>& Out_;
};

class FByteReader
{
public:
	FByteReader(const std::uint8_t* Data, std::size_t Size)
		: Data_(Data), Size_(Size), Pos_(0)
	{
	}

	[[nodiscard]] bool CanRead(std::size_t Num) const
	{
		return Pos_ + Num <= Size_;
	}

	std::uint8_t ReadU8()
	{
		if (!CanRead(1))
		{
			throw std::runtime_error("ReadU8 underflow");
		}
		return Data_[Pos_++];
	}
	std::uint16_t ReadU16()
	{
		if (!CanRead(2))
		{
			throw std::runtime_error("ReadU16 underflow");
		}
		std::uint16_t V = static_cast<std::uint16_t>(Data_[Pos_])
			| (static_cast<std::uint16_t>(Data_[Pos_ + 1]) << 8);
		Pos_ += 2;
		return V;
	}
	std::uint32_t ReadU32()
	{
		if (!CanRead(4))
		{
			throw std::runtime_error("ReadU32 underflow");
		}
		std::uint32_t V = static_cast<std::uint32_t>(Data_[Pos_])
			| (static_cast<std::uint32_t>(Data_[Pos_ + 1]) << 8)
			| (static_cast<std::uint32_t>(Data_[Pos_ + 2]) << 16)
			| (static_cast<std::uint32_t>(Data_[Pos_ + 3]) << 24);
		Pos_ += 4;
		return V;
	}
	std::vector<std::uint8_t> ReadBytes(std::size_t Count)
	{
		if (!CanRead(Count))
		{
			throw std::runtime_error("ReadBytes underflow");
		}
		std::vector<std::uint8_t> Out(Data_ + Pos_, Data_ + Pos_ + Count);
		Pos_ += Count;
		return Out;
	}
	std::string ReadString()
	{
		std::uint32_t Len = ReadU32();
		if (Len == 0)
		{
			return {};
		}
		if (!CanRead(Len))
		{
			throw std::runtime_error("ReadString underflow");
		}
		std::string S(reinterpret_cast<const char*>(Data_ + Pos_), Len);
		Pos_ += Len;
		return S;
	}
	void Skip(std::size_t N)
	{
		Pos_ = std::min(Pos_ + N, Size_);
	}
	void SkipPadTo4()
	{
		while (Pos_ % 4 != 0 && Pos_ < Size_)
		{
			++Pos_;
		}
	}

private:
	const std::uint8_t* Data_;
	std::size_t Size_;
	std::size_t Pos_;
};

void AppendChunk(std::vector<std::uint8_t>& Doc, std::uint32_t Tag, std::uint32_t Flags, const std::vector<std::uint8_t>& Payload)
{
	FByteWriter W(Doc);
	W.WriteU32(Tag);
	W.WriteU32(static_cast<std::uint32_t>(Payload.size()));
	W.WriteU32(Flags);
	W.WriteBytes(Payload.data(), Payload.size());
	W.PadTo4();
}

bool WriteCpuPayloadBytes(const FResource& Resource, std::vector<std::uint8_t>& OutCpuBytes)
{
	FArchive Ar(EArchiveMode::Saving, OutCpuBytes);
	const_cast<FResource&>(Resource).Serialize(Ar);
	return !Ar.IsError();
}

// ── Object record ──────────────────────────────────────────────

void WriteObjectRecord(const FResource& Object, std::vector<std::uint8_t>& Out)
{
	FByteWriter W(Out);
	W.WriteString(Object.GetName());
	W.WriteU16(static_cast<std::uint16_t>(Object.GetType()));
	W.WriteString(Object.GetSourcePath());

	std::uint32_t RecordFlags = kRecordHasCpu;

	std::vector<std::string> Refs = Object.GetReferencePaths();
	if (!Refs.empty())
	{
		RecordFlags |= kRecordHasRefs;
	}

	W.WriteU32(RecordFlags);
	W.WriteU32(0); // Reserved0
	W.WriteU32(0); // Reserved1

	if (!Refs.empty())
	{
		W.WriteU32(static_cast<std::uint32_t>(Refs.size()));
		for (const std::string& Ref : Refs)
		{
			W.WriteString(Ref);
		}
	}

	std::vector<std::uint8_t> CpuBytes;
	if (WriteCpuPayloadBytes(Object, CpuBytes))
	{
		W.WriteU32(static_cast<std::uint32_t>(CpuBytes.size()));
		if (!CpuBytes.empty())
		{
			W.WriteBytes(CpuBytes.data(), CpuBytes.size());
		}
	}
	else
	{
		W.WriteU32(0);
	}
}

void ParseObjectRecord(FByteReader& R, FPackageDocumentObject& Out)
{
	Out.Name = R.ReadString();
	Out.Type = static_cast<EAssetType>(R.ReadU16());
	Out.ImportSource = R.ReadString();
	const std::uint32_t RecordFlags = R.ReadU32();
	R.Skip(8); // Reserved0 + Reserved1

	if ((RecordFlags & kRecordHasRefs) != 0)
	{
		std::uint32_t RefCount = R.ReadU32();
		Out.Refs.reserve(RefCount);
		for (std::uint32_t I = 0; I < RefCount; ++I)
		{
			Out.Refs.push_back(R.ReadString());
		}
	}

	if ((RecordFlags & kRecordHasCpu) != 0)
	{
		std::uint32_t CpuSize = R.ReadU32();
		if (CpuSize > 0)
		{
			Out.CpuBytes = R.ReadBytes(CpuSize);
		}
	}

	if ((RecordFlags & kRecordHasExtras) != 0)
	{
		std::uint32_t ExtraCount = R.ReadU32();
		for (std::uint32_t I = 0; I < ExtraCount; ++I)
		{
			R.ReadU32(); // Key
			std::uint32_t Size = R.ReadU32();
			R.Skip(Size);
		}
	}
}

// ── Document build/parse ───────────────────────────────────────

void BuildUncompressedDocument(
	const FResourcePackage& Package,
	const std::vector<FResource*>& Objects,
	std::vector<std::uint8_t>& OutDoc)
{
	// PKG1 chunk
	{
		std::vector<std::uint8_t> Chunk;
		FByteWriter W(Chunk);
		W.WriteString(Package.Name);
		W.WriteU32(Package.Flags);
		W.WriteString("");
		W.WriteU32(static_cast<std::uint32_t>(Objects.size()));
		for (int I = 0; I < 5; ++I)
		{
			W.WriteU32(0);
		}
		AppendChunk(OutDoc, kChunkTagPKG1, kChunkMustUnderstand, Chunk);
	}

	// DEPS chunk — collect cross-package references
	{
		std::vector<std::uint8_t> Chunk;
		FByteWriter W(Chunk);

		std::vector<std::pair<std::string, std::string>> Deps;
		for (const FResource* Obj : Objects)
		{
			if (!Obj)
			{
				continue;
			}
			for (const std::string& RefPath : Obj->GetReferencePaths())
			{
				std::size_t Dot = RefPath.find('.');
				if (Dot == std::string::npos)
				{
					continue;
				}
				std::string DepPkg = RefPath.substr(0, Dot);
				if (DepPkg != Package.Name)
				{
					Deps.push_back({DepPkg, RefPath});
				}
			}
		}

		W.WriteU32(static_cast<std::uint32_t>(Deps.size()));
		for (const auto& Dep : Deps)
		{
			W.WriteString(Dep.first);
			W.WriteString(Dep.second);
			W.WriteU32(0);
		}
		AppendChunk(OutDoc, kChunkTagDEPS, kChunkMustUnderstand, Chunk);
	}

	// OBJS chunk
	{
		std::vector<std::uint8_t> Chunk;
		FByteWriter W(Chunk);
		W.WriteU32(static_cast<std::uint32_t>(Objects.size()));
		for (const FResource* Obj : Objects)
		{
			if (!Obj)
			{
				continue;
			}
			std::vector<std::uint8_t> Record;
			WriteObjectRecord(*Obj, Record);
			W.WriteBytes(Record.data(), Record.size());
		}
		W.PadTo4();
		AppendChunk(OutDoc, kChunkTagOBJS, kChunkMustUnderstand, Chunk);
	}
}

void ParseUncompressedDocument(
	const std::vector<std::uint8_t>& Doc,
	FPackageDocument& Out)
{
	FByteReader R(Doc.data(), Doc.size());
	while (R.CanRead(4))
	{
		std::uint32_t Tag = R.ReadU32();
		std::uint32_t PayloadSize = R.ReadU32();
		std::uint32_t Flags = R.ReadU32();
		(void)Flags;

		if (!R.CanRead(PayloadSize))
		{
			break;
		}

		std::vector<std::uint8_t> Payload = R.ReadBytes(PayloadSize);
		FByteReader PR(Payload.data(), Payload.size());

		if (Tag == kChunkTagPKG1)
		{
			Out.Name = PR.ReadString();
			Out.Flags = PR.ReadU32();
			PR.ReadString(); // DefaultObjectName (legacy)
			PR.ReadU32(); // ObjectCountHint
			PR.Skip(20); // 5 x Reserved
		}
		else if (Tag == kChunkTagDEPS)
		{
			std::uint32_t DepCount = PR.ReadU32();
			Out.Dependencies.resize(DepCount);
			for (std::uint32_t I = 0; I < DepCount; ++I)
			{
				Out.Dependencies[I].PackageName = PR.ReadString();
				Out.Dependencies[I].FilePath = PR.ReadString();
				PR.ReadU32(); // Reserved
			}
		}
		else if (Tag == kChunkTagOBJS)
		{
			std::uint32_t ObjCount = PR.ReadU32();
			Out.Objects.reserve(ObjCount);
			for (std::uint32_t I = 0; I < ObjCount; ++I)
			{
				FPackageDocumentObject Obj;
				ParseObjectRecord(PR, Obj);
				Out.Objects.push_back(std::move(Obj));
			}
		}

		R.SkipPadTo4();
	}
}

// ── Internal codec entry points ────────────────────────────────

bool IsCassetBinaryFile(const std::uint8_t* Data, std::size_t Size)
{
	if (Size < 8)
	{
		return false;
	}
	std::uint32_t Magic = static_cast<std::uint32_t>(Data[0])
		| (static_cast<std::uint32_t>(Data[1]) << 8)
		| (static_cast<std::uint32_t>(Data[2]) << 16)
		| (static_cast<std::uint32_t>(Data[3]) << 24);
	return Magic == kCassetMagic;
}

bool WrapDocumentToMcasFile(
	const std::vector<std::uint8_t>& UncompressedDocument,
	std::vector<std::uint8_t>& OutFileBytes)
{
	std::vector<std::uint8_t> Compressed;
	if (!FCompression::CompressZlib(
		UncompressedDocument.data(),
		UncompressedDocument.size(),
		Compressed))
	{
		MAHO_CORE_ERROR("ResourceCasset: zlib compress failed");
		return false;
	}

	OutFileBytes.clear();
	FByteWriter W(OutFileBytes);
	W.WriteU32(kCassetMagic);
	W.WriteU32(kCassetFormatVersion);
	W.WriteU32(kCassetHeaderSize);
	W.WriteU32(kCassetFlagBodyZlib);
	W.WriteU32(kCassetContentVersion);
	W.WriteU32(static_cast<std::uint32_t>(UncompressedDocument.size()));
	W.WriteU32(static_cast<std::uint32_t>(Compressed.size()));
	W.WriteU32(0); // Checksum

	// Reserved
	for (int I = 0; I < 8; ++I)
	{
		W.WriteU32(0);
	}

	W.WriteBytes(Compressed.data(), Compressed.size());
	return true;
}

bool EncodePackageFile(
	const FResourcePackage& Package,
	const std::vector<FResource*>& Objects,
	std::vector<std::uint8_t>& OutFileBytes)
{
	std::vector<std::uint8_t> Document;
	BuildUncompressedDocument(Package, Objects, Document);
	return WrapDocumentToMcasFile(Document, OutFileBytes);
}

bool DecodePackageFile(
	const std::uint8_t* FileBytes,
	std::size_t FileSize,
	FPackageDocument& OutPackage)
{
	if (FileSize < kCassetHeaderSize)
	{
		return false;
	}

	FByteReader R(FileBytes, FileSize);
	std::uint32_t Magic = R.ReadU32();
	if (Magic != kCassetMagic)
	{
		return false;
	}

	std::uint32_t FormatVersion = R.ReadU32();
	std::uint32_t HeaderSize = R.ReadU32();
	(void)HeaderSize;
	std::uint32_t Flags = R.ReadU32();

	if (FormatVersion > kCassetFormatVersion)
	{
		return false;
	}

	R.Skip(4); // ContentVersion
	std::uint32_t UncompressedSize = R.ReadU32();
	std::uint32_t CompressedSize = R.ReadU32();
	R.Skip(36); // Checksum(4) + Reserved(32)

	std::vector<std::uint8_t> Compressed = R.ReadBytes(CompressedSize);

	std::vector<std::uint8_t> Uncompressed;
	if ((Flags & kCassetFlagBodyZlib) != 0)
	{
		if (!FCompression::DecompressZlib(
			Compressed.data(),
			Compressed.size(),
			UncompressedSize,
			Uncompressed))
		{
			MAHO_CORE_ERROR("ResourceCasset: zlib decompress failed");
			return false;
		}
	}
	else
	{
		Uncompressed = std::move(Compressed);
	}

	try
	{
		ParseUncompressedDocument(Uncompressed, OutPackage);
	}
	catch (const std::exception& Ex)
	{
		MAHO_CORE_ERROR("ResourceCasset: document parse failed: {}", Ex.what());
		return false;
	}
	return true;
}

} // namespace

void RegisterCassetPackageCodec(FResourceSystem& System)
{
	FPackageCodec Codec;
	Codec.Encode = [](const FResourcePackage& Package, const std::vector<FResource*>& Objects, std::vector<std::uint8_t>& OutFileBytes)
	{
		return EncodePackageFile(Package, Objects, OutFileBytes);
	};
	Codec.Decode = [](const std::uint8_t* FileBytes, std::size_t FileSize, FPackageDocument& OutDocument)
	{
		return DecodePackageFile(FileBytes, FileSize, OutDocument);
	};
	Codec.IsBinary = [](const std::uint8_t* FileBytes, std::size_t FileSize)
	{
		return IsCassetBinaryFile(FileBytes, FileSize);
	};
	System.RegisterPackageCodec(std::move(Codec));
}

} // namespace Maho
