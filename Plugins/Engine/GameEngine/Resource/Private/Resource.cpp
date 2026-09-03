#include "Resource.h"

#include <Name.h>
#include <Paths.h>

#include <atomic>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Maho::Resource
{

FResourceSystem* GResourceSystem = nullptr;

MAHO_RESOURCE_API FResourceSystem* GetResourceSystem()
{
	return GResourceSystem;
}

namespace
{
	constexpr std::size_t kMaxAppliesPerTick = 1;
}

std::size_t detail::FindLastDot(std::string_view Path)
{
	return Path.find_last_of('.');
}

// -- internal implementation types (hidden from the header) --

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
	explicit FTransferHandle(std::shared_ptr<FTransferState> InState) : State(std::move(InState)) {}
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

struct FPendingImport
{
	FTransferHandle Handle;
	std::function<void(std::span<const std::uint8_t>)> OnBulkReady;
};

/** A queued async export: encoded+written on the IO thread, OnDone on the game thread. */
struct FPendingExport
{
	FTransferHandle Handle;
	std::function<void(bool)> OnDone;
};

class FResourceSystem::FImpl
{
public:
	mutable std::mutex Mutex;
	std::unordered_map<Name::FName, FPendingImport> PendingIO;
	std::unordered_map<Name::FName, FPendingExport> PendingExports;
	std::unordered_map<Name::FName, std::unique_ptr<FResource>> Catalog;
};

// The dtor lives here (FImpl is complete) so the header's unique_ptr<FImpl>
// can delete it.
FResourceSystem::~FResourceSystem() = default;

// -- FResourceSystem --

FResourceSystem::FResourceSystem()
	: Impl(std::make_unique<FImpl>())
{
}

void FResourceSystem::Initialize(FEngineBase& Engine)
{
	(void)Engine;
	FThreadedServer::Initialize();   // start the async load thread
	GResourceSystem = this;
}

void FResourceSystem::Shutdown(FEngineBase&)
{
	GResourceSystem = nullptr;
	FThreadedServer::Shutdown();   // stop + join the IO thread
	{
		std::lock_guard Lock(Impl->Mutex);
		Impl->PendingIO.clear();
		Impl->PendingExports.clear();
		Impl->Catalog.clear();
	}
}

void FResourceSystem::Tick(FEngineBase& Engine)
{
	(void)Engine;
	ProcessReadyIO();   // poll transfers + decode on the game thread
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
	const std::string PhysicalPath = Paths::GetPaths()->Resolve(SourcePath).string();
	FTransferHandle Handle = RequestLoad(PhysicalPath);

	std::lock_guard Lock(Impl->Mutex);
	Impl->PendingIO[Name::FName(AssetPath)] = FPendingImport{ std::move(Handle), std::move(OnBulkReady) };
	return true;
}

bool FResourceSystem::EnqueueExport(
	std::vector<std::uint8_t> Bytes,
	std::string DestinationPath,
	std::function<void(bool)> OnDone)
{
	auto State = std::make_shared<FTransferState>();
	const std::string Dest = std::move(DestinationPath);
	Submit([State, Dest, Bytes = std::move(Bytes)]()
	{
		const bool bWritten = WriteBytes(Dest, Bytes);
		if (bWritten)
		{
			State->bSucceeded.store(true, std::memory_order_release);
		}
		else
		{
			State->bFailed.store(true, std::memory_order_release);
		}
	});

	std::lock_guard Lock(Impl->Mutex);
	Impl->PendingExports[Name::FName(Dest)] = FPendingExport{
		FTransferHandle{ std::move(State) }, std::move(OnDone) };
	return true;
}

const FResource* FResourceSystem::RegisterResource(std::string AssetPath, std::unique_ptr<FResource> Resource)
{
	const FResource* Raw = Resource.get();
	std::lock_guard Lock(Impl->Mutex);
	Impl->Catalog[Name::FName(AssetPath)] = std::move(Resource);
	return Raw;
}

const FResource* FResourceSystem::RegisterChildResource(std::string AssetPath, std::unique_ptr<FResource> Resource)
{
	return RegisterResource(std::move(AssetPath), std::move(Resource));
}

void FResourceSystem::ProcessReadyIO()
{
	std::vector<FPendingImport> ReadyImports;
	std::vector<FPendingExport> ReadyExports;
	{
		// Hold the impl lock only to SPLIT OUT the completed transfers; producers
		// (EnqueueImport/Export on the IO thread) write PendingIO/PendingExports
		// concurrently, so the iteration/erase needs the lock. The decode callbacks
		// run BELOW, outside it.
		std::lock_guard Lock(Impl->Mutex);

		std::size_t Applied = 0;
		for (auto It = Impl->PendingIO.begin(); It != Impl->PendingIO.end() && Applied < kMaxAppliesPerTick;)
		{
			FPendingImport& Pending = It->second;
			if (!Pending.Handle.HasSucceeded() && !Pending.Handle.HasFailed())
			{
				++It;
				continue;
			}

			FPendingImport Ready = std::move(Pending);
			It = Impl->PendingIO.erase(It);
			ReadyImports.push_back(std::move(Ready));
			++Applied;
		}

		for (auto It = Impl->PendingExports.begin(); It != Impl->PendingExports.end();)
		{
			FPendingExport& Pending = It->second;
			if (!Pending.Handle.HasSucceeded() && !Pending.Handle.HasFailed())
			{
				++It;
				continue;
			}
			FPendingExport Ready = std::move(Pending);
			It = Impl->PendingExports.erase(It);
			ReadyExports.push_back(std::move(Ready));
		}
	}

	// Apply OUTSIDE the impl lock. Importers may register the resource (and, for a
	// prefab split, several child resources) via RegisterResource /
	// RegisterChildResource, which lock Impl->Mutex themselves — invoking them under
	// the impl lock would deadlock the non-recursive mutex.
	for (FPendingImport& Ready : ReadyImports)
	{
		if (Ready.Handle.HasSucceeded())
		{
			std::lock_guard Lock(Ready.Handle.State->Mutex);
			Ready.OnBulkReady(Ready.Handle.State->Bulk.Bytes);   // decode, game thread
		}
		else
		{
			Ready.OnBulkReady({});   // failed - empty span
		}
	}

	for (FPendingExport& Ready : ReadyExports)
	{
		if (Ready.OnDone)
		{
			Ready.OnDone(Ready.Handle.HasSucceeded());
		}
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
	const auto It = Impl->Catalog.find(Name::FName(AssetPath));
	return It != Impl->Catalog.end() ? It->second.get() : nullptr;
}

const FResource* FResourceSystem::TryLoad(std::string_view AssetPath)
{
	return Find(AssetPath);
}

} // namespace Maho::Resource

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_RESOURCE_API Maho::FLayerBase* CreateLayer()
{
	return Maho::Resource::FResourceSystem::CreateLayer();
}
