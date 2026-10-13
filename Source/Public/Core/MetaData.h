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

/** The TYPE this header's own declarations are filed under: one row per DECLARED TYPE, whose address
 *  is that type's identity token (see `MAHO_DECLARE_TYPE`). */
inline constexpr std::string_view KDeclaredType = "Type";

/** A type's DECLARED NAME -- non-empty only for a type declared with `MAHO_DECLARE_TYPE`. It is what
 *  makes a type addressable in the store without RTTI; an empty one is the trait's way of saying
 *  "this type has no identity here". */
template <typename T>
struct TTypeName
{
	static constexpr std::string_view value{};
};

namespace Detail
{
	/** The registrar `MAHO_DECLARE_TYPE` expands: it files the type's NAME, so the row exists -- and
	 *  its ADDRESS is the token every module resolves to the same value. The value is empty on
	 *  purpose: a declaration says "this identity exists", nothing more. */
	struct MAHO_API FTypeRegistrar
	{
		explicit FTypeRegistrar(std::string_view TypeName) noexcept;
	};
}

/**
 * Declare a TYPE's identity: `MAHO_DECLARE_TYPE(FTransform)` gives it a row in the store, and the
 * ADDRESS of that row is the token `TQueryTag<T>::Get()` answers with -- the same value in every DLL,
 * which is what lets a component pool be found by component type without a `dynamic_cast`.
 *
 * Written INSIDE `namespace Maho`, next to the type it names (the same convention as
 * `MAHO_DECLARE_RESOURCE`). Use the most qualified spelling that is unambiguous: the name IS the key.
 */
#define MAHO_DECLARE_TYPE(X)                                                     \
	template <>                                                                  \
	struct TTypeName<X>                                                          \
	{                                                                            \
		static constexpr std::string_view value = #X;                             \
		static inline const ::Maho::Detail::FTypeRegistrar Registration{ value }; \
	};

[[nodiscard]] MAHO_API std::string_view BareTypeName(std::string_view Qualified) noexcept;

}
