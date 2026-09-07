#include "Paths.h"

#include <utility>

namespace Maho::Paths
{

static FPaths* GPaths = nullptr;

FPaths* GetPaths()
{
	return GPaths;
}

void FPaths::Initialize(FEngineBase&)
{
	GPaths = this;
	Roots.clear();
}

void FPaths::Shutdown(FEngineBase&)
{
	GPaths = nullptr;
	Roots.clear();
}

void FPaths::SetRoot(std::string_view Alias, std::filesystem::path Path)
{
	Roots[std::string(Alias)] = std::move(Path);
}

std::filesystem::path FPaths::Resolve(std::string_view VirtualPath) const
{
	const std::size_t Separator = VirtualPath.find_first_of("/:");
	if (Separator == std::string_view::npos)
	{
		const auto It = Roots.find(std::string(VirtualPath));
		if (It != Roots.end())
		{
			return It->second;
		}
		return std::filesystem::path(std::string(VirtualPath));
	}

	const std::string_view Alias = VirtualPath.substr(0, Separator);
	const auto It = Roots.find(std::string(Alias));
	if (It == Roots.end())
	{
		return std::filesystem::path(std::string(VirtualPath));
	}

	std::filesystem::path Result = It->second;
	Result /= std::string(VirtualPath.substr(Separator + 1));
	return Result;
}

bool FPaths::HasRoot(std::string_view Alias) const
{
	return Roots.find(std::string(Alias)) != Roots.end();
}

} // namespace Maho::Paths

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_PATHS_API Maho::FLayerBase* CreateLayer()
{
	return Maho::Paths::FPaths::CreateLayer();
}

