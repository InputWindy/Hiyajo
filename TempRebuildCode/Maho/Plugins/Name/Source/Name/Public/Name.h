#pragma once

#include "NameApi.h"
#include <Engine.h>

#include <cstdint>
#include <functional>
#include <string_view>

namespace Maho
{

namespace Name
{

/**
 * Interned immutable string identifier — pooled storage, O(1) compare.
 * Constructing a FName interns the string into the global pool; identical
 * strings share one entry. Default-constructed FName is None (empty).
 *
 *   const FName Bone = "head";
 *   const FName Also = "head";       // same pool entry
 *   Bone == Also;                    // true, O(1)
 *   Bone.ToString();                 // "head"
 */
class MAHO_NAME_API FName
{
public:
	FName() = default;
	FName(std::string_view Str);

	[[nodiscard]] std::string_view ToString() const;
	[[nodiscard]] bool IsNone() const { return Id == 0; }

	/** The interned pool index — the perfect hash key. */
	[[nodiscard]] std::uint32_t GetId() const { return Id; }

	[[nodiscard]] bool operator==(const FName& Other) const { return Id == Other.Id; }
	[[nodiscard]] bool operator!=(const FName& Other) const { return Id != Other.Id; }
	[[nodiscard]] bool operator<(const FName& Other) const { return Id < Other.Id; }

private:
	friend class FNamePool;
	explicit FName(std::uint32_t InId) : Id(InId) {}

	std::uint32_t Id = 0;
};

/** Global interned string pool (pre-app singleton). Init/Clear on lifecycle. */
class MAHO_NAME_API FNamePool final : public TExtension<EToolStage, FNamePool>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

	/** Intern a string — returns the canonical FName (thread-safe). */
	[[nodiscard]] FName Intern(std::string_view Str);

private:
	friend TSingleton<FNamePool>;
	FNamePool() = default;
};

} // namespace Name

} // namespace Maho

template <>
struct std::hash<Maho::Name::FName>
{
	std::size_t operator()(const Maho::Name::FName& Name) const noexcept
	{
		return std::hash<std::uint32_t>{}(Name.GetId());
	}
};
