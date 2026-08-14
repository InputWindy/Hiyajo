#include "Core/Misc/Archive.h"

#include <stdexcept>

namespace Maho
{

FArchive::FArchive(EArchiveMode InMode, std::vector<std::uint8_t>& InOutData)
	: Mode(InMode)
	, OutData(&InOutData)
	, OutPos(0)
	, InData(nullptr)
	, InSize(0)
	, InPos(0)
{
	if (IsSaving())
	{
		OutPos = OutData->size();
	}
}

FArchive::FArchive(EArchiveMode InMode, const std::uint8_t* Data, std::size_t Size)
	: Mode(InMode)
	, OutData(nullptr)
	, OutPos(0)
	, InData(Data)
	, InSize(Size)
	, InPos(0)
{
}

void FArchive::SerializeBytes(void* Ptr, std::size_t Count)
{
	if (bError || Count == 0) return;

	if (IsSaving())
	{
		const auto* Src = static_cast<const std::uint8_t*>(Ptr);
		OutData->resize(OutPos + Count);
		std::memcpy(OutData->data() + OutPos, Src, Count);
		OutPos += Count;
	}
	else
	{
		if (InPos + Count > InSize)
		{
			bError = true;
			return;
		}
		std::memcpy(Ptr, InData + InPos, Count);
		InPos += Count;
	}
}

void FArchive::SerializeRaw(std::int8_t& V)
{
	SerializeBytes(&V, sizeof(V));
}

void FArchive::SerializeRaw(std::int16_t& V)
{
	SerializeBytes(&V, sizeof(V));
}

void FArchive::SerializeRaw(std::int32_t& V)
{
	SerializeBytes(&V, sizeof(V));
}

void FArchive::SerializeRaw(std::int64_t& V)
{
	SerializeBytes(&V, sizeof(V));
}

void FArchive::SerializeRaw(std::uint8_t& V)
{
	SerializeBytes(&V, sizeof(V));
}

void FArchive::SerializeRaw(std::uint16_t& V)
{
	SerializeBytes(&V, sizeof(V));
}

void FArchive::SerializeRaw(std::uint32_t& V)
{
	SerializeBytes(&V, sizeof(V));
}

void FArchive::SerializeRaw(std::uint64_t& V)
{
	SerializeBytes(&V, sizeof(V));
}

void FArchive::SerializeRaw(char& V)
{
	SerializeBytes(&V, sizeof(V));
}

void FArchive::SerializeRaw(float& V)
{
	SerializeBytes(&V, sizeof(V));
}

void FArchive::SerializeRaw(double& V)
{
	SerializeBytes(&V, sizeof(V));
}

void FArchive::SerializeRaw(bool& V)
{
	std::uint8_t B = V ? 1 : 0;
	SerializeRaw(B);
	if (IsLoading())
	{
		V = (B != 0);
	}
}

void FArchive::SerializeString(std::string& S)
{
	if (IsSaving())
	{
		std::uint32_t Len = static_cast<std::uint32_t>(S.size());
		SerializeRaw(Len);
		SerializeBytes(const_cast<char*>(S.data()), Len);
	}
	else
	{
		std::uint32_t Len = 0;
		SerializeRaw(Len);
		S.resize(Len);
		SerializeBytes(S.data(), Len);
	}
}

std::size_t FArchive::Tell() const
{
	return IsSaving() ? OutPos : InPos;
}

} // namespace Maho
