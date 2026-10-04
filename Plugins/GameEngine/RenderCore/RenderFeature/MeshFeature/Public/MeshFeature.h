#pragma once

#include "MeshFeatureApi.h"
#include <Engine/Frame.h>
#include <Maho.h>
#include <Render.h>

namespace Maho
{

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
 * Batch 2.1 -- the first real mesh draw path.
 *
 * What is NEW here, against the demo triangle (`FDrawTriangleFeature`) and the UI passes:
 *   - a VERTEX BUFFER + INDEX BUFFER (Persistent pool buffers, uploaded once), instead of in-shader
 *     generated geometry or a per-frame merged ImGui buffer;
 *   - a CAMERA: a per-frame Transient uniform buffer holding the view-projection matrix, bound
 *     through a `UniformBuffer` descriptor (the only descriptor type the RHI actually writes today);
 *   - a PER-OBJECT transform: the object matrix travels in the push-constant block, packed from the
 *     same `ShaderParameterStruct` that declares the pipeline layout -- so no descriptor set churn
 *     per object;
 *   - DEPTH WRITE on (the demo triangle leaves it off): required for anything to occlude anything.
 *
 * The geometry is procedural for now: the asset path (`FStaticMesh` -> GPU) is batch 2.2, materials
 * are 2.3, per-instance data 2.4.
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
	/** Build the cube's vertex/index buffers once (idempotent; Persistent so they survive frames). */
	void EnsureCubeBuffers(FRender& R);

	FRDGBufferRef CubeVertices;
	FRDGBufferRef CubeIndices;
	std::uint32_t CubeIndexCount = 0;

	/** Procedural spin angle, advanced by the world's delta each frame. */
	float SpinAngle = 0.f;
};

} // namespace Maho
