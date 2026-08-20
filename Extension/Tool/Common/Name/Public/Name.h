#pragma once

#include "NameApi.h"
#include <Maho.h>
#include <Engine/Tool.h>

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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
	friend class FNameTool;
	explicit FName(std::uint32_t InId) : Id(InId) {}

	std::uint32_t Id = 0;
};

/** Global interned string pool (pre-app singleton). Init/Clear on lifecycle. */
class MAHO_NAME_API FNameTool : public TTool<FNameTool>
{
public:
	/** Identity tag — this is a Tool. */
	using FTags = TTypeList<FToolTag>;

	/** Intern a string — returns the canonical FName (thread-safe). */
	[[nodiscard]] FName Intern(std::string_view Str);

	/** Look up a pooled string by index. */
	[[nodiscard]] std::string_view ToString(std::uint32_t Id) const;

protected:
	// ── 写（protected，仅调度器）──

	/** Clear the intern pool and reserve index 0 = None. Lifecycle write. */
	void Clear();

private:
	template <typename TExtension, typename TStage>
	friend bool Maho::ExecuteExtension(TStage Stage);

	mutable std::mutex Mutex;
	std::vector<std::string> Pool;                            // index → string (0 = None)
	std::unordered_map<std::string, std::uint32_t> Lookup;    // string → index
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
