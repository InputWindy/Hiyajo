#pragma once

#include "MeshFeatureApi.h"
#include <AssetTypes.h>
#include <Engine/Frame.h>
#include <Maho.h>
#include <Name.h>
#include <PrimitiveRegistry.h>
#include <Render.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Maho
{

/**
 * A code-built `FStaticMesh`. The geometry ctor is protected, and a project-side derived resource
 * type is the intended extension point (AGENTS.md's module-boundary rule: a derived type that
 * crosses a DLL boundary is exported from its own module -- hence the API tag here).
 *
 * It is now the PROVIDER for the built-in mesh name `Mesh.Cube` (see the provider table in
 * MeshFeature.cpp): TODO route it through the asset system (`.casset` importer) so a mesh NAME
 * resolves to a real imported asset instead of a procedural stand-in.
 */
class MAHO_MESHFEATURE_API FCubeMesh final : public Resource::FStaticMesh
{
public:
	FCubeMesh(std::string Path, std::vector<float> Positions, std::vector<float> Normals,
		std::vector<float> UVs, std::vector<std::uint32_t> Indices);
};

/**
 * Interleaved mesh vertex: position, normal, uv.
 *
 * Interleaving happens CPU-side because the RHI's vertex attributes carry no binding index (binding
 * 0 is implicit), so the asset's separate arrays cannot be bound as separate streams.
 */
struct FMeshVertex
{
	float X = 0.f, Y = 0.f, Z = 0.f;
	float NX = 0.f, NY = 0.f, NZ = 0.f;
	float U = 0.f, V = 0.f;
};

/** Sources of the mesh pass: a per-frame view-projection uniform buffer (set 0 / binding 0) plus the
 *  per-object matrix in the push block. */
struct FMeshShader
{
	static const char* GetVertexSource();
	static const char* GetFragmentSource();
	static const char* GetVertexEntryPoint();
	static const char* GetFragmentEntryPoint();
};

/**
 * The mesh pass: an ACTOR MANAGER fed by the mirror table -- deliberately DUMB about the game.
 *
 * It knows no entity, no component, no transform, no camera: each frame it reads
 *
 *   - `FPrimitiveRegistry::ReadLatest()`       -> what to draw (mesh name + object matrix + actor id)
 *   - `FPrimitiveRegistry::ReadLatestCamera()` -> the view-projection to draw it with
 *
 * and nothing else. Everything semantic (TRS composition, fov/near/far, which entity is the eye)
 * lives on the game side; this feature consumes 16 floats per object and one 4x4 for the frame.
 *
 * GPU geometry is cached BY MESH NAME (never per entity): two actors with the same mesh share one
 * vertex/index buffer pair, and the buffers are created lazily from the built-in provider table the
 * first time a name shows up (TODO: resolve names through the asset system instead).
 *
 * Rendering-wise nothing changed against the previous revision: same shader, same persistent VA/IB,
 * same per-frame Transient view-projection UBO, same per-object push-constant matrix, same depth
 * test+write -- only the SOURCE of the per-object matrix and of the camera moved to the game side.
 */
class MAHO_MESHFEATURE_API FMeshFeature : public FFrameExtension, public IPipeline<IInitViews, IRender, IPreUnInstall>
{
	MAHO_DECLARE_FRAME(FMeshFeature);

public:
	FMeshFeature();

	void InitViews(FRender& R, FRenderContext& Frame) override;
	void Render(FRender& R, FRenderContext& Frame) override;
	void PreUnInstall(FRender& R, FRenderContext& Frame) override;

private:
	/** GPU geometry for one mesh name (Persistent: it outlives the frame that created it). */
	struct FMeshBuffers
	{
		FRDGBufferRef Vertices;
		FRDGBufferRef Indices;
		std::uint32_t IndexCount = 0;
	};

	/** Get-or-create the geometry for a mesh name; nullptr when the name has no provider today. */
	const FMeshBuffers* EnsureMesh(FRender& R, Name::FName MeshName);

	/** Make the actor set match this frame's snapshot: actors appear/disappear with their ids. */
	void SyncActors(const std::vector<FRenderPrimitive>& Snapshot);

	/** Diagnostics: how many primitives / which camera channel this frame's read got, reported
	 *  ONCE per state change (a per-frame line is not a report -- the publish counter changes every
	 *  frame and would drown it). */
	void ReportRead(std::size_t PrimitiveCount, bool bCamera, std::uint64_t Publishes);

	/** mesh name -> GPU geometry (shared by every actor using that mesh). */
	std::map<Name::FName, FMeshBuffers> Meshes;

	/** The current actors, keyed by the game side's `ActorId`, holding the frame's object matrix. */
	std::map<std::uint32_t, FRenderPrimitive> Actors;

	/** The procedural provider geometry behind `Mesh.Cube` (see `FCubeMesh`). */
	std::unique_ptr<FCubeMesh> CubeMesh;

	/** Mesh names that had no provider, so each one is reported once instead of once per frame. */
	std::vector<Name::FName> UnresolvedMeshes;

	/** Frames seen without a camera -- a startup race must not be reported as "the game has no
	 *  camera", but a real one must be reported (exactly once). */
	std::uint32_t FramesWithoutCamera = 0;
	bool bReportedNoCamera = false;

	/** Diagnostics: frames rendered, and the signature of the last reported read. */
	std::uint32_t FramesSeen = 0;
	bool bReportedNoRegistry = false;
	std::size_t ReportedPrimitives = static_cast<std::size_t>(-1);
	bool bReportedCamera = false;
};

} // namespace Maho
