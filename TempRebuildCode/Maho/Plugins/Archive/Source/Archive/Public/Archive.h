#pragma once

#include "ArchiveApi.h"
#include <Engine.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace Maho
{

namespace Archive
{

/** Serialization mode. */
enum class EArchiveMode : std::uint8_t
{
	Read = 0,
	Write = 1,
};

/**
 * Binary serialization stream — the bridge between raw bytes and typed data.
 *
 *   // Writing
 *   FMemoryWriter Writer;
 *   int X = 42; Writer << X;
 *   auto Bytes = Writer.TakeBytes();
 *
 *   // Reading
 *   FMemoryReader Reader(Bytes);
 *   int Y = 0; Reader << Y;   // Y == 42
 */
class MAHO_ARCHIVE_API FArchive
{
public:
	virtual ~FArchive() = default;

	[[nodiscard]] bool IsReading() const { return Mode == EArchiveMode::Read; }
	[[nodiscard]] bool IsWriting() const { return Mode == EArchiveMode::Write; }

	/** Raw bytes (memcpy semantics). */
	virtual void Serialize(void* Data, std::size_t Size) = 0;
	virtual void Seek(std::size_t Pos) = 0;
	[[nodiscard]] virtual std::size_t Tell() const = 0;

	// ── typed serialization (builtins) ──

	FArchive& operator<<(std::int32_t& V);
	FArchive& operator<<(std::uint32_t& V);
	FArchive& operator<<(std::int64_t& V);
	FArchive& operator<<(std::uint64_t& V);
	FArchive& operator<<(float& V);
	FArchive& operator<<(double& V);
	FArchive& operator<<(bool& V);
	FArchive& operator<<(std::string& V);

	/** Generic POD serialization (trivially copyable types, e.g. glm::vec3). */
	template <typename T>
	FArchive& operator<<(T& Value)
	{
		static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
		Serialize(&Value, sizeof(T));
		return *this;
	}

protected:
	explicit FArchive(EArchiveMode InMode) : Mode(InMode) {}

private:
	EArchiveMode Mode;
};

/**
 * Interface for types that serialize themselves through an FArchive.
 *
 *   struct FMaterial : public Maho::Archive::ISerialize
 *   {
 *       void Serialize(Maho::Archive::FArchive& Ar) override
 *       {
 *           Ar << BaseColor << Roughness;
 *       }
 *   };
 */
class MAHO_ARCHIVE_API ISerialize
{
public:
	virtual ~ISerialize() = default;

	/** Serialize all fields through the archive (read or write). */
	virtual void Serialize(FArchive& Ar) = 0;
};

/** Reader over an existing byte buffer (the buffer must outlive the reader). */
class MAHO_ARCHIVE_API FMemoryReader final : public FArchive
{
public:
	explicit FMemoryReader(const std::vector<std::uint8_t>& InData);

	void Serialize(void* Data, std::size_t Size) override;
	void Seek(std::size_t Pos) override;
	[[nodiscard]] std::size_t Tell() const override;

private:
	const std::vector<std::uint8_t>& Data;
	std::size_t Pos = 0;
};

/** Writer that accumulates into an owned byte buffer. */
class MAHO_ARCHIVE_API FMemoryWriter final : public FArchive
{
public:
	FMemoryWriter();

	void Serialize(void* Data, std::size_t Size) override;
	void Seek(std::size_t Pos) override;
	[[nodiscard]] std::size_t Tell() const override;

	[[nodiscard]] const std::vector<std::uint8_t>& GetBytes() const;
	[[nodiscard]] std::vector<std::uint8_t> TakeBytes();

private:
	std::vector<std::uint8_t> Buffer;
};

/** Serialization extension (pre-app toolkit, no state). */
class MAHO_ARCHIVE_API FArchiveSystem final : public TExtension<EToolStage, FArchiveSystem>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

private:
	friend TSingleton<FArchiveSystem>;
	FArchiveSystem() = default;
};

} // namespace Archive

} // namespace Maho
