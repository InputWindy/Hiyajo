#include "Asset.h"

#include <Paths.h>

#include <filesystem>
#include <fstream>
#include <iterator>

namespace Maho::Asset
{

FAssetRegistry& FAssetRegistry::Get()
{
	static FAssetRegistry Instance;
	return Instance;
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

void FAssetRegistry::Initialize(int Argc, char** Argv)
{
	(void)Argc; (void)Argv;
	std::lock_guard<std::mutex> Lock(Mutex);
	Assets.clear();
}

void FAssetRegistry::Shutdown()
{
	std::lock_guard<std::mutex> Lock(Mutex);
	Assets.clear();
}

void FAssetRegistry::Scan(const std::filesystem::path& ContentDir, std::string_view MountAlias)
{
	std::error_code Error;
	if (!std::filesystem::is_directory(ContentDir, Error))
	{
		return;
	}

	// The mount alias is a FPaths root — re-map it so asset paths follow the
	// platform-root abstraction.
	Paths::FPaths::Get().SetRoot(MountAlias, ContentDir);

	std::lock_guard<std::mutex> Lock(Mutex);
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
			continue;   // no extension — skip
		}

		FAssetData Data;
		Data.Path = FAssetPath("/" + std::string(MountAlias) + "/" + Relative.substr(0, Dot));
		Data.Type = TypeFromExtension(Relative.substr(Dot));
		Data.File = Entry.path();
		Assets[std::string(Data.Path.GetPath())] = std::move(Data);
	}
}

const FAssetData* FAssetRegistry::Find(const FAssetPath& Path) const
{
	std::lock_guard<std::mutex> Lock(Mutex);
	const auto It = Assets.find(std::string(Path.GetPath()));
	return It != Assets.end() ? &It->second : nullptr;
}

std::filesystem::path FAssetRegistry::Resolve(const FAssetPath& Path) const
{
	// "/Game/Materials/M_Metal" → FPaths root "Game" + "Materials/M_Metal".
	std::string_view P = Path.GetPath();
	if (!P.empty() && P.front() == '/')
	{
		P.remove_prefix(1);
	}
	return Paths::FPaths::Get().Resolve(P);
}

std::optional<std::vector<std::uint8_t>> FAssetRegistry::Load(const FAssetPath& Path) const
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

std::size_t FAssetRegistry::GetAssetCount() const
{
	std::lock_guard<std::mutex> Lock(Mutex);
	return Assets.size();
}

} // namespace Maho::Asset
