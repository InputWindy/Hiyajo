#include "Resource.h"

#include <Name.h>
#include <Paths.h>
#include <Trace.h>

#include <atomic>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <typeinfo>
#include <unordered_map>

namespace Maho::Resource
{

FResourceSystem* GResourceSystem = nullptr;

/** This role's trace lane: the row the IO thread's own work lands on. The same string
 *  GetThreadName() answers, kept as a literal because the task bodies below are lambdas and should
 *  not have to keep the system alive just to be named (the RHI's device names its lane the same way). */
constexpr const char* kResourceServerLane = "ResourceServer";

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

/** A queued async export: encoded+written on the IO thread, completion broadcast on the game thread. */
struct FPendingExport
{
	FTransferHandle Handle;
	Name::FName AssetName;
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

FOnTransferDone FResourceSystem::MakeTransferDone(std::string AssetPath)
{
	// On success the consuming handler signals it has taken the payload; drop the CPU
	// bulk data so the parsed resource no longer holds the decoded pixels. The catalog
	// entry stays (Find() still resolves the resource) - only its payload is freed.
	// ReleaseBulk runs under the catalog lock; the callback is invoked on the game thread.
	return [this, AssetPath = std::move(AssetPath)](bool bSuccess, std::string_view /*Error*/)
	{
		if (!bSuccess)
		{
			return;
		}
		std::lock_guard Lock(Impl->Mutex);
		auto It = Impl->Catalog.find(Name::FName(AssetPath));
		if (It != Impl->Catalog.end())
		{
			It->second->ReleaseBulk();
		}
	};
}

void FResourceSystem::Initialize(FEngineBase& Engine, FEngineContext& Frame)
{
	MAHO_TRACE_STAGE(IInit, "Resource init", "start the async resource IO thread");
	(void)Engine;
	FThreadedServer::Initialize();   // start the async load thread
	GResourceSystem = this;
}

const char* FResourceSystem::GetThreadName() const
{
	return "ResourceServer";
}

void FResourceSystem::Shutdown(FEngineBase&, FEngineContext&)
{
	MAHO_TRACE_STAGE(IShutdown, "Resource shutdown", "join the IO thread and report leftovers");
	GResourceSystem = nullptr;
	FThreadedServer::Shutdown();   // stop + join the IO thread
	{
		std::lock_guard Lock(Impl->Mutex);

		// Report leftovers BEFORE destroying them. Anything still here means its owner
		// did not release it in its own Shutdown -- and destroying it now runs code
		// (dtor / stored closure) from that owner's module, which may already be unloaded
		// (the validated crash is a sub-plugin unloaded before this Shutdown with its
		// transfers still here). Loud first, then release anyway (never dodge
		// cross-module destruction by not releasing).
		//
		// Asset names are printed as raw ids: FNamePool::Shutdown clears/retracts the
		// pool, and it is not ordered against this one, so ToString() here can read
		// freed pool storage. GetPath() is a plain data read through a NON-virtual
		// getter -- asking for typeid(*Resource) would dereference a vptr that is
		// exactly what dangles when the module unloaded first.
		for (const auto& [Asset, Pending] : Impl->PendingIO)
		{
			ReportError((std::string("ResourceSystem: leftover pending IMPORT (asset id ")
				+ std::to_string(Asset.GetId())
				+ ") -- the module that requested it must cancel it in its own Shutdown").c_str());
		}
		for (const auto& [Asset, Pending] : Impl->PendingExports)
		{
			ReportError((std::string("ResourceSystem: leftover pending EXPORT (asset id ")
				+ std::to_string(Asset.GetId())
				+ ") -- the module that requested it must cancel it in its own Shutdown").c_str());
		}
		for (const auto& [Asset, Resource] : Impl->Catalog)
		{
			ReportError((std::string("ResourceSystem: leftover resource (asset id ")
				+ std::to_string(Asset.GetId()) + ", path '" + std::string(Resource->GetPath())
				+ "') -- its creator must DestroyResource it in its own Shutdown").c_str());
		}

		Impl->PendingIO.clear();
		Impl->PendingExports.clear();
		Impl->Catalog.clear();
	}
}

void FResourceSystem::Tick(FEngineBase& Engine, FEngineContext& Frame)
{
	MAHO_TRACE_STAGE(ITick, "Resource tick", "apply the transfers the IO thread finished");
	(void)Engine;
	ProcessReadyIO();   // poll transfers + decode on the game thread
}

FTransferHandle FResourceSystem::RequestLoad(std::string Path)
{
	auto State = std::make_shared<FTransferState>();
	Submit([this, State, Path = std::move(Path)]() { LoadAssetBytes(State, std::move(Path)); });
	return FTransferHandle{ std::move(State) };
}

void FResourceSystem::LoadAssetBytes(std::shared_ptr<FTransferState> State, std::string Path)
{
	// The IO thread's own bar: its lane with itself as the row. A NAMED member rather than the
	// submitting lambda, because a scope bar takes its name from __FUNCTION__ and a timeline should
	// read "FResourceSystem::LoadAssetBytes", not "<lambda_1>::operator()".
	MAHO_TRACE_SCOPE(kResourceServerLane, "read the asset bytes off disk");
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
}

std::vector<std::uint8_t> FResourceSystem::ReadAssetFile(std::string_view SourcePath)
{
	const std::string PhysicalPath = Paths::GetPaths()->Resolve(SourcePath).string();
	std::ifstream Stream(PhysicalPath, std::ios::binary);
	if (!Stream)
	{
		return {};
	}
	return std::vector<std::uint8_t>(
		(std::istreambuf_iterator<char>(Stream)),
		std::istreambuf_iterator<char>());
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
	Name::FName AssetName)
{
	auto State = std::make_shared<FTransferState>();
	const std::string Dest = std::move(DestinationPath);
	Submit([this, State, Dest, Bytes = std::move(Bytes)]()
	{
		WriteAssetBytes(State, Dest, std::move(Bytes));
	});

	std::lock_guard Lock(Impl->Mutex);
	Impl->PendingExports[Name::FName(Dest)] = FPendingExport{
		FTransferHandle{ std::move(State) }, std::move(AssetName) };
	return true;
}

void FResourceSystem::WriteAssetBytes(std::shared_ptr<FTransferState> State, std::string Destination,
	std::vector<std::uint8_t> Bytes)
{
	// The IO thread's write leg -- a named member for the same reason as LoadAssetBytes above.
	MAHO_TRACE_SCOPE(kResourceServerLane, "write the asset bytes to disk");
	const bool bWritten = WriteBytes(Destination, Bytes);
	if (bWritten)
	{
		State->bSucceeded.store(true, std::memory_order_release);
	}
	else
	{
		State->bFailed.store(true, std::memory_order_release);
	}
}

const FResource* FResourceSystem::RegisterResource(std::string AssetPath, std::unique_ptr<FResource> Resource)
{
	const FResource* Raw = Resource.get();
	std::lock_guard Lock(Impl->Mutex);
	Impl->Catalog[Name::FName(AssetPath)] = std::move(Resource);
	return Raw;
}

bool FResourceSystem::DestroyResource(std::string_view AssetPath)
{
	const Name::FName AssetName(AssetPath);
	{
		std::lock_guard Lock(Impl->Mutex);
		const auto It = Impl->Catalog.find(AssetName);
		if (It == Impl->Catalog.end())
		{
			return false;
		}
		Impl->Catalog.erase(It);   // destroy the resource object
	}
	// Notify listeners (e.g. the render mirror) to release any GPU resource held for
	// this asset. The resource is already out of the catalog here, so MakeTransferDone's
	// ReleaseBulk finds no entry and no-ops - harmless.
	OnAssetUnloaded.Broadcast(AssetName, MakeTransferDone(std::string(AssetPath)));
	return true;
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
		OnAssetExported.Broadcast(Ready.AssetName, Ready.Handle.HasSucceeded());
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

FResource* FResourceSystem::FindMutable(std::string_view AssetPath)
{
	std::lock_guard Lock(Impl->Mutex);
	const auto It = Impl->Catalog.find(Name::FName(AssetPath));
	return It != Impl->Catalog.end() ? It->second.get() : nullptr;
}

const FResource* FResourceSystem::TryLoad(std::string_view AssetPath)
{
	return Find(AssetPath);
}

void FResourceSystem::ForEachResource(const std::function<void(const Name::FName&, const FResource&)>& Fn) const
{
	if (!Fn)
	{
		return;
	}
	std::lock_guard Lock(Impl->Mutex);
	for (const auto& [AssetName, Resource] : Impl->Catalog)
	{
		Fn(AssetName, *Resource);
	}
}

} // namespace Maho::Resource

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_RESOURCE_API Maho::FFrameExtension* CreateFrame()
{
	return Maho::Resource::FResourceSystem::CreateFrame();
}
