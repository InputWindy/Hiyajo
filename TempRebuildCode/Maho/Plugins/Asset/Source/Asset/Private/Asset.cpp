#include <Asset.h>

#include <filesystem>

namespace Maho::Asset
{

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

bool FAssetRegistry::ExecuteStage(EToolStage Stage)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	switch (Stage)
	{
	case EToolStage::Init:
		Assets.clear();
		break;

	case EToolStage::Shutdown:
		Assets.clear();
		break;

	default:
		break;
	}
	return true;
}

void FAssetRegistry::Scan(const std::filesystem::path& ContentDir, std::string_view MountAlias)
{
	std::error_code Error;
	if (!std::filesystem::is_directory(ContentDir, Error))
	{
		return;
	}

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

} // namespace Maho::Asset

// ── Dynamic plugin entry (runtime load/unload via FPluginManager) ──

namespace
{

class FAssetRegistryAdapter final : public Maho::IExtension<Maho::EToolStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EToolStage Stage) override
	{
		return Maho::Asset::FAssetRegistry::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_ASSET_API Maho::IExtension<Maho::EToolStage>* CreateExtension()
{
	return new FAssetRegistryAdapter();
}
