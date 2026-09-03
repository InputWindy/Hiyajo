#pragma once

// Tiny little-endian binary serializer for the value asset types (mesh/material/
// skeleton/animation). These types have no standalone file codec - they are the
// typed containers produced by model / prefab decode - so their importer/exporter
// round-trip this format. Header-only (inlined); folded into the host TU.

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Maho
{
namespace Resource
{
namespace detail
{

class FBinWriter
{
public:
	explicit FBinWriter(std::vector<std::uint8_t>& OutBytes) : Bytes(&OutBytes) {}

	void WriteU32(std::uint32_t V) { WriteRaw(&V, sizeof(V)); }
	void WriteI32(std::int32_t V) { WriteRaw(&V, sizeof(V)); }
	void WriteU64(std::uint64_t V) { WriteRaw(&V, sizeof(V)); }
	void WriteF32(float V) { WriteRaw(&V, sizeof(V)); }
	void WriteBool(bool V) { Bytes->push_back(V ? 1 : 0); }

	void WriteString(std::string_view S)
	{
		WriteU32(static_cast<std::uint32_t>(S.size()));
		Bytes->insert(Bytes->end(), S.begin(), S.end());
	}

private:
	void WriteRaw(const void* Ptr, std::size_t Size)
	{
		const auto* P = static_cast<const std::uint8_t*>(Ptr);
		Bytes->insert(Bytes->end(), P, P + Size);
	}

	std::vector<std::uint8_t>* Bytes;
};

class FBinReader
{
public:
	FBinReader(std::span<const std::uint8_t> InBytes) : Data(InBytes) {}

	bool ReadU32(std::uint32_t& Out) { return ReadRaw(&Out, sizeof(Out)); }
	bool ReadI32(std::int32_t& Out) { return ReadRaw(&Out, sizeof(Out)); }
	bool ReadU64(std::uint64_t& Out) { return ReadRaw(&Out, sizeof(Out)); }
	bool ReadF32(float& Out) { return ReadRaw(&Out, sizeof(Out)); }

	bool ReadBool(bool& Out)
	{
		if (Pos >= Data.size()) { return false; }
		Out = Data[Pos++] != 0;
		return true;
	}

	bool ReadString(std::string& Out)
	{
		std::uint32_t Len = 0;
		if (!ReadU32(Len)) { return false; }
		if (Pos + Len > Data.size()) { return false; }
		Out.assign(reinterpret_cast<const char*>(Data.data() + Pos), Len);
		Pos += Len;
		return true;
	}

private:
	bool ReadRaw(void* Out, std::size_t Size)
	{
		if (Pos + Size > Data.size()) { return false; }
		std::memcpy(Out, Data.data() + Pos, Size);
		Pos += Size;
		return true;
	}

	std::span<const std::uint8_t> Data;
	std::size_t Pos = 0;
};

} // namespace detail
} // namespace Resource
} // namespace Maho
