#include "PrimitiveRegistry.h"

#include <Log.h>

#include <utility>

namespace Maho
{

namespace
{
	/** The published accessor. Set in Initialize, cleared in Shutdown -- the registry is a frame, so
	 *  both moments are scheduled, not left to static destruction order. */
	FPrimitiveRegistry* GPrimitiveRegistry = nullptr;
}

FPrimitiveRegistry* GetPrimitiveRegistry()
{
	return GPrimitiveRegistry;
}

FPrimitiveRegistry::FPrimitiveRegistry() = default;
FPrimitiveRegistry::~FPrimitiveRegistry() = default;

void FPrimitiveRegistry::Initialize(FEngineBase&, FEngineContext&)
{
	GPrimitiveRegistry = this;
	MAHO_LOG_CORE_INFO("FPrimitiveRegistry: published (the render side can read the mirror table)");
}

void FPrimitiveRegistry::Shutdown(FEngineBase&, FEngineContext&)
{
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		Latest.reset();
		PublishCount = 0;
	}
	GPrimitiveRegistry = nullptr;
}

void FPrimitiveRegistry::Publish(std::vector<FRenderPrimitive>&& Primitives)
{
	// The snapshot is built OUTSIDE the lock: the copy of a frame's primitives is the expensive part,
	// and a reader only ever needs the pointer swap to be atomic w.r.t. other readers/writers.
	auto Snapshot = std::make_shared<const std::vector<FRenderPrimitive>>(std::move(Primitives));
	std::lock_guard<std::mutex> Lock(Mutex);
	Latest = std::move(Snapshot);
	++PublishCount;
}

std::shared_ptr<const std::vector<FRenderPrimitive>> FPrimitiveRegistry::ReadLatest() const
{
	std::lock_guard<std::mutex> Lock(Mutex);
	return Latest;
}

std::uint64_t FPrimitiveRegistry::GetPublishCount() const
{
	std::lock_guard<std::mutex> Lock(Mutex);
	return PublishCount;
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_PRIMITIVEREGISTRY_API Maho::FFrameExtension* CreateFrame()
{
	return Maho::FPrimitiveRegistry::CreateFrame();
}
