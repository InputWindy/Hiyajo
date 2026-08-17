#include <Archive.h>

#include <cstring>

namespace Maho::Archive
{

// ── typed serialization (builtins) ──

FArchive& FArchive::operator<<(std::int32_t& V)
{
	Serialize(&V, sizeof(V));
	return *this;
}

FArchive& FArchive::operator<<(std::uint32_t& V)
{
	Serialize(&V, sizeof(V));
	return *this;
}

FArchive& FArchive::operator<<(std::int64_t& V)
{
	Serialize(&V, sizeof(V));
	return *this;
}

FArchive& FArchive::operator<<(std::uint64_t& V)
{
	Serialize(&V, sizeof(V));
	return *this;
}

FArchive& FArchive::operator<<(float& V)
{
	Serialize(&V, sizeof(V));
	return *this;
}

FArchive& FArchive::operator<<(double& V)
{
	Serialize(&V, sizeof(V));
	return *this;
}

FArchive& FArchive::operator<<(bool& V)
{
	Serialize(&V, sizeof(V));
	return *this;
}

FArchive& FArchive::operator<<(std::string& V)
{
	std::uint32_t Len = static_cast<std::uint32_t>(V.size());
	*this << Len;
	if (IsReading())
	{
		V.resize(Len);
	}
	if (Len > 0)
	{
		Serialize(V.data(), Len);
	}
	return *this;
}

// ── FMemoryReader ──

FMemoryReader::FMemoryReader(const std::vector<std::uint8_t>& InData)
	: FArchive(EArchiveMode::Read)
	, Data(InData)
{
}

void FMemoryReader::Serialize(void* Out, std::size_t Size)
{
	if (Pos + Size > Data.size())
	{
		return;   // TODO: report out-of-bounds read
	}
	std::memcpy(Out, Data.data() + Pos, Size);
	Pos += Size;
}

void FMemoryReader::Seek(std::size_t InPos)
{
	Pos = InPos;
}

std::size_t FMemoryReader::Tell() const
{
	return Pos;
}

// ── FMemoryWriter ──

FMemoryWriter::FMemoryWriter()
	: FArchive(EArchiveMode::Write)
{
}

void FMemoryWriter::Serialize(void* In, std::size_t Size)
{
	const std::size_t Old = Buffer.size();
	Buffer.resize(Old + Size);
	std::memcpy(Buffer.data() + Old, In, Size);
}

void FMemoryWriter::Seek(std::size_t InPos)
{
	if (InPos < Buffer.size())
	{
		Buffer.resize(InPos);
	}
}

std::size_t FMemoryWriter::Tell() const
{
	return Buffer.size();
}

const std::vector<std::uint8_t>& FMemoryWriter::GetBytes() const
{
	return Buffer;
}

std::vector<std::uint8_t> FMemoryWriter::TakeBytes()
{
	return std::move(Buffer);
}

// ── FArchiveSystem (extension) ──

bool FArchiveSystem::ExecuteStage(EToolStage Stage)
{
	// Pure serialization library — no state.
	(void)Stage;
	return true;
}

} // namespace Maho::Archive

// ── Dynamic plugin entry (runtime load/unload via FAssemblyImporter) ──

namespace
{

class FArchiveSystemAdapter final : public Maho::IExtension<Maho::EToolStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EToolStage Stage) override
	{
		return Maho::Archive::FArchiveSystem::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_ARCHIVE_API Maho::IExtension<Maho::EToolStage>* CreateExtension()
{
	return new FArchiveSystemAdapter();
}
