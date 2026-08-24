#include "Name.h"

namespace Maho::Name
{

FName::FName(std::string_view Str)
	: Id(FNamePool::Get().Intern(Str).Id)
{
}

std::string_view FName::ToString() const
{
	return FNamePool::Get().StringForId(Id);
}

std::string_view FNamePool::StringForId(std::uint32_t Id) const
{
	return Id < Pool.size() ? Pool[Id] : std::string_view{};
}

void FNamePool::free()
{
	Pool.clear();
	Lookup.clear();
}

FName FNamePool::Intern(std::string_view Str)
{
	if (Str.empty())
	{
		return FName(0);
	}

	std::lock_guard<std::mutex> Lock(Mutex);
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

} // namespace Maho::Name
