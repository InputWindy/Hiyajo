#pragma once

/**
 * FArchive: bidirectional binary stream for serialization.
 * Each FResource subclass implements Serialize(FArchive&) — same code saves and loads.
 * Extension points (versioning, cooking, net, ref resolution) are placeholders for now.
 */

#include <Core/Export.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Maho
{

/**
 * Direction: same Serialize(FArchive&) body reads or writes depending on mode.
 */
enum class EArchiveMode : std::uint8_t
{
	Saving = 0,
	Loading = 1,
};

/**
 * Extensible serialization context.
 * Current scope: binary byte stream (Saving → OutData, Loading → InData).
 * Future: ArVer / IsCooking / IsNet / object ref resolution.
 */
class MAHO_API FArchive
{
public:
	/** As saving: builds OutData. */
	explicit FArchive(EArchiveMode Mode, std::vector<std::uint8_t>& OutData);
	/** As loading: reads from Data[0..Size). Caller owns the buffer. */
	FArchive(EArchiveMode Mode, const std::uint8_t* Data, std::size_t Size);

	[[nodiscard]] bool IsSaving() const { return Mode == EArchiveMode::Saving; }
	[[nodiscard]] bool IsLoading() const { return Mode == EArchiveMode::Loading; }

	// ── Raw bytes ──

	void SerializeBytes(void* Ptr, std::size_t Count);
	void SerializeRaw(std::uint8_t& V);
	void SerializeRaw(std::uint16_t& V);
	void SerializeRaw(std::uint32_t& V);
	void SerializeRaw(std::uint64_t& V);
	void SerializeRaw(std::int32_t& V);
	void SerializeRaw(float& V);
	void SerializeRaw(double& V);
	void SerializeRaw(bool& V);

	void SerializeString(std::string& S);
	void SerializeBytes(std::vector<std::uint8_t>& B);

	// ── Convenience operator ──

	FArchive& operator<<(std::uint8_t& V)  { SerializeRaw(V); return *this; }
	FArchive& operator<<(std::uint16_t& V) { SerializeRaw(V); return *this; }
	FArchive& operator<<(std::uint32_t& V) { SerializeRaw(V); return *this; }
	FArchive& operator<<(std::uint64_t& V) { SerializeRaw(V); return *this; }
	FArchive& operator<<(std::int32_t& V)  { SerializeRaw(V); return *this; }
	FArchive& operator<<(float& V)         { SerializeRaw(V); return *this; }
	FArchive& operator<<(double& V)        { SerializeRaw(V); return *this; }
	FArchive& operator<<(bool& V)          { SerializeRaw(V); return *this; }

	// ── Query ──

	[[nodiscard]] std::size_t Tell() const;
	[[nodiscard]] bool IsError() const { return bError; }

	// ── Extension points (reserved, no-op for now) ──

	[[nodiscard]] std::uint32_t GetVersion() const { return ArchiveVersion; }
	void SetVersion(std::uint32_t V) { ArchiveVersion = V; }
	[[nodiscard]] bool IsCooking() const { return false; }
	[[nodiscard]] bool IsNet() const { return false; }

private:
	EArchiveMode Mode;
	bool bError = false;

	// Saving
	std::vector<std::uint8_t>* OutData = nullptr;
	std::size_t OutPos = 0;

	// Loading
	const std::uint8_t* InData = nullptr;
	std::size_t InSize = 0;
	std::size_t InPos = 0;

	// Reserved
	std::uint32_t ArchiveVersion = 0;
};

} // namespace Maho
