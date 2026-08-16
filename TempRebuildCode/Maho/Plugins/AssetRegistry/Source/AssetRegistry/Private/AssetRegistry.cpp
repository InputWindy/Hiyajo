#include <AssetRegistry.h>

#include <Paths.h>

#include <fstream>
#include <iterator>
#include <mutex>

namespace Maho::AssetRegistry
{

namespace
{
	std::mutex GMutex;

	EAssetType TypeFromExtension(std::string_view Ext)
	{
		if (Ext == ".material")     return EAssetType::Material;
		if (Ext == ".texture")      return EAssetType::Texture;
		if (Ext == ".staticmesh")   return EAssetType::StaticMesh;
		if (Ext == ".skeletalmesh") return EAssetType::SkeletalMesh;
		if (Ext == ".blueprint")    return EAssetType::Blueprint;
		if (Ext == ".sound")        return EAssetType::Sound;
		return EAssetType::Unknown;
	}

	FAssetPath ToLogicalPath(const std::filesystem::path& ContentDir, const std::filesystem::path& File)
	{
		const std::filesystem::path Rel = std::filesystem::relative(File, ContentDir);
		const std::filesystem::path Parent = Rel.parent_path();
		const std::string Stem = Rel.stem().string();

		std::string Logical = "/Game";
		if (!Parent.empty())
		{
			Logical += "/" + Parent.generic_string();
		}
		Logical += "/" + Stem;
		return FAssetPath(std::move(Logical));
	}
}

bool FAssetRegistry::ExecuteStage(EToolStage Stage)
{
	std::lock_guard<std::mutex> Lock(GMutex);
	if (Stage == EToolStage::Init || Stage == EToolStage::Shutdown)
	{
		Assets.clear();
	}
	return true;
}

void FAssetRegistry::Scan(std::filesystem::path ContentDir)
{
	if (!std::filesystem::is_directory(ContentDir))
	{
		return;
	}

	// The "/Game" logical root is a FPaths alias — re-map it so asset paths
	// follow the platform-root abstraction.
	Maho::Paths::FPaths::Get().SetRoot("Game", ContentDir);

	std::lock_guard<std::mutex> Lock(GMutex);
	for (const auto& Entry : std::filesystem::recursive_directory_iterator(ContentDir))
	{
		if (!Entry.is_regular_file())
		{
			continue;
		}
		const std::filesystem::path File = Entry.path();
		const EAssetType Type = TypeFromExtension(File.extension().string());
		if (Type == EAssetType::Unknown)
		{
			continue;
		}

		FAssetData Data;
		Data.Path = ToLogicalPath(ContentDir, File);
		Data.Type = Type;
		Data.File = File;
		// TODO: parse the asset file to discover Dependencies.
		Assets[Data.Path] = std::move(Data);
	}
}

const FAssetData* FAssetRegistry::Find(const FAssetPath& Path) const
{
	std::lock_guard<std::mutex> Lock(GMutex);
	const auto It = Assets.find(Path);
	return It != Assets.end() ? &It->second : nullptr;
}

std::filesystem::path FAssetRegistry::Resolve(const FAssetPath& Path) const
{
	// "/Game/Materials/M_Metal" → FPaths "Game" root + "Materials/M_Metal".
	std::string_view P = Path.GetPath();
	if (!P.empty() && P.front() == '/')
	{
		P.remove_prefix(1);
	}
	return Maho::Paths::FPaths::Get().Resolve(P);
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
	std::lock_guard<std::mutex> Lock(GMutex);
	return Assets.size();
}

} // namespace Maho::AssetRegistry

// ── Dynamic plugin entry (runtime load/unload via FPluginManager) ──

namespace
{

class FAssetRegistryAdapter final : public Maho::IExtension<Maho::EToolStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EToolStage Stage) override
	{
		return Maho::AssetRegistry::FAssetRegistry::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_ASSETREGISTRY_API Maho::IExtension<Maho::EToolStage>* CreateExtension()
{
	return new FAssetRegistryAdapter();
}
