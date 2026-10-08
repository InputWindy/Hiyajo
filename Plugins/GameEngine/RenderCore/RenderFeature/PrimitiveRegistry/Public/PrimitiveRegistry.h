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
	Name::FName Material;   // the material's NAME, kept for diagnostics only (see `MaterialIndex`)
	std::uint32_t MaterialIndex = 0;   // index into the snapshot's material table (invalid index = default material)
	std::uint32_t ActorId = 0;
	float ObjectMatrix[16] = {};
};

/**
 * One MATERIAL PARAMETER value, mirrored to the render side.
 *
 * The name is the shader's own property name (`_BaseColor`) in its authoritative spelling -- the
 * case-insensitive lookup a gameplay API wants happens on the GAME side, so what travels here is
 * already a resolved name. Two value kinds cover every property the .shader syntax supports today:
 * a float4 (Color = all four lanes; Float/Range/Int use lane 0) and a texture asset name.
 */
struct FRenderMaterialParam
{
	Name::FName Name;
	float Value[4] = {};
	Name::FName Texture;    // texture properties only; None = "no override, use the shader's default"
};

/**
 * One entry of the frame's MATERIAL TABLE.
 *
 * A material is a sparse, SHARED thing: many actors point at the same entry through
 * `FRenderPrimitive::MaterialIndex`, and its parameters live ONCE in the snapshot's parameter table
 * (see `FirstParam`/`ParamCount`). That sharing is the whole reason the material table exists as a
 * separate table instead of each primitive carrying its own copy -- 1000 actors on one material copy
 * one color, and changing it changes every one of them (see design D5).
 */
struct FRenderMaterial
{
	Name::FName Shader;              // the .shader name (ShaderLibrary's key); the render side falls back when unknown
	Name::FName Name;                // the material's name for diagnostics/logs
	std::uint32_t FirstParam = 0;    // this material's parameters start here...
	std::uint32_t ParamCount = 0;    // ...and there are this many of them
};

/**
 * The frame's whole mirrored scene: primitives + the materials they reference + the parameter values.
 *
 * THREE TABLES, ONE SNAPSHOT. They must be published and read as one unit: a primitive's
 * `MaterialIndex` only means something against the material table of the SAME frame, so three
 * separate channels (or one lock per table) would let a reader pair frame N's primitives with frame
 * N-1's materials -- and draw with a material that is one frame stale, or, worse, out of range. One
 * shared_ptr swap makes the triple indivisible by construction.
 */
struct FRenderScene
{
	std::vector<FRenderPrimitive> Primitives;
	std::vector<FRenderMaterial> Materials;
	std::vector<FRenderMaterialParam> Params;
};

/** Snapshot accessor -- same shape as `GetUIViewRegistry()` / `GetLog()`: a cross-DLL exported
 *  FUNCTION, never an exported variable. Null until the frame's Initialize ran. */
MAHO_PRIMITIVEREGISTRY_API FPrimitiveRegistry* GetPrimitiveRegistry();

/**
 * The CAMERA channel beside the primitive channel: the game side's camera POSE + FRUSTUM, published
 * so the render side can compose the frame's view-projection itself.
 *
 * Same reduction as `FRenderPrimitive` (the game side owns what a camera IS: the transform it hangs
 * from, its fov/near/far), and the projection is deliberately NOT part of it -- the projection's
 * ASPECT belongs to whoever owns the DRAW TARGET, and that is the render side. The reason is not
 * taste: the OS window's shape and the target the frame is actually drawn into are different things
 * (an editor layout can draw the scene into a canvas whose aspect has nothing to do with the window),
 * and a projection built from the wrong one squashes the image on exactly one axis. So the mirror
 * carries the VIEW and the frustum numbers, and the aspect is resolved where the target is
 * (`FMeshFeature::Render`, from the scene color target's own desc).
 *
 * `Eye` is the composed world-space eye position, carried for diagnostics / future eye-dependent
 * passes (it is not needed to draw).
 *
 * An EMPTY `FRenderCamera` (all zeros -- what the game side publishes at teardown) is NOT drawable:
 * `FovY == 0` / `Far <= Near` cannot produce a projection, so the render side treats it as "no
 * camera" instead of dividing by `tan(0)` and writing NaN into the frame's UBO.
 *
 * One camera per frame on purpose: multi-view rendering is a later concern and a second channel is
 * cheaper to add than a wrongly-general one is to remove.
 */
struct FRenderCamera
{
	float View[16] = {};    // column-major view matrix (world -> camera); the inverse of the eye's T*R
	float FovY = 0.0f;      // vertical field of view in radians (the unit the projection math wants)
	float Near = 0.0f;      // near plane, world units
	float Far = 0.0f;       // far plane, world units
	float Eye[3] = {};      // world-space eye position
};

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
 * It is a FRAME (only IInit/IShutdown) installed by the **HOST** (the project entry layer, i.e. a
 * child in `PluginManager.json`'s root node), NOT by `FRender`: it mounts ENGINE stages, and the
 * rule is "install into the collector whose stages you mount" -- a frame installed into a collector
 * whose stage set it does not mount gets NO node at all, is never driven, and its Initialize never
 * runs (that is exactly how this table stayed inaccessible). Its lifetime is still in the graph:
 * the slots are cleared before anything unloads (see FUIViewRegistry for the long version of why
 * per-frame members beat file-level statics here).
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

	/** GAME side: publish this frame's scene (primitives + materials + parameters, ONE swap). */
	void Publish(FRenderScene&& Scene);

	/** RENDER side: the latest complete snapshot (may be empty; never a partial one). The three
	 *  tables inside it always belong to the same frame -- that is the point of publishing them
	 *  together (see FRenderScene). Reading is one lock + one shared_ptr copy, NOT three. */
	[[nodiscard]] std::shared_ptr<const FRenderScene> ReadLatest() const;

	/** Diagnostics: how many snapshots have been published ("the game side is alive"). */
	[[nodiscard]] std::uint64_t GetPublishCount() const;

	/** GAME side: publish this frame's camera pose + frustum (REPLACES the previous one -- a camera is
	 *  not a list). The PROJECTION is not published: its aspect belongs to the draw target's owner
	 *  (the render side), see FRenderCamera. */
	void PublishCamera(FRenderCamera Camera);

	/** RENDER side: the latest camera, or null when none was ever published. A null camera is
	 *  "nothing to draw with", NOT "keep the previous one": a frame with a camera and a frame without
	 *  one must be distinguishable (the render side reports and skips). */
	[[nodiscard]] std::shared_ptr<const FRenderCamera> ReadLatestCamera() const;

private:
	void Initialize(FEngineBase& Engine, FEngineContext& Frame) override;   // publish the accessor
	void Shutdown(FEngineBase& Engine, FEngineContext& Frame) override;     // drop the snapshots

	mutable std::mutex Mutex;
	std::shared_ptr<const FRenderScene> Latest;
	std::shared_ptr<const FRenderCamera> LatestCamera;
	std::uint64_t PublishCount = 0;
};

} // namespace Maho
