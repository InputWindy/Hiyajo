#include "Name.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Maho
{

namespace Name
{

void FNameTool::Clear()
{
	std::lock_guard<std::mutex> Lock(Mutex);
	Pool.clear();
	Lookup.clear();
	Pool.emplace_back();   // reserve index 0 = None (empty)
}

FName::FName(std::string_view Str)
	: Id(FNameTool::Get().Intern(Str).Id)
{
}

std::string_view FName::ToString() const
{
	return FNameTool::Get().ToString(Id);
}

std::string_view FNameTool::ToString(std::uint32_t Id) const
{
	return Pool[Id];
}

FName FNameTool::Intern(std::string_view Str)
{
	if (Str.empty())
	{
		return FName(0);
	}

	std::lock_guard<std::mutex> Lock(Mutex);
	if (Pool.empty())
	{
		Pool.emplace_back();   // lazily reserve index 0 = None before first intern
	}

	const auto It = Lookup.find(std::string(Str));
	if (It != Lookup.end())
	{
		return FName(It->second);
	}

	const std::uint32_t Id = static_cast<std::uint32_t>(Pool.size());
	Pool.emplace_back(Str);
	Lookup.emplace(Pool.back(), Id);
	return FName(Id);
}

} // namespace Name

} // namespace Maho
