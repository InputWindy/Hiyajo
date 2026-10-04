#pragma once

#include "PrimitiveRegistryApi.h"
#include <Engine/Frame.h>
#include <Maho.h>
#include <Name.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace Maho
{

class FPrimitiveRegistry;

/**
 * One mirrored primitive: what the GAME side says it wants drawn, already reduced to render-side
 * vocabulary.
 *
 * The object matrix is composed ON THE GAME SIDE because the transform's semantics (TRS order,
 * units, hierarchy) belong to the game -- the renderer consumes a matrix and nothing else. This is
 * the whole interface between the two sides; there is no shared mutable actor object anywhere.
 *
 * Fixed-size and POD on purpose: a snapshot is one contiguous copy, published whole.
 */
struct FRenderPrimitive
{
	Name::FName Mesh;       // asset name; the render side owns the GPU buffers behind a name
	Name::FName Material;   // reserved: the material path (batch 2.3) is not wired yet
	std::uint32_t ActorId = 0;
	float ObjectMatrix[16] = {};
};

/** Snapshot accessor -- same shape as `GetUIViewRegistry()` / `GetLog()`: a cross-DLL exported
 *  FUNCTION, never an exported variable. Null until the frame's Initialize ran. */
MAHO_PRIMITIVEREGISTRY_API FPrimitiveRegistry* GetPrimitiveRegistry();

/**
 * The mirror table: GAME side publishes, RENDER side reads.
 *
 * The two sides live in different collectors (FGameWorld's systems vs FRender's features), so their
 * relative order is NOT guaranteed. That is why this is a publish/read table and not a queue:
 *   - `Publish` REPLACES the snapshot with a new immutable one (a shared_ptr swap under the lock), so
 *     a reader sees either the previous complete snapshot or the new complete one -- never a
 *     half-written one, and no ordering edge between the two sides is needed;
 *   - `ReadLatest` hands back a shared_ptr, so the render side keeps reading a consistent snapshot
 *     even when it runs a frame behind the game side;
 *   - nothing is cleared on read, so a frame with no publication draws the last known scene instead
 *     of flickering to empty.
 *
 * It is a FRAME (only IInit/IShutdown) installed under the Render plugin, so its lifetime is in the
 * graph: the slots are cleared before anything unloads (see FUIViewRegistry for the long version of
 * why per-frame members beat file-level statics here).
 */
class MAHO_PRIMITIVEREGISTRY_API FPrimitiveRegistry
	: public FFrameExtension
	, public IPipeline<IInit, IShutdown>
{
	MAHO_DECLARE_FRAME(FPrimitiveRegistry);

public:
	FPrimitiveRegistry();
	~FPrimitiveRegistry() override;

	FPrimitiveRegistry(const FPrimitiveRegistry&) = delete;
	FPrimitiveRegistry& operator=(const FPrimitiveRegistry&) = delete;

	/** GAME side: publish this frame's primitives (replaces the previous snapshot). */
	void Publish(std::vector<FRenderPrimitive>&& Primitives);

	/** RENDER side: the latest complete snapshot (may be empty; never a partial one). */
	[[nodiscard]] std::shared_ptr<const std::vector<FRenderPrimitive>> ReadLatest() const;

	/** Diagnostics: how many snapshots have been published ("the game side is alive"). */
	[[nodiscard]] std::uint64_t GetPublishCount() const;

private:
	void Initialize(FEngineBase& Engine, FEngineContext& Frame) override;   // publish the accessor
	void Shutdown(FEngineBase& Engine, FEngineContext& Frame) override;     // drop the snapshot

	mutable std::mutex Mutex;
	std::shared_ptr<const std::vector<FRenderPrimitive>> Latest;
	std::uint64_t PublishCount = 0;
};

} // namespace Maho
