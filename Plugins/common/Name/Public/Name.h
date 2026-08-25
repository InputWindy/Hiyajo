#pragma once

#include <Core/Singleton.h>
#include <Engine/Layer.h>

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
 */
class FName
{
public:
	FName() = default;
	explicit FName(std::string_view Str);

	[[nodiscard]] std::string_view ToString() const;
	[[nodiscard]] bool IsNone() const { return Id == 0; }
	[[nodiscard]] std::uint32_t GetId() const { return Id; }

	[[nodiscard]] bool operator==(const FName& O) const { return Id == O.Id; }
	[[nodiscard]] bool operator!=(const FName& O) const { return Id != O.Id; }
	[[nodiscard]] bool operator<(const FName& O) const { return Id < O.Id; }

private:
	friend class FNamePool;
	explicit FName(std::uint32_t InId) : Id(InId) {}

	std::uint32_t Id = 0;
};

/** Global interned string pool — a singleton service. */
class FNamePool
	: public TSingleton<FNamePool>
	, public IPlugin<IInitialize, IShutdown>
{
public:
	/** Process-unique accessor — declared here, defined in Name.cpp (in Name.dll). */
	static FNamePool& Get();

	void Initiate(int, char**) override { free(); }
	void Shutdown() override { free(); }

	/** Intern a string — returns the canonical FName (thread-safe). */
	FName Intern(std::string_view Str);

	/** The string stored at Id (the reverse of Intern). */
	[[nodiscard]] std::string_view StringForId(std::uint32_t Id) const;

private:
	void free();

	std::mutex Mutex;
	std::vector<std::string> Pool;
	std::unordered_map<std::string, std::uint32_t> Lookup;
};

} // namespace Name
} // namespace Maho

template <>
struct std::hash<Maho::Name::FName>
{
	std::size_t operator()(const Maho::Name::FName& N) const noexcept
	{
		return std::hash<std::uint32_t>{}(N.GetId());
	}
};
