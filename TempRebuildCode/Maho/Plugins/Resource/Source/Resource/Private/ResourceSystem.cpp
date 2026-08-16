#include <ResourceSystem.h>

#include <Name.h>

#include <atomic>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Maho::Resource
{

namespace
{
	constexpr std::size_t kMaxAppliesPerTick = 1;
}

// ── internal implementation types (hidden from the header) ──

struct FBulkData
{
	std::vector<std::uint8_t> Bytes;
};

struct FTransferState
{
	std::atomic<bool> bSucceeded = false;
	std::atomic<bool> bFailed = false;
	std::mutex Mutex;
	FBulkData Bulk;
};

class FTransferHandle
{
public:
	FTransferHandle() = default;
	FTransferHandle(FTransferHandle&&) noexcept = default;
	FTransferHandle& operator=(FTransferHandle&&) noexcept = default;
	~FTransferHandle() = default;

	FTransferHandle(const FTransferHandle&) = delete;
	FTransferHandle& operator=(const FTransferHandle&) = delete;

	[[nodiscard]] bool IsValid() const { return State != nullptr; }
	[[nodiscard]] bool HasSucceeded() const { return State != nullptr && State->bSucceeded.load(std::memory_order_acquire); }
	[[nodiscard]] bool HasFailed() const { return State != nullptr && State->bFailed.load(std::memory_order_acquire); }

	std::shared_ptr<FTransferState> State;
};

struct FPendingIO
{
	FTransferHandle Handle;
	std::function<void(std::span<const std::uint8_t>)> OnBulkReady;
};

// ── FResourceSystem::FImpl ──

class FResourceSystem::FImpl
{
public:
	mutable std::mutex Mutex;
	std::unordered_map<Maho::Name::FName, FPendingIO> PendingIO;
	std::unordered_map<Maho::Name::FName, std::unique_ptr<FResource>> Catalog;
};

// ── FResourceSystem ──

FResourceSystem::FResourceSystem()
	: Impl(std::make_unique<FImpl>())
{
}

FResourceSystem::~FResourceSystem() = default;

bool FResourceSystem::ExecuteStage(EEngineStage Stage)
{
	switch (Stage)
	{
	case EEngineStage::Init:
		return Initialize();   // start the async load thread

	case EEngineStage::Tick:
		ProcessReadyIO();     // poll transfers + decode on the game thread
		return true;

	case EEngineStage::Shutdown:
		Shutdown();           // stop + join the thread
		{
			std::lock_guard Lock(Impl->Mutex);
			Impl->PendingIO.clear();
			Impl->Catalog.clear();
		}
		return true;

	default:
		return true;
	}
}

FTransferHandle FResourceSystem::RequestLoad(std::string Path)
{
	auto State = std::make_shared<FTransferState>();
	Submit([State, Path = std::move(Path)]()
	{
		FBulkData Bulk;
		std::ifstream Stream(Path, std::ios::binary);
		if (Stream)
		{
			Bulk.Bytes.assign(
				(std::istreambuf_iterator<char>(Stream)),
				std::istreambuf_iterator<char>());
		}
		{
			std::lock_guard Lock(State->Mutex);
			State->Bulk = std::move(Bulk);
		}
		if (!State->Bulk.Bytes.empty())
		{
			State->bSucceeded.store(true, std::memory_order_release);
		}
		else
		{
			State->bFailed.store(true, std::memory_order_release);
		}
	});
	return FTransferHandle{ std::move(State) };
}

bool FResourceSystem::EnqueueImport(
	std::string SourcePath,
	std::string AssetPath,
	std::function<void(std::span<const std::uint8_t>)> OnBulkReady)
{
	const std::string PhysicalPath = Maho::Paths::FPaths::Get().Resolve(SourcePath).string();
	FTransferHandle Handle = RequestLoad(PhysicalPath);

	std::lock_guard Lock(Impl->Mutex);
	Impl->PendingIO[Maho::Name::FName(AssetPath)] = FPendingIO{ std::move(Handle), std::move(OnBulkReady) };
	return true;
}

const FResource* FResourceSystem::RegisterResource(std::string AssetPath, std::unique_ptr<FResource> Resource)
{
	const FResource* Raw = Resource.get();
	std::lock_guard Lock(Impl->Mutex);
	Impl->Catalog[Maho::Name::FName(AssetPath)] = std::move(Resource);
	return Raw;
}

void FResourceSystem::ProcessReadyIO()
{
	std::size_t Applied = 0;
	for (auto It = Impl->PendingIO.begin(); It != Impl->PendingIO.end() && Applied < kMaxAppliesPerTick;)
	{
		FPendingIO& Pending = It->second;
		if (!Pending.Handle.HasSucceeded() && !Pending.Handle.HasFailed())
		{
			++It;
			continue;
		}

		FPendingIO Ready = std::move(Pending);
		It = Impl->PendingIO.erase(It);

		if (Ready.Handle.HasSucceeded())
		{
			std::lock_guard Lock(Ready.Handle.State->Mutex);
			Ready.OnBulkReady(Ready.Handle.State->Bulk.Bytes);   // decode, game thread
		}
		else
		{
			Ready.OnBulkReady({});   // failed — empty span
		}

		++Applied;
	}
}

bool FResourceSystem::WriteBytes(std::string_view PhysicalPath, std::span<const std::uint8_t> Bytes)
{
	std::ofstream Stream(std::string(PhysicalPath), std::ios::binary);
	if (!Stream)
	{
		return false;
	}
	Stream.write(
		reinterpret_cast<const char*>(Bytes.data()),
		static_cast<std::streamsize>(Bytes.size()));
	return static_cast<bool>(Stream);
}

const FResource* FResourceSystem::Find(std::string_view AssetPath) const
{
	std::lock_guard Lock(Impl->Mutex);
	const auto It = Impl->Catalog.find(Maho::Name::FName(AssetPath));
	return It != Impl->Catalog.end() ? It->second.get() : nullptr;
}

const FResource* FResourceSystem::TryLoad(std::string_view AssetPath)
{
	return Find(AssetPath);
}

const char* FResourceSystem::GetThreadName() const
{
	return "Resource";
}

} // namespace Maho::Resource

// ── Dynamic plugin entry (runtime load/unload via FPluginManager) ──

namespace
{

class FResourceSystemAdapter final : public Maho::IExtension<Maho::EEngineStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EEngineStage Stage) override
	{
		return Maho::Resource::FResourceSystem::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_RESOURCE_API Maho::IExtension<Maho::EEngineStage>* CreateExtension()
{
	return new FResourceSystemAdapter();
}
