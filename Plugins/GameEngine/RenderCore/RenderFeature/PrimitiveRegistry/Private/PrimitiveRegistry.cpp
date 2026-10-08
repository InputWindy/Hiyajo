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

FPrimitiveRegistry::FPrimitiveRegistry()
{
	// The two frames are installed by the SAME batch (both are root children of the host, see
	// PluginManager.json), so their IInit nodes are dispatched concurrently and nothing orders them.
	// `MAHO_LOG_CORE_INFO` below calls `GetLog()`, which FLog publishes in ITS IInit -- hence the edge:
	// "my IInit waits for FLog@IInit, this frame". Without it the log call inside a Debug build trips
	// `MAHO_ENSURE_NOT_NULL` (a hard break) and in Release it silently prints nothing.
	// Any layer that logs during IInit declares this same edge; the direction is declared by the
	// consumer, never by FLog.
	MyStage<IInit>().IsWaiting<FLog>().ForStage<IInit>();
}

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
		LatestCamera.reset();
		PublishCount = 0;
	}
	GPrimitiveRegistry = nullptr;
}

void FPrimitiveRegistry::Publish(FRenderScene&& Scene)
{
	// The snapshot is built OUTSIDE the lock: the copy of a frame's three tables is the expensive part,
	// and a reader only ever needs the pointer swap to be atomic w.r.t. other readers/writers. ONE
	// make_shared for the whole scene is what makes primitives + materials + params indivisible.
	auto Snapshot = std::make_shared<const FRenderScene>(std::move(Scene));
	std::lock_guard<std::mutex> Lock(Mutex);
	Latest = std::move(Snapshot);
	++PublishCount;
}

std::shared_ptr<const FRenderScene> FPrimitiveRegistry::ReadLatest() const
{
	std::lock_guard<std::mutex> Lock(Mutex);
	return Latest;
}

std::uint64_t FPrimitiveRegistry::GetPublishCount() const
{
	std::lock_guard<std::mutex> Lock(Mutex);
	return PublishCount;
}

void FPrimitiveRegistry::PublishCamera(FRenderCamera Camera)
{
	// Same construction as Publish(): build the immutable value off the lock, then swap the pointer.
	// The payload is still "whatever the game side reduced its camera to" -- today a view matrix plus
	// the frustum numbers (NOT a view-projection: the aspect belongs to the draw target's owner, see
	// FRenderCamera in the header). The publish/read semantics are unchanged: replace, one pointer
	// swap, readers keep a consistent snapshot.
	// No publish counter here -- the camera rides along with the primitive channel, and a second
	// counter would only invite "which one is the frame counter?" questions.
	std::lock_guard<std::mutex> Lock(Mutex);
	LatestCamera = std::make_shared<const FRenderCamera>(Camera);
}

std::shared_ptr<const FRenderCamera> FPrimitiveRegistry::ReadLatestCamera() const
{
	std::lock_guard<std::mutex> Lock(Mutex);
	return LatestCamera;
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_PRIMITIVEREGISTRY_API Maho::FFrameExtension* CreateFrame()
{
	return Maho::FPrimitiveRegistry::CreateFrame();
}
