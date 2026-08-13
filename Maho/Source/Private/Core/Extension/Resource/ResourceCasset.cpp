#include <Core/Extension/Resource/ResourceCasset.h>

#include <Core/System/Compression.h>
#include <Core/System/Log.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Maho
{
namespace ResourceCasset
{

// ── Binary helpers ─────────────────────────────────────────────

namespace
{

class FByteWriter
{
public:
	explicit FByteWriter(std::vector<std::uint8_t>& Out) : Out_(Out) {}

	void WriteU8(std::uint8_t V) { Out_.push_back(V); }
	void WriteU16(std::uint16_t V)
	{
		Out_.push_back(static_cast<std::uint8_t>(V & 0xFF));
		Out_.push_back(static_cast<std::uint8_t>((V >> 8) & 0xFF));
	}
	void WriteU32(std::uint32_t V)
	{
		for (int I = 0; I < 4; ++I)
			Out_.push_back(static_cast<std::uint8_t>((V >> (I * 8)) & 0xFF));
	}
	void WriteF32(float V)
	{
		std::uint32_t Tmp;
		std::memcpy(&Tmp, &V, sizeof(Tmp));
		WriteU32(Tmp);
	}
	void WriteBytes(const std::uint8_t* Data, std::size_t Count)
	{
		Out_.insert(Out_.end(), Data, Data + Count);
	}
	void WriteString(const std::string& S)
	{
		WriteU32(static_cast<std::uint32_t>(S.size()));
		if (!S.empty())
			WriteBytes(reinterpret_cast<const std::uint8_t*>(S.data()), S.size());
	}
	void PadTo4()
	{
		while (Out_.size() % 4 != 0)
			Out_.push_back(0);
	}
	std::size_t Tell() const { return Out_.size(); }

private:
	std::vector<std::uint8_t>& Out_;
};

class FByteReader
{
public:
	FByteReader(const std::uint8_t* Data, std::size_t Size)
		: Data_(Data), Size_(Size), Pos_(0) {}

	[[nodiscard]] bool CanRead(std::size_t Num) const { return Pos_ + Num <= Size_; }

	std::uint8_t ReadU8()
	{
		if (!CanRead(1)) throw std::runtime_error("ReadU8 underflow");
		return Data_[Pos_++];
	}
	std::uint16_t ReadU16()
	{
		if (!CanRead(2)) throw std::runtime_error("ReadU16 underflow");
		std::uint16_t V = static_cast<std::uint16_t>(Data_[Pos_])
			| (static_cast<std::uint16_t>(Data_[Pos_ + 1]) << 8);
		Pos_ += 2;
		return V;
	}
	std::uint32_t ReadU32()
	{
		if (!CanRead(4)) throw std::runtime_error("ReadU32 underflow");
		std::uint32_t V = static_cast<std::uint32_t>(Data_[Pos_])
			| (static_cast<std::uint32_t>(Data_[Pos_ + 1]) << 8)
			| (static_cast<std::uint32_t>(Data_[Pos_ + 2]) << 16)
			| (static_cast<std::uint32_t>(Data_[Pos_ + 3]) << 24);
		Pos_ += 4;
		return V;
	}
	float ReadF32()
	{
		std::uint32_t Tmp = ReadU32();
		float F;
		std::memcpy(&F, &Tmp, sizeof(F));
		return F;
	}
	std::vector<std::uint8_t> ReadBytes(std::size_t Count)
	{
		if (!CanRead(Count)) throw std::runtime_error("ReadBytes underflow");
		std::vector<std::uint8_t> Out(Data_ + Pos_, Data_ + Pos_ + Count);
		Pos_ += Count;
		return Out;
	}
	std::string ReadString()
	{
		std::uint32_t Len = ReadU32();
		if (Len == 0) return {};
		if (!CanRead(Len)) throw std::runtime_error("ReadString underflow");
		std::string S(reinterpret_cast<const char*>(Data_ + Pos_), Len);
		Pos_ += Len;
		return S;
	}
	void Skip(std::size_t N) { Pos_ = std::min(Pos_ + N, Size_); }
	void SkipPadTo4()
	{
		while (Pos_ % 4 != 0 && Pos_ < Size_) ++Pos_;
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

template <typename T>
void WriteBlob(FByteWriter& W, const T* Data, std::size_t Count)
{
	W.WriteU32(static_cast<std::uint32_t>(Count));
	if (Count > 0 && Data)
		W.WriteBytes(reinterpret_cast<const std::uint8_t*>(Data), Count * sizeof(T));
}

template <typename T>
[[nodiscard]] std::vector<T> ReadBlob(FByteReader& R)
{
	std::uint32_t Count = R.ReadU32();
	if (Count == 0) return {};
	if (!R.CanRead(Count * sizeof(T))) throw std::runtime_error("ReadBlob underflow");
	std::vector<T> Out(Count);
	std::memcpy(Out.data(), &R, Count * sizeof(T)); // This is wrong in the original code
	throw std::runtime_error("ReadBlob: use manual read");
	return Out;
}

} // namespace

// ── Object record ──────────────────────────────────────────────

static void WriteObjectRecord(const FResource& Object, std::vector<std::uint8_t>& Out)
{
	FByteWriter W(Out);
	W.WriteString(Object.GetName());
	W.WriteU16(static_cast<std::uint16_t>(Object.GetType()));
	W.WriteString(Object.GetSourcePath());

	std::uint32_t RecordFlags = kRecordHasCpu;

	std::vector<std::string> Refs = Object.GetReferencePaths();
	if (!Refs.empty())
		RecordFlags |= kRecordHasRefs;

	W.WriteU32(RecordFlags);
	W.WriteU32(0); // Reserved0
	W.WriteU32(0); // Reserved1

	if (!Refs.empty())
	{
		W.WriteU32(static_cast<std::uint32_t>(Refs.size()));
		for (const std::string& Ref : Refs)
			W.WriteString(Ref);
	}

	std::vector<std::uint8_t> CpuBytes;
	if (WriteCpuPayloadBytes(Object, CpuBytes))
	{
		W.WriteU32(static_cast<std::uint32_t>(CpuBytes.size()));
		if (!CpuBytes.empty())
			W.WriteBytes(CpuBytes.data(), CpuBytes.size());
	}
	else
	{
		W.WriteU32(0);
	}
}

static void ParseObjectRecord(FByteReader& R, FCassetParsedObject& Out)
{
	Out.Name = R.ReadString();
	Out.Type = static_cast<EAssetType>(R.ReadU16());
	Out.ImportSource = R.ReadString();
	Out.RecordFlags = R.ReadU32();
	R.Skip(8); // Reserved0 + Reserved1

	if ((Out.RecordFlags & kRecordHasRefs) != 0)
	{
		std::uint32_t RefCount = R.ReadU32();
		Out.Refs.reserve(RefCount);
		for (std::uint32_t I = 0; I < RefCount; ++I)
			Out.Refs.push_back(R.ReadString());
	}

	if ((Out.RecordFlags & kRecordHasCpu) != 0)
	{
		std::uint32_t CpuSize = R.ReadU32();
		if (CpuSize > 0)
			Out.CpuBytes = R.ReadBytes(CpuSize);
	}

	if ((Out.RecordFlags & kRecordHasExtras) != 0)
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

static void BuildUncompressedDocument(
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
		for (int I = 0; I < 5; ++I) W.WriteU32(0);
		AppendChunk(OutDoc, kChunkTagPKG1, kChunkMustUnderstand, Chunk);
	}

	// DEPS chunk — collect cross-package references
	{
		std::vector<std::uint8_t> Chunk;
		FByteWriter W(Chunk);

		std::vector<std::pair<std::string, std::string>> Deps;
		for (const FResource* Obj : Objects)
		{
			if (!Obj) continue;
			for (const std::string& RefPath : Obj->GetReferencePaths())
			{
				std::size_t Dot = RefPath.find('.');
				if (Dot == std::string::npos) continue;
				std::string DepPkg = RefPath.substr(0, Dot);
				if (DepPkg != Package.Name)
					Deps.push_back({DepPkg, RefPath});
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
			if (!Obj) continue;
			std::vector<std::uint8_t> Record;
			WriteObjectRecord(*Obj, Record);
			W.WriteBytes(Record.data(), Record.size());
		}
		W.PadTo4();
		AppendChunk(OutDoc, kChunkTagOBJS, kChunkMustUnderstand, Chunk);
	}
}

static void ParseUncompressedDocument(
	const std::vector<std::uint8_t>& Doc,
	FCassetParsedPackage& Out)
{
	FByteReader R(Doc.data(), Doc.size());
	while (R.CanRead(4))
	{
		std::uint32_t Tag = R.ReadU32();
		std::uint32_t PayloadSize = R.ReadU32();
		std::uint32_t Flags = R.ReadU32();
		(void)Flags;

		if (!R.CanRead(PayloadSize)) break;

		std::vector<std::uint8_t> Payload = R.ReadBytes(PayloadSize);
		FByteReader PR(Payload.data(), Payload.size());

		if (Tag == kChunkTagPKG1)
		{
			Out.Name = PR.ReadString();
			Out.PackageFlags = PR.ReadU32();
			Out.DefaultObjectName = PR.ReadString();
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
				Out.Dependencies[I].Reserved = PR.ReadU32();
			}
		}
		else if (Tag == kChunkTagOBJS)
		{
			std::uint32_t ObjCount = PR.ReadU32();
			Out.Objects.reserve(ObjCount);
			for (std::uint32_t I = 0; I < ObjCount; ++I)
			{
				FCassetParsedObject Obj;
				ParseObjectRecord(PR, Obj);
				Out.Objects.push_back(std::move(Obj));
			}
		}

		R.SkipPadTo4();
	}
}

// ── Public API ─────────────────────────────────────────────────

bool IsCassetBinaryFile(const std::uint8_t* Data, std::size_t Size)
{
	if (Size < 8) return false;
	std::uint32_t Magic = static_cast<std::uint32_t>(Data[0])
		| (static_cast<std::uint32_t>(Data[1]) << 8)
		| (static_cast<std::uint32_t>(Data[2]) << 16)
		| (static_cast<std::uint32_t>(Data[3]) << 24);
	return Magic == kCassetMagic;
}

bool WriteCpuPayloadBytes(const FResource& Resource, std::vector<std::uint8_t>& OutCpuBytes)
{
	FArchive Ar(EArchiveMode::Saving, OutCpuBytes);
	const_cast<FResource&>(Resource).Serialize(Ar);
	return !Ar.IsError();
}

bool ApplyCpuPayload(FResource& Resource, const std::vector<std::uint8_t>& CpuBytes)
{
	FArchive Ar(EArchiveMode::Loading, CpuBytes.data(), CpuBytes.size());
	Resource.Serialize(Ar);
	if (Ar.IsError()) return false;

	Resource.MarkCpuReady();
	return true;
}

bool EncodePackageFile(
	const FResourcePackage& Package,
	const std::vector<FResource*>& Objects,
	std::vector<std::uint8_t>& OutFileBytes)
{
	std::vector<std::uint8_t> Document;
	if (!BuildPackageDocument(Package, Objects, Document))
		return false;
	return WrapDocumentToMcasFile(Document, OutFileBytes);
}

bool BuildPackageDocument(
	const FResourcePackage& Package,
	const std::vector<FResource*>& Objects,
	std::vector<std::uint8_t>& OutDocument)
{
	OutDocument.clear();
	BuildUncompressedDocument(Package, Objects, OutDocument);
	return true;
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

	for (int I = 0; I < 8; ++I) W.WriteU32(0); // Reserved

	W.WriteBytes(Compressed.data(), Compressed.size());
	return true;
}

bool DecodePackageFile(
	const std::uint8_t* FileBytes,
	std::size_t FileSize,
	FCassetParsedPackage& OutPackage)
{
	if (FileSize < kCassetHeaderSize) return false;

	FByteReader R(FileBytes, FileSize);
	std::uint32_t Magic = R.ReadU32();
	if (Magic != kCassetMagic) return false;

	std::uint32_t FormatVersion = R.ReadU32();
	std::uint32_t HeaderSize = R.ReadU32();
	std::uint32_t Flags = R.ReadU32();

	if (FormatVersion > kCassetFormatVersion) return false;

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

	ParseUncompressedDocument(Uncompressed, OutPackage);
	return true;
}

} // namespace ResourceCasset
} // namespace Maho
