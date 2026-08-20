#include "Asset.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace Maho
{

namespace Asset
{

void FAssetTool::Clear()
{
	std::lock_guard<std::mutex> Lock(Mutex);
	Assets.clear();
	MountRoots.clear();
}

namespace
{
	[[nodiscard]] EAssetType TypeFromExtension(std::string_view Extension)
	{
		if (Extension == ".material")
		{
			return EAssetType::Material;
		}
		if (Extension == ".texture")
		{
			return EAssetType::Texture;
		}
		return EAssetType::Unknown;
	}
}

void FAssetTool::Scan(const std::filesystem::path& ContentDir, std::string_view MountAlias)
{
	std::error_code Error;
	if (!std::filesystem::is_directory(ContentDir, Error))
	{
		return;
	}

	std::lock_guard<std::mutex> Lock(Mutex);

	// Register the mount alias as a root so Resolve() can map logical paths
	// back to physical files.
	MountRoots[std::string(MountAlias)] = ContentDir;

	for (const std::filesystem::directory_entry& Entry : std::filesystem::recursive_directory_iterator(ContentDir, Error))
	{
		if (Error || !Entry.is_regular_file())
		{
			continue;
		}

		const std::string Relative = std::filesystem::relative(Entry.path(), ContentDir).generic_string();
		const std::size_t Dot = Relative.find_last_of('.');
		if (Dot == std::string::npos)
		{
			continue;   // no extension —?skip
		}

		FAssetData Data;
		Data.Path = FAssetPath("/" + std::string(MountAlias) + "/" + Relative.substr(0, Dot));
		Data.Type = TypeFromExtension(Relative.substr(Dot));
		Data.File = Entry.path();
		Assets[std::string(Data.Path.GetPath())] = std::move(Data);
	}
}

const FAssetData* FAssetTool::Find(const FAssetPath& Path) const
{
	std::lock_guard<std::mutex> Lock(Mutex);
	const auto It = Assets.find(std::string(Path.GetPath()));
	return It != Assets.end() ? &It->second : nullptr;
}

std::filesystem::path FAssetTool::Resolve(const FAssetPath& Path) const
{
	// "/Game/Materials/M_Metal" →?mount root "Game" + "Materials/M_Metal".
	std::string_view P = Path.GetPath();
	if (!P.empty() && P.front() == '/')
	{
		P.remove_prefix(1);
	}
	const std::size_t Separator = P.find_first_of("/:");
	if (Separator == std::string_view::npos)
	{
		return std::filesystem::path(std::string(P));
	}

	std::lock_guard<std::mutex> Lock(Mutex);
	const std::string Alias(P.substr(0, Separator));
	const auto It = MountRoots.find(Alias);
	if (It == MountRoots.end())
	{
		return std::filesystem::path(std::string(P));
	}

	std::filesystem::path Result = It->second;
	Result /= std::string(P.substr(Separator + 1));
	return Result;
}

std::optional<std::vector<std::uint8_t>> FAssetTool::Load(const FAssetPath& Path) const
{
	const FAssetData* Data = Find(Path);
	if (Data == nullptr)
	{
		return std::nullopt;
	}

	std::ifstream Stream(Data->File, std::ios::binary);
	if (!Stream)
	{
		return std::nullopt;
	}
	std::vector<std::uint8_t> Bytes(
		(std::istreambuf_iterator<char>(Stream)),
		std::istreambuf_iterator<char>());
	return Bytes;
}

std::size_t FAssetTool::GetAssetCount() const
{
	std::lock_guard<std::mutex> Lock(Mutex);
	return Assets.size();
}

} // namespace Asset

} // namespace Maho
