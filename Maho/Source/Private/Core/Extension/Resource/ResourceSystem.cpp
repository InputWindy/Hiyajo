#include <Core/Extension/Resource/ResourceIO.h>
#include <Core/Extension/Resource/ResourceCasset.h>
#include <Core/Extension/Resource/ResourceServer.h>

#include <Core/Application/App.h>
#include <Core/Json.h>
#include <Core/System/Log.h>
#include <Core/System/Paths.h>
#include <Core/System/Utf8Path.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <utility>

namespace Maho
{

FResourceSystem::FResourceSystem()
	: Server(std::make_unique<FResourceServer>())
{
}

FResourceSystem::~FResourceSystem()
{
	Shutdown();
}

bool FResourceSystem::IsInitialized() const
{
	return Server && Server->IsInitialized();
}

std::string FResourceSystem::NormalizePackageName(std::string Name)
{
	for (char& Ch : Name)
	{
		if (Ch == '\\') Ch = '/';
	}
	while (Name.size() >= 2 && Name[0] == '.' && Name[1] == '/')
		Name.erase(0, 2);
	return Name;
}

std::string FResourceSystem::NormalizeSourcePath(std::string Path)
{
	for (char& Ch : Path)
	{
		if (Ch == '\\') Ch = '/';
	}
#if defined(_WIN32)
	AsciiToLowerInPlace(Path);
#endif
	while (Path.size() >= 2 && Path[0] == '.' && Path[1] == '/')
		Path.erase(0, 2);
	return Path;
}

std::string FResourceSystem::MakeObjectNameFromSource(const std::string& SourcePath)
{
	const std::size_t Slash = SourcePath.find_last_of("/\\");
	const std::size_t Start = (Slash == std::string::npos) ? 0 : Slash + 1;
	std::string Stem = SourcePath.substr(Start);
	const std::size_t Dot = Stem.find_last_of('.');
	if (Dot != std::string::npos) Stem.resize(Dot);
	return Stem.empty() ? std::string("Resource") : Stem;
}

std::string FResourceSystem::MakeAssetCatalogKey(const std::string& PackagePath, const std::string& ObjectName)
{
	return PackagePath + "." + ObjectName;
}

std::string FResourceSystem::NormalizeResourceVirtualPath(const std::string& VirtualPath)
{
	std::string Path = VirtualPath;
	for (char& Ch : Path)
	{
		if (Ch == '\\') Ch = '/';
	}
	const std::size_t LastSlash = Path.find_last_of('/');
	if (LastSlash != std::string::npos && LastSlash > 0 && LastSlash + 1 < Path.size())
		Path[LastSlash] = '.';
	return Path;
}

bool FResourceSystem::Initialize()
{
	if (IsInitialized()) return true;

	if (!Server->Initialize())
	{
		MAHO_CORE_ERROR("FResourceSystem::Initialize: FResourceServer failed");
		return false;
	}

	bAcceptingNewWork = true;
	MAHO_CORE_INFO("FResourceSystem initialized");
	return true;
}

FResourcePackage* FResourceSystem::FindOrCreatePackage(const std::string& PackagePath, const std::string& FilePath)
{
	std::string Normalized = NormalizePackageName(PackagePath);
	auto It = Packages.find(Normalized);
	if (It != Packages.end()) return It->second.get();

	auto Pkg = std::make_unique<FResourcePackage>();
	Pkg->Name = Normalized;
	Pkg->FilePath = FilePath;
	FResourcePackage* Raw = Pkg.get();
	Packages.emplace(Normalized, std::move(Pkg));
	return Raw;
}

bool FResourceSystem::RegisterResource(Ref<FResource> Resource, const std::string& PackagePath)
{
	if (!bAcceptingNewWork)
	{
		MAHO_CORE_ERROR("FResourceSystem::RegisterResource: refused during exit");
		return false;
	}
	if (!Resource)
	{
		MAHO_CORE_ERROR("FResourceSystem::RegisterResource: null resource");
		return false;
	}

	std::string PkgPath = PackagePath.empty() ? Resource->GetName() : NormalizePackageName(PackagePath);
	const std::string Key = MakeAssetCatalogKey(PkgPath, Resource->GetName());

	if (Catalog.find(Key) != Catalog.end())
	{
		MAHO_CORE_ERROR("FResourceSystem::RegisterResource: '{}' already registered", Key);
		return false;
	}

	FResourcePackage* Pkg = FindOrCreatePackage(PkgPath);
	Pkg->Objects.push_back(Resource.Get());
	Catalog[Key] = std::move(Resource);
	return true;
}

bool FResourceSystem::RegisterOwnedResource(const std::string& PackagePath, FResource* Resource)
{
	if (!Resource) return false;

	std::string PkgPath = NormalizePackageName(PackagePath);
	FResourcePackage* Pkg = FindOrCreatePackage(PkgPath);
	Pkg->Objects.push_back(Resource);

	const std::string Key = MakeAssetCatalogKey(PkgPath, Resource->GetName());
	// Catalog already has the unique_ptr; just ensure package linkage
	return true;
}

bool FResourceSystem::UnregisterResource(FResource* Resource)
{
	if (!Resource) return false;

	CancelPendingImport(Resource);

	for (auto It = Catalog.begin(); It != Catalog.end(); ++It)
	{
		if (It->second.Get() == Resource)
		{
			Catalog.erase(It);
			return true;
		}
	}
	return false;
}

void FResourceSystem::AbortFailedImport(FResource& Resource)
{
	UnregisterResource(&Resource);
}

void FResourceSystem::CancelPendingImport(FResource* Resource)
{
	if (!Resource) return;

	const std::string Key = MakeAssetCatalogKey(
		NormalizePackageName(Resource->GetName()),
		Resource->GetName());

	const auto It = PendingIO.find(Key);
	if (It == PendingIO.end()) return;

	ReleaseBulkLoad(It->second.Handle);
	PendingIO.erase(It);
}

void FResourceSystem::ForEachRegisteredResource(
	const std::function<void(const std::string& CatalogKey, FResource& Resource)>& Fn) const
{
	if (!Fn) return;
	for (const auto& Pair : Catalog)
	{
		if (Pair.second)
			Fn(Pair.first, *Pair.second);
	}
}

void FResourceSystem::UnregisterResourcesInPackage(const std::string& PackagePath)
{
	std::string Key = NormalizePackageName(PackagePath);
	for (auto It = Catalog.begin(); It != Catalog.end();)
	{
		if (It->first.find(Key) == 0)
			It = Catalog.erase(It);
		else
			++It;
	}
	Packages.erase(Key);
}

void FResourceSystem::PrepareForExit()
{
	if (!IsInitialized()) return;

	bAcceptingNewWork = false;
	if (AsyncSave.Task)
	{
		AsyncSave.Task->Wait();
		AsyncSave = {};
	}
	FlushAll();

	for (auto& Pair : PendingIO)
		ReleaseBulkLoad(Pair.second.Handle);
	PendingIO.clear();

	Catalog.clear();
	Packages.clear();
}

bool FResourceSystem::IsIdle() const
{
	if (!IsInitialized()) return true;
	return !bAcceptingNewWork
		&& Catalog.empty()
		&& PendingIO.empty()
		&& !AsyncSave.bActive
		&& !Server->HasPendingLoads();
}

bool FResourceSystem::ExecuteStage(EEngineStage Stage)
{
	switch (Stage)
	{
	case EEngineStage::Init:
		if (!Initialize())
		{
			MAHO_CORE_ERROR("FResourceSystem: Initialize failed");
			return false;
		}
		return true;
	case EEngineStage::BeginFrame:
	case EEngineStage::Update:
		ProcessReadyIO();
		TickSavePackage();
		return true;
	case EEngineStage::PrepareExit:
		PrepareForExit();
		return true;
	case EEngineStage::Shutdown:
		if (IsInitialized()) Shutdown();
		return true;
	default:
		return true;
	}
}

void FResourceSystem::Shutdown()
{
	if (!IsInitialized()) return;
	PrepareForExit();

	if (Server->IsInitialized())
		Server->Shutdown();

	MAHO_CORE_INFO("FResourceSystem shut down");
}

void FResourceSystem::ProcessReadyIO()
{
	if (!HasActiveServer() || PendingIO.empty()) return;

	std::vector<std::string> ReadyKeys;
	ReadyKeys.reserve(PendingIO.size());
	for (const auto& Pair : PendingIO)
	{
		const FTransferHandle Handle = Pair.second.Handle;
		if (!Handle.IsValid() || Handle.HasFailed() || Handle.HasSucceeded())
			ReadyKeys.push_back(Pair.first);
	}

	constexpr std::size_t kMaxAppliesPerTick = 1;
	std::size_t Applied = 0;
	for (const std::string& Key : ReadyKeys)
	{
		if (Applied >= kMaxAppliesPerTick) break;

		const auto It = PendingIO.find(Key);
		if (It == PendingIO.end()) continue;

		FPendingIO Pending = std::move(It->second);
		PendingIO.erase(It);

		if (!Pending.Handle.IsValid() || Pending.Handle.HasFailed())
		{
			ReleaseBulkLoad(Pending.Handle);
			continue;
		}

		FResourceBulkData Bulk;
		if (!TakeBulkData(Pending.Handle, Bulk))
		{
			ReleaseBulkLoad(Pending.Handle);
			continue;
		}

		const bool bOk = Pending.Importer
			&& Pending.Importer->ApplyBulkData(*this, Pending.Config, Bulk);
		ReleaseBulkLoad(Pending.Handle);
		++Applied;
	}
}

bool FResourceSystem::HasActiveServer() const
{
	return Server && Server->IsInitialized();
}

FTransferHandle FResourceSystem::RequestBulkLoad(const std::string& SourcePath)
{
	if (!HasActiveServer()) return {};
	return Server->RequestLoad(SourcePath);
}

void FResourceSystem::ReleaseBulkLoad(FTransferHandle Handle)
{
	if (HasActiveServer() && Handle.IsValid())
		Server->Release(Handle);
}

bool FResourceSystem::TakeBulkData(FTransferHandle Handle, FResourceBulkData& OutBulk)
{
	if (!HasActiveServer() || !Handle.IsValid()) return false;
	return Server->TryTakeBulkData(Handle, OutBulk);
}

bool FResourceSystem::EnqueueImport(
	std::unique_ptr<IResourceImporter> Importer,
	FResourceImportConfig Config,
	std::string& OutAssetPath)
{
	OutAssetPath.clear();
	if (!Importer) return false;

	if (!IsInitialized() || !bAcceptingNewWork) return false;

	Config.SourcePath = NormalizeSourcePath(std::move(Config.SourcePath));
	Config.PackagePath = NormalizePackageName(std::move(Config.PackagePath));
	if (Config.ObjectName.empty() && !Config.SourcePath.empty())
		Config.ObjectName = MakeObjectNameFromSource(Config.SourcePath);

	if (Config.PackagePath.empty() || Config.ObjectName.empty() || Config.SourcePath.empty())
		return false;

	OutAssetPath = MakeAssetCatalogKey(Config.PackagePath, Config.ObjectName);

	if (PendingIO.find(OutAssetPath) != PendingIO.end())
		return true;

	if (!HasActiveServer()) return false;

	FTransferHandle Handle = RequestBulkLoad(Config.SourcePath);
	if (!Handle.IsValid()) return false;

	FPendingIO Pending;
	Pending.Handle = Handle;
	Pending.AssetPath = OutAssetPath;
	Pending.Config = Config;
	Pending.Importer = std::move(Importer);
	PendingIO.emplace(OutAssetPath, std::move(Pending));

	return true;
}

bool FResourceSystem::TryLoad(const std::string& AssetPath, bool bAsync)
{
	if (AssetPath.empty()) return false;

	// Already loaded
	if (Find(AssetPath)) return true;

	const std::string Filename = FPaths::ConvertPackageNameToFilename(AssetPath);
	if (Filename.empty()) return false;

	FResourceImportConfig Config;
	Config.PackagePath = AssetPath;
	Config.SourcePath = Filename;
	Config.Mode = bAsync ? EResourceIOMode::Async : EResourceIOMode::Sync;
	std::string OutAssetPath;
	return Import<FCassetPackageImporter>(std::move(Config), OutAssetPath);
}

bool FResourceSystem::SavePackage(
	const std::string& PackagePath,
	bool bSaveDependencies)
{
	std::unordered_set<std::string> SavingPackageNames;
	return SavePackageInternal(PackagePath, bSaveDependencies, SavingPackageNames);
}

bool FResourceSystem::EnqueueSavePackage(
	const std::string& PackagePath,
	bool bSaveDependencies)
{
	if (AsyncSave.bActive)
	{
		MAHO_CORE_WARN("FResourceSystem::EnqueueSavePackage: save already in progress");
		return false;
	}
	if (!IsInitialized() || !bAcceptingNewWork) return false;

	std::string PkgPath = NormalizePackageName(PackagePath);
	auto It = Packages.find(PkgPath);
	if (It == Packages.end())
	{
		MAHO_CORE_ERROR("FResourceSystem::EnqueueSavePackage: package '{}' not loaded", PkgPath);
		return false;
	}

		FResourcePackage& Pkg = *It->second;
		std::string OutPath = FPaths::ConvertPackageNameToFilename(PkgPath);
		if (OutPath.empty()) return false;

	if (bSaveDependencies)
	{
		std::unordered_set<std::string> SavingPackageNames;
		for (const FResource* Obj : Pkg.Objects)
		{
			if (!Obj) continue;
			for (const std::string& Ref : Obj->GetReferencePaths())
			{
				std::size_t Dot = Ref.find('.');
				if (Dot == std::string::npos) continue;
				std::string DepPkg = NormalizePackageName(Ref.substr(0, Dot));
				if (DepPkg == PkgPath) continue;

				auto DepIt = Packages.find(DepPkg);
				if (DepIt == Packages.end()) continue;

				if (!SavePackageInternal(DepPkg, true, SavingPackageNames))
				{
					MAHO_CORE_ERROR("FResourceSystem::EnqueueSavePackage: failed saving dependency '{}'", DepPkg);
					return false;
				}
			}
		}
	}

	{
		std::error_code ErrorCode;
		const std::filesystem::path Parent = PathFromUtf8(OutPath).parent_path();
		if (!Parent.empty())
			std::filesystem::create_directories(Parent, ErrorCode);
	}

	AsyncSave = {};
	AsyncSave.bActive = true;
	AsyncSave.PackagePath = PkgPath;
	AsyncSave.OutPath = OutPath;
	AsyncSave.Progress = 0.15f;
	AsyncSave.StatusText = "Building package...";
	AsyncSave.FileBytes = std::make_shared<std::vector<std::uint8_t>>();
	AsyncSave.bOk = std::make_shared<std::atomic<bool>>(false);

	if (!ResourceCasset::BuildPackageDocument(Pkg, Pkg.Objects, AsyncSave.Document))
	{
		MAHO_CORE_ERROR("FResourceSystem::EnqueueSavePackage: BuildPackageDocument failed");
		AsyncSave = {};
		return false;
	}

	AsyncSave.Progress = 0.45f;
	AsyncSave.StatusText = "Compressing...";
	Pkg.FilePath = OutPath;

	auto Document = std::make_shared<std::vector<std::uint8_t>>(std::move(AsyncSave.Document));
	AsyncSave.Document.clear();
	auto FileBytes = AsyncSave.FileBytes;
	auto bOk = AsyncSave.bOk;
	const std::string WritePath = OutPath;

	AsyncSave.Task = FAsyncTask::LaunchNew([Document, FileBytes, bOk, WritePath]()
	{
		std::vector<std::uint8_t> LocalFile;
		if (!ResourceCasset::WrapDocumentToMcasFile(*Document, LocalFile))
		{
			bOk->store(false, std::memory_order_release);
			return;
		}
		std::ofstream Out(PathFromUtf8(WritePath), std::ios::binary | std::ios::trunc);
		if (!Out || !Out.write(reinterpret_cast<const char*>(LocalFile.data()), static_cast<std::streamsize>(LocalFile.size())))
		{
			bOk->store(false, std::memory_order_release);
			return;
		}
		*FileBytes = std::move(LocalFile);
		bOk->store(true, std::memory_order_release);
	});
	AsyncSave.bCompressStarted = true;
	return true;
}

bool FResourceSystem::IsSavePackageBusy() const { return AsyncSave.bActive; }

float FResourceSystem::GetSavePackageProgress() const
{
	if (!AsyncSave.bActive) return 1.f;
	if (!AsyncSave.bCompressStarted) return AsyncSave.Progress;
	if (AsyncSave.Task && !AsyncSave.Task->IsDone()) return 0.7f;
	return 0.95f;
}

const std::string& FResourceSystem::GetSavePackageStatusText() const
{
	static const std::string IdleText;
	return AsyncSave.bActive ? AsyncSave.StatusText : IdleText;
}

void FResourceSystem::TickSavePackage()
{
	if (!AsyncSave.bActive || !AsyncSave.bCompressStarted || !AsyncSave.Task) return;
	if (!AsyncSave.Task->IsDone())
	{
		AsyncSave.StatusText = "Compressing / writing...";
		AsyncSave.Progress = 0.7f;
		return;
	}

	AsyncSave.Task->Wait();
	const bool bOk = AsyncSave.bOk && AsyncSave.bOk->load(std::memory_order_acquire);
	if (bOk)
		FinalizeSavePackageSuccess();
	else
		FinalizeSavePackageFailure();
}

void FResourceSystem::FinalizeSavePackageSuccess()
{
	auto It = Packages.find(AsyncSave.PackagePath);
	if (It != Packages.end())
	{
		for (FResource* Obj : It->second->Objects)
		{
			if (Obj) Obj->ClearDirty();
		}
	}
	AsyncSave = {};
}

void FResourceSystem::FinalizeSavePackageFailure()
{
	MAHO_CORE_ERROR("FResourceSystem: async save failed for '{}' -> '{}'",
		AsyncSave.PackagePath, AsyncSave.OutPath);
	AsyncSave = {};
}

bool FResourceSystem::SavePackageInternal(
	const std::string& PackagePath,
	bool bSaveDependencies,
	std::unordered_set<std::string>& SavingPackageNames)
{
	if (!IsInitialized()) return false;

	std::string PkgKey = NormalizePackageName(PackagePath);
	if (SavingPackageNames.find(PkgKey) != SavingPackageNames.end())
	{
		MAHO_CORE_ERROR("FResourceSystem::SavePackage: cycle detected while saving '{}'", PkgKey);
		return false;
	}
	SavingPackageNames.insert(PkgKey);

	auto It = Packages.find(PkgKey);
	if (It == Packages.end())
	{
		MAHO_CORE_ERROR("FResourceSystem::SavePackage: package '{}' not loaded", PkgKey);
		SavingPackageNames.erase(PkgKey);
		return false;
	}

	FResourcePackage& Pkg = *It->second;
	const std::string OutPath = FPaths::ConvertPackageNameToFilename(PkgKey);
	if (OutPath.empty())
	{
		SavingPackageNames.erase(PkgKey);
		return false;
	}

	Pkg.FilePath = OutPath;

	if (bSaveDependencies)
	{
		for (const FResource* Obj : Pkg.Objects)
		{
			if (!Obj) continue;
			for (const std::string& Ref : Obj->GetReferencePaths())
			{
				std::size_t Dot = Ref.find('.');
				if (Dot == std::string::npos) continue;
				std::string DepPkg = NormalizePackageName(Ref.substr(0, Dot));
				if (DepPkg == PkgKey) continue;

				auto DepIt = Packages.find(DepPkg);
				if (DepIt == Packages.end()) continue;

				if (!SavePackageInternal(DepPkg, true, SavingPackageNames))
				{
					SavingPackageNames.erase(PkgKey);
					return false;
				}
			}
		}
	}

	{
		std::error_code ErrorCode;
		const std::filesystem::path Parent = PathFromUtf8(OutPath).parent_path();
		if (!Parent.empty())
			std::filesystem::create_directories(Parent, ErrorCode);
	}

	std::vector<std::uint8_t> FileBytes;
	if (!ResourceCasset::EncodePackageFile(Pkg, Pkg.Objects, FileBytes))
	{
		MAHO_CORE_ERROR("FResourceSystem::SavePackage: EncodePackageFile failed for '{}'", PkgKey);
		SavingPackageNames.erase(PkgKey);
		return false;
	}

	{
		std::ofstream Out(PathFromUtf8(OutPath), std::ios::binary | std::ios::trunc);
		if (!Out || !Out.write(reinterpret_cast<const char*>(FileBytes.data()), static_cast<std::streamsize>(FileBytes.size())))
		{
			MAHO_CORE_ERROR("FResourceSystem::SavePackage: write failed '{}'", OutPath);
			SavingPackageNames.erase(PkgKey);
			return false;
		}
	}

	for (FResource* Obj : Pkg.Objects)
	{
		if (Obj) Obj->ClearDirty();
	}

	SavingPackageNames.erase(PkgKey);
	return true;
}

std::string FResourceSystem::LoadPackage(const std::string& FilePath)
{
	std::unordered_set<std::string> LoadingFilePaths;
	return LoadPackageInternal(FilePath, LoadingFilePaths);
}

std::string FResourceSystem::LoadPackageInternal(
	const std::string& FilePath,
	std::unordered_set<std::string>& LoadingFilePaths)
{
	if (!IsInitialized() || !bAcceptingNewWork) return {};

	if (FilePath.empty()) return {};

	const std::string NormalizedFile = NormalizeSourcePath(FilePath);
	if (LoadingFilePaths.find(NormalizedFile) != LoadingFilePaths.end())
	{
		MAHO_CORE_ERROR("FResourceSystem::LoadPackage: cycle detected at '{}'", FilePath);
		return {};
	}
	LoadingFilePaths.insert(NormalizedFile);

	std::ifstream In(PathFromUtf8(FilePath), std::ios::binary | std::ios::ate);
	if (!In)
	{
		MAHO_CORE_ERROR("FResourceSystem::LoadPackage: open failed '{}'", FilePath);
		LoadingFilePaths.erase(NormalizedFile);
		return {};
	}
	const std::streamoff Size = In.tellg();
	if (Size < 0)
	{
		LoadingFilePaths.erase(NormalizedFile);
		return {};
	}
	In.seekg(0, std::ios::beg);
	std::vector<std::uint8_t> Bytes(static_cast<std::size_t>(Size));
	if (Size > 0 && !In.read(reinterpret_cast<char*>(Bytes.data()), Size))
	{
		MAHO_CORE_ERROR("FResourceSystem::LoadPackage: read failed '{}'", FilePath);
		LoadingFilePaths.erase(NormalizedFile);
		return {};
	}

	return LoadPackageFromBinary(FilePath, Bytes.data(), Bytes.size(), LoadingFilePaths);
}

namespace
{

template <typename TResource>
[[nodiscard]] TResource* CreateTypedResource(
	FResourceSystem& Manager,
	const std::string& PackagePath,
	const std::string& ObjectName,
	EAssetType Type,
	const std::string& ImportSource)
{
	auto Res = std::make_unique<TResource>(ObjectName, Type, ImportSource);
	TResource* Raw = Res.get();
	Manager.RegisterResource(Ref<FResource>(Res.release()), PackagePath);
	return Raw;
}

FResource* CreateResourceFromParsed(
	FResourceSystem& Manager,
	const std::string& PackageName,
	const ResourceCasset::FCassetParsedObject& Entry)
{
	if (Entry.Name.empty()) return nullptr;

	FResource* Created = nullptr;
	switch (Entry.Type)
	{
	case EAssetType::Texture2D:
		Created = CreateTypedResource<FTexture2D>(Manager, PackageName, Entry.Name, EAssetType::Texture2D, Entry.ImportSource);
		break;
	case EAssetType::Texture3D:
		Created = CreateTypedResource<FTexture3D>(Manager, PackageName, Entry.Name, Entry.Type, Entry.ImportSource);
		break;
	case EAssetType::TextureCube:
		Created = CreateTypedResource<FTextureCube>(Manager, PackageName, Entry.Name, Entry.Type, Entry.ImportSource);
		break;
	case EAssetType::TextureCubeArray:
		Created = CreateTypedResource<FTextureCubeArray>(Manager, PackageName, Entry.Name, Entry.Type, Entry.ImportSource);
		break;
	case EAssetType::Texture2DArray:
		Created = CreateTypedResource<FTexture2DArray>(Manager, PackageName, Entry.Name, Entry.Type, Entry.ImportSource);
		break;
	case EAssetType::Mesh:
		Created = CreateTypedResource<FStaticMesh>(Manager, PackageName, Entry.Name, Entry.Type, Entry.ImportSource);
		break;
	case EAssetType::Material:
		Created = CreateTypedResource<FMaterial>(Manager, PackageName, Entry.Name, Entry.Type, Entry.ImportSource);
		break;
	case EAssetType::Skeleton:
		Created = CreateTypedResource<FSkeleton>(Manager, PackageName, Entry.Name, Entry.Type, Entry.ImportSource);
		break;
	case EAssetType::Animation:
		Created = CreateTypedResource<FAnimation>(Manager, PackageName, Entry.Name, Entry.Type, Entry.ImportSource);
		break;
	case EAssetType::AnimationGraph:
		Created = CreateTypedResource<FAnimationGraph>(Manager, PackageName, Entry.Name, Entry.Type, Entry.ImportSource);
		break;
	case EAssetType::Prefab:
		Created = CreateTypedResource<FPrefab>(Manager, PackageName, Entry.Name, Entry.Type, Entry.ImportSource);
		break;
	// Level and World are ECS-owned; not created as FResource
	case EAssetType::Level:
	case EAssetType::World:
		MAHO_CORE_WARN("FResourceSystem: {} asset '{}' not supported as FResource (use ECS)",
			Entry.Type == EAssetType::Level ? "Level" : "World", Entry.Name);
		return nullptr;
	default:
		MAHO_CORE_ERROR("FResourceSystem: unsupported casset type {} for '{}'",
			static_cast<int>(Entry.Type), Entry.Name);
		return nullptr;
	}

	if (!Created) return nullptr;

	if ((Entry.RecordFlags & ResourceCasset::kRecordHasCpu) != 0)
	{
		if (!ResourceCasset::ApplyCpuPayload(*Created, Entry.CpuBytes))
		{
			Manager.UnregisterResource(Created);
			return nullptr;
		}
	}
	else
	{
		Created->MarkCpuReady();
	}

	return Created;
}

} // namespace

std::string FResourceSystem::LoadPackageFromBinary(
	const std::string& FilePath,
	const std::uint8_t* FileBytes,
	std::size_t FileSize,
	std::unordered_set<std::string>& LoadingFilePaths)
{
	const std::string NormalizedFile = NormalizeSourcePath(FilePath);
	if (LoadingFilePaths.find(NormalizedFile) == LoadingFilePaths.end())
		LoadingFilePaths.insert(NormalizedFile);

	ResourceCasset::FCassetParsedPackage Parsed;
	if (!ResourceCasset::DecodePackageFile(FileBytes, FileSize, Parsed))
	{
		LoadingFilePaths.erase(NormalizedFile);
		return {};
	}

	for (const ResourceCasset::FCassetDependency& Dep : Parsed.Dependencies)
	{
		const std::string DepName = NormalizePackageName(Dep.PackageName);
		if (!DepName.empty() && Packages.find(DepName) != Packages.end())
			continue;
		if (Dep.FilePath.empty()) continue;
		if (LoadPackageInternal(Dep.FilePath, LoadingFilePaths).empty())
		{
			MAHO_CORE_ERROR("FResourceSystem::LoadPackage: failed loading dependency '{}' from '{}'",
				DepName.empty() ? Dep.FilePath : DepName, Dep.FilePath);
			LoadingFilePaths.erase(NormalizedFile);
			return {};
		}
	}

	std::string PackageName = NormalizePackageName(Parsed.Name);
	if (PackageName.empty())
		PackageName = FilePath;

	FResourcePackage* Pkg = FindOrCreatePackage(PackageName, FilePath);
	Pkg->Flags = Parsed.PackageFlags;

	for (const ResourceCasset::FCassetParsedObject& Entry : Parsed.Objects)
	{
		FResource* Created = CreateResourceFromParsed(*this, PackageName, Entry);
		if (!Created)
		{
			MAHO_CORE_WARN("FResourceSystem::LoadPackage: failed to create '{}' in '{}'",
				Entry.Name, PackageName);
		}
	}

	MAHO_CORE_INFO("Loaded package '{}' ({} objects) from '{}'",
		PackageName, Pkg->Objects.size(), FilePath);
	LoadingFilePaths.erase(NormalizedFile);
	return PackageName;
}

EAssetLoadState FResourceSystem::GetLoadState(const std::string& AssetPath) const
{
	if (AssetPath.empty()) return EAssetLoadState::Invalid;

	const std::string Key = NormalizeResourceVirtualPath(AssetPath);
	auto Found = Find<FResource>(Key);
	if (Found) return Found->GetLoadState();

	return EAssetLoadState::Invalid;
}

void FResourceSystem::Flush(const std::string& AssetPath)
{
	if (!IsInitialized() || AssetPath.empty()) return;

	std::string Key = NormalizeResourceVirtualPath(AssetPath);
	const auto It = PendingIO.find(Key);
	if (It != PendingIO.end() && HasActiveServer())
		Server->Flush(It->second.Handle);

	while (PendingIO.find(Key) != PendingIO.end())
	{
		ProcessReadyIO();
		const auto Still = PendingIO.find(Key);
		if (Still == PendingIO.end()) break;
		if (Still->second.Handle.IsInProgress())
			Server->Flush(Still->second.Handle);
	}
}

void FResourceSystem::FlushAll()
{
	if (!IsInitialized()) return;

	for (const auto& Pair : PendingIO)
		Server->Flush(Pair.second.Handle);

	Server->FThreadedServer::Flush();
	while (!PendingIO.empty())
		ProcessReadyIO();
}

namespace Detail
{

FResourceSystem* GetResourceSystem()
{
	if (!GApp) return nullptr;
	return GApp->GetExtension<FResourceSystem>();
}

} // namespace Detail

} // namespace Maho
