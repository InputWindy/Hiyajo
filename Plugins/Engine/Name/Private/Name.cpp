#include "Name.h"

namespace Maho::Name
{

static FNamePool* GNamePool = nullptr;

FNamePool* GetNamePool()
{
	return GNamePool;
}

void FNamePool::Initialize(FEngineBase&)
{
	GNamePool = this;
	free();
}

void FNamePool::Shutdown(FEngineBase&)
{
	GNamePool = nullptr;
	free();
}

FName::FName(std::string_view Str)
	: Id(GetNamePool()->Intern(Str).Id)
{
}

std::string_view FName::ToString() const
{
	return GetNamePool()->StringForId(Id);
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
		return FName{};
	}

	std::lock_guard<std::mutex> Lock(Mutex);
	const auto It = Lookup.find(std::string(Str));
	if (It != Lookup.end())
	{
		return FName{ It->second };
	}

	const std::uint32_t NextId = static_cast<std::uint32_t>(Pool.size());
	Pool.emplace_back(Str);
	Lookup.emplace(Pool.back(), NextId);
	return FName{ NextId };
}

} // namespace Maho::Name

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_NAME_API Maho::FLayerBase* CreateLayer()
{
	return Maho::Name::FNamePool::CreateLayer();
}

