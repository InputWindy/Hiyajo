#pragma once


#include <Core/Export.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Maho
{

struct FMetaDataEntry
{
	std::string Type;
	std::string Key;
	std::string Value;
};

class MAHO_API FMetaData
{
public:
	FMetaData();
	~FMetaData();

	FMetaData(const FMetaData&) = delete;
	FMetaData& operator=(const FMetaData&) = delete;

	void Register(std::string_view Type, std::string_view Key, std::string_view Encoded);

	[[nodiscard]] const FMetaDataEntry* Find(std::string_view Type, std::string_view Key) const;

	[[nodiscard]] std::size_t Count(std::string_view Type) const;

	[[nodiscard]] std::vector<std::string> Types() const;

	[[nodiscard]] std::vector<FMetaDataEntry> Snapshot(std::string_view Type) const;

	void DumpOnce() const;

private:
	struct FImpl;
	std::unique_ptr<FImpl> Impl;
};

[[nodiscard]] MAHO_API FMetaData& GetMetaData() noexcept;

[[nodiscard]] MAHO_API std::string_view BareTypeName(std::string_view Qualified) noexcept;

}
