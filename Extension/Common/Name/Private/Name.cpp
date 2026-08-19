#include "Name.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Maho
{

namespace Name
{

namespace
{
	std::mutex GPoolMutex;
	std::vector<std::string> GPool;                            // index → string (0 = None)
	std::unordered_map<std::string, std::uint32_t> GLookup;    // string → index
}

FName::FName(std::string_view Str)
	: Id(FNamePool::Get().Intern(Str).Id)
{
}

std::string_view FName::ToString() const
{
	return GPool[Id];
}

bool FNamePool::ExecuteStage(ENameStage Stage)
{
	std::lock_guard<std::mutex> Lock(GPoolMutex);
	switch (Stage)
	{
	case ENameStage::Init:
		GPool.clear();
		GLookup.clear();
		GPool.emplace_back();   // reserve index 0 = None (empty)
		break;

	case ENameStage::Shutdown:
		GPool.clear();
		GLookup.clear();
		break;
	}
	return true;
}

FName FNamePool::Intern(std::string_view Str)
{
	if (Str.empty())
	{
		return FName(0);
	}

	std::lock_guard<std::mutex> Lock(GPoolMutex);
	if (GPool.empty())
	{
		GPool.emplace_back();   // lazily reserve index 0 = None before first intern
	}

	const auto It = GLookup.find(std::string(Str));
	if (It != GLookup.end())
	{
		return FName(It->second);
	}

	const std::uint32_t Id = static_cast<std::uint32_t>(GPool.size());
	GPool.emplace_back(Str);
	GLookup.emplace(GPool.back(), Id);
	return FName(Id);
}

} // namespace Name

} // namespace Maho
