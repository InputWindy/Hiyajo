#pragma once

/**
 * Explicit Importer / Exporter types for FResourceSystem::Import / Export.
 * DOTS-aligned: no UObject, no FObjectRef, no FSoftObjectPath, no GC.
 */

#include <Core/Extension/Resource/ResourceSystem.h>

#include <Core/System/Log.h>
#include <Core/System/Paths.h>

#include <type_traits>
#include <utility>

namespace Maho
{

class MAHO_API IResourceImporter
{
public:
	virtual ~IResourceImporter() = default;

	[[nodiscard]] virtual EAssetType GetType() const = 0;
	[[nodiscard]] virtual bool MatchesSourcePath(const std::string& SourcePath) const = 0;

	[[nodiscard]] virtual bool ApplyBulkData(
		FResourceSystem& Manager,
		FResourceImportConfig& Config,
		FResourceBulkData& Bulk) = 0;
};

class MAHO_API IResourceExporter
{
public:
	virtual ~IResourceExporter() = default;

	[[nodiscard]] virtual EAssetType GetType() const = 0;
	[[nodiscard]] virtual bool CanExport(const FResource& Resource) const = 0;
	[[nodiscard]] virtual bool Export(FResourceExportConfig Config, const FResource& Resource) = 0;
};

template <typename TResource>
struct TResourceIOTraits
{
	static_assert(sizeof(TResource) == 0, "Specialize TResourceIOTraits for this resource type");
};

template <typename TResource>
class TResourceImporter final : public IResourceImporter
{
public:
	static_assert(std::is_base_of_v<FResource, TResource>, "TResource must derive from FResource");

	using FTraits = TResourceIOTraits<TResource>;

	[[nodiscard]] EAssetType GetType() const override { return FTraits::GetType(); }
	[[nodiscard]] bool MatchesSourcePath(const std::string& SourcePath) const override
	{
		return FTraits::MatchesSourcePath(SourcePath);
	}

	[[nodiscard]] bool ApplyBulkData(
		FResourceSystem& Manager,
		FResourceImportConfig& Config,
		FResourceBulkData& Bulk) override
	{
		if (Config.TypeHint == EAssetType::Unknown)
			Config.TypeHint = FTraits::GetType();
		return Manager.ApplyTypedBulkData<TResource>(Config, Bulk);
	}
};

template <typename TResource>
class TResourceExporter final : public IResourceExporter
{
public:
	static_assert(std::is_base_of_v<FResource, TResource>, "TResource must derive from FResource");

	using FTraits = TResourceIOTraits<TResource>;

	[[nodiscard]] EAssetType GetType() const override { return FTraits::GetType(); }
	[[nodiscard]] bool CanExport(const FResource& Resource) const override
	{
		return dynamic_cast<const TResource*>(&Resource) != nullptr;
	}

	[[nodiscard]] bool Export(FResourceExportConfig Config, const FResource& Resource) override
	{
		const TResource* Typed = dynamic_cast<const TResource*>(&Resource);
		if (!Typed)
		{
			MAHO_CORE_ERROR("TResourceExporter: Resource is not the expected type");
			return false;
		}
		if (Config.DestinationPath.empty())
		{
			MAHO_CORE_ERROR("TResourceExporter: empty DestinationPath");
			return false;
		}
		return FTraits::ExportSource(Config, *Typed);
	}
};

// ── IO Traits specializations ──────────────────────────────────
// Concrete asset types live in the game project and specialize
// TResourceIOTraits<TResource> there (see game Source/Resource/ResourceIOTraits.h).

template <typename TImporter>
bool FResourceSystem::Import(FResourceImportConfig Config, std::string& OutAssetPath)
{
	return EnqueueImport(std::make_unique<TImporter>(), std::move(Config), OutAssetPath);
}

template <typename TResource>
bool FResourceSystem::ApplyTypedBulkData(FResourceImportConfig& Config, FResourceBulkData& Bulk)
{
	using FTraits = TResourceIOTraits<TResource>;

	if (!IsInitialized() || !bAcceptingNewWork) return false;

	Config.SourcePath = NormalizeSourcePath(std::move(Config.SourcePath));
	Config.PackagePath = NormalizePackageName(std::move(Config.PackagePath));
	if (Config.ObjectName.empty() && !Config.SourcePath.empty())
		Config.ObjectName = MakeObjectNameFromSource(Config.SourcePath);

	if (Config.PackagePath.empty() || Config.ObjectName.empty() || Config.SourcePath.empty())
		return false;

	EAssetType Type = Config.TypeHint;
	if (Type == EAssetType::Unknown)
		Type = FTraits::GetType();

	const std::string Key = MakeAssetCatalogKey(Config.PackagePath, Config.ObjectName);
	if (Catalog.find(Key) != Catalog.end())
	{
		MAHO_CORE_ERROR("FResourceSystem::ApplyTypedBulkData: '{}' already exists", Key);
		return false;
	}

	auto Resource = std::make_unique<TResource>(Config.ObjectName, Type, Config.SourcePath);
	TResource* Raw = Resource.get();
	Catalog[Key] = Ref<FResource>(Resource.release());
	RegisterOwnedResource(Config.PackagePath, Raw);

	Raw->LoadState = EAssetLoadState::Pending;
	const bool bOk = FTraits::ImportSource(Config, Bulk, *Raw, this);
	Raw->LoadState = bOk ? EAssetLoadState::Ready : EAssetLoadState::Failed;
	if (!bOk)
	{
		AbortFailedImport(*Raw);
		return false;
	}
	Raw->MarkDirty();
	return true;
}

template <typename TExporter>
bool FResourceSystem::Export(FResourceExportConfig Config, const std::string& SourcePath)
{
	if (!IsInitialized() || !bAcceptingNewWork) return false;

			auto Resource = Find<FResource>(SourcePath);
	if (!Resource) return false;

	TExporter Exporter;
	if (!Exporter.CanExport(*Resource) || !Exporter.Export(std::move(Config), *Resource))
		return false;

	return true;
}

} // namespace Maho
