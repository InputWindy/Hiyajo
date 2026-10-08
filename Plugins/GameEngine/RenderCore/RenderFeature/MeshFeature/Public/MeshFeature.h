#pragma once

#include "MeshFeatureApi.h"
#include <AssetTypes.h>
#include <Engine/Frame.h>
#include <Maho.h>
#include <Name.h>
#include <PrimitiveRegistry.h>
#include <Render.h>
#include <ShaderLibrary.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
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

/** Sources of the FALLBACK mesh pass: the built-in GLSL used only when the ShaderLibrary is not
 *  available in this build/run (see `DrawWithBuiltinShader`). The normal path takes its bytecode,
 *  material layout and render state from `Content/Shaders/<名>.shader` through the ShaderLibrary --
 *  these two strings are the "no .shader needed at all" floor, not a second pipeline. */
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
 * It knows no entity, no component, no transform, no projection: each frame it reads
 *
 *   - `FPrimitiveRegistry::ReadLatest()`       -> what to draw (mesh + object matrix + actor id
 *                                                 + the MATERIAL its material index points at)
 *   - `FPrimitiveRegistry::ReadLatestCamera()` -> the VIEW matrix and the frustum (fov/near/far)
 *
 * and nothing else. Everything semantic (TRS composition, fov/near/far, which entity is the eye,
 * what a material's parameters MEAN) lives on the game side; this feature consumes 16 floats per
 * object, one 4x4 for the frame, and a material table it looks up in the ShaderLibrary by name.
 *
 * THE PROJECTION IS COMPOSED HERE, and that is on purpose: its ASPECT is a property of the draw
 * TARGET, which this side owns (the game side knows the OS window, not the canvas the frame lands
 * in -- see `Render`, and `FRenderCamera`'s note). The frame's UBO still receives one view-projection,
 * so nothing downstream of this class knows the difference.
 *
 * GPU geometry is cached BY MESH NAME (never per entity): two actors with the same mesh share one
 * vertex/index buffer pair, and the buffers are created lazily from the built-in provider table the
 * first time a name shows up (TODO: resolve names through the asset system instead).
 *
 * RENDERING-wise this is where the shader stops being a C++ type: a material's `Shader` name is
 * resolved against the ShaderLibrary (bytecode + std140 material layout + render state), the layout
 * drives a per-material Transient parameter buffer bound at set2 binding0, its texture properties are
 * bound as set2 samplers 1..N, and the pass state goes into the pipeline. (shader, pass state) is
 * cached, so parameters changing never rebuilds a PSO (design D7 / tasks 6.1-6.5).
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
	/** How many material SAMPLER slots the parameter struct declares. The .shader syntax has no such
	 *  limit by design ("按声明序分配"), but a fixed-capacity set is what the compile-time parameter
	 *  macros can express -- so the slots exist, and the ones a shader does not use are bound to the
	 *  white fallback texture. A shader declaring more than this is REPORTED and clamped (not silently
	 *  half-bound). */
	static constexpr std::uint32_t kMaxMaterialSamplers = 4;

	/** GPU geometry for one mesh name (Persistent: it outlives the frame that created it). */
	struct FMeshBuffers
	{
		FRDGBufferRef Vertices;
		FRDGBufferRef Indices;
		std::uint32_t IndexCount = 0;
	};

	/** Identity of a graphics pipeline on the render side: everything `FRHIGraphicsPipelineDesc`
	 *  carries that can vary per material/actor. Parameters are NOT in here -- a parameter change must
	 *  never rebuild a PSO (D7), which is exactly why the material block lives in a descriptor set. */
	struct FPipelineKey
	{
		std::uint32_t ShaderId = 0;
		std::uint64_t ShaderHash = 0;     // SPIR-V content (two shaders with the same name can differ)
		ERHICullMode Cull = ERHICullMode::Back;
		bool bDepthWrite = false;
		bool bDepthTest = false;
		bool bBlend = false;
		ERHIFormat ColorFormat = ERHIFormat::Unknown;
		ERHIFormat DepthFormat = ERHIFormat::Unknown;

		[[nodiscard]] auto AsTuple() const
		{
			return std::tie(ShaderId, ShaderHash, Cull, bDepthWrite, bDepthTest, bBlend, ColorFormat, DepthFormat);
		}
		[[nodiscard]] bool operator<(const FPipelineKey& O) const { return AsTuple() < O.AsTuple(); }
	};

	/** One resolved SHADER: the modules + the pipeline description built from it. Built once per
	 *  (shader, pass state, target formats) and reused -- the pipeline itself is created by the pool's
	 *  PSO cache from this description (bytecode-keyed), so a hit here is also a PSO hit. */
	struct FShaderEntry
	{
		const FShaderDesc* Description = nullptr;   // ShaderLibrary-owned; valid for the whole run
		FRHIShaderModule* Vertex = nullptr;
		FRHIShaderModule* Fragment = nullptr;
		std::string VertexEntry = "main";
		std::string FragmentEntry = "main";
		FRHIGraphicsPipelineDesc Pipeline;
	};

	/** One material, resolved for THIS frame into bindable things: a parameter buffer packed from the
	 *  shader's layout (+ the game side's overrides) and one texture/sampler pair per sampler slot. */
	struct FMaterialFrame
	{
		const FShaderEntry* Shader = nullptr;   // null => could not be resolved (reported)
		FRDGBufferRef ParamBuffer;
		std::vector<std::byte> Packed;          // the CPU bytes the buffer is filled with (per frame)
		FRDGTextureRef Textures[kMaxMaterialSamplers];
		FRHISampler* Samplers[kMaxMaterialSamplers] = {};
	};

	/** Get-or-create the geometry for a mesh name; nullptr when the name has no provider today. */
	const FMeshBuffers* EnsureMesh(FRender& R, Name::FName MeshName);

	/** Make the actor set match this frame's snapshot: actors appear/disappear with their ids. */
	void SyncActors(const std::vector<FRenderPrimitive>& Snapshot);

	/** Diagnostics: how many primitives / which camera channel this frame's read got, reported
	 *  ONCE per state change (a per-frame line is not a report -- the publish counter changes every
	 *  frame and would drown it). */
	void ReportRead(std::size_t PrimitiveCount, bool bCamera, std::uint64_t Publishes);

	// -- the ShaderLibrary path (tasks 6.1-6.5) ------------------------------------------------

	/** Resolve `ShaderName` to a cached pipeline entry: ShaderLibrary lookup (with the built-in
	 *  fallback + a REPORT when the name is unknown), SPIR-V -> shader modules, and the pipeline
	 *  description with the pass' render state applied. Returns nullptr when even the fallback is
	 *  unusable (reported) -- the caller skips the actor rather than drawing something undefined. */
	const FShaderEntry* ResolveShader(FRender& R, Name::FName ShaderName, ERHIFormat ColorFormat,
		ERHIFormat DepthFormat);

	/** Pack one material's parameters into `Out` according to the shader's `FMaterialLayout`:
	 *  overrides first, the .shader's own default for everything the game side did not set (an
	 *  unset property is "the shader's default", never zero). */
	void PackMaterialParameters(const FShaderDesc& Desc, const FRenderMaterial& Material,
		const std::vector<FRenderMaterialParam>& Params, std::vector<std::byte>& Out) const;

	/** Resolve one material for this frame: parameter buffer + per-slot textures/samplers.
	 *  Missing textures are REPORTED (once per material+property) and replaced by the white fallback,
	 *  so a bad texture name costs one log line, not a black screen. */
	void ResolveMaterial(FRender& R, const FRenderScene& Scene, std::size_t MaterialIndex,
		ERHIFormat ColorFormat, ERHIFormat DepthFormat, FMaterialFrame& Out);

	/** Lazily create the 1x1 white texture (+ its default sampler) used whenever a material's texture
	 *  cannot be resolved. Persistent: it is bound in passes of many frames. */
	bool EnsureWhiteTexture(FRender& R);

	/** The per-frame uniform block, matching the injected `ViewConstants` block (set0 / binding0). */
	struct FViewConstants
	{
		float ViewProj[16];
	};

	/** The ShaderLibrary path: one pass per actor, pipeline + material set2 chosen per material
	 *  (tasks 6.1-6.4). Color/depth formats are read off `Target`. */
	void DrawWithMaterials(FRender& R, const FRenderScene& Scene, const FRenderTarget& Target,
		std::uint32_t TargetW, std::uint32_t TargetH, const FViewConstants& ViewConstants,
		const FRDGBufferRef& ViewBuffer);

	/** The `GetShaderLibrary() == nullptr` path: the built-in GLSL pair, one pass per actor, no
	 *  material binding at all (task 6.5 -- "内置 GLSL 退役为 fallback"). */
	void DrawWithBuiltinShader(FRender& R, const FRenderScene& Scene, const FRenderTarget& Target,
		std::uint32_t TargetW, std::uint32_t TargetH, const FViewConstants& ViewConstants,
		const FRDGBufferRef& ViewBuffer);

	/** mesh name -> GPU geometry (shared by every actor using that mesh). */
	std::map<Name::FName, FMeshBuffers> Meshes;

	/** The current actors, keyed by the game side's `ActorId`, holding the frame's object matrix and
	 *  the material index into the frame's material table. */
	std::map<std::uint32_t, FRenderPrimitive> Actors;

	/** (shader, pass state, target formats) -> resolved modules + pipeline description. */
	std::map<FPipelineKey, FShaderEntry> Pipelines;

	/** Shader names whose pipeline could not be resolved: reported once, not per frame/actor. */
	std::vector<Name::FName> UnresolvedShaders;

	/** (material, property) pairs whose texture was missing: reported once each, not per frame. */
	std::vector<std::string> ReportedMissingTextures;

	/** The procedural provider geometry behind `Mesh.Cube` (see `FCubeMesh`). */
	std::unique_ptr<FCubeMesh> CubeMesh;

	/** Mesh names that had no provider, so each one is reported once instead of once per frame. */
	std::vector<Name::FName> UnresolvedMeshes;

	/** The white fallback texture + the sampler bound with it (Persistent; see EnsureWhiteTexture). */
	FRDGTextureRef WhiteTexture;
	FRDGBufferRef WhiteTextureStaging;
	FRHISampler* WhiteSampler = nullptr;
	bool bWhiteTextureFailed = false;

	/**
	 * Camera-constant buffers, ONE PER FRAME-IN-FLIGHT SLOT (Persistent, lazily created).
	 *
	 * Deliberately not a frame-scoped transient: the CPU runs ahead of the GPU by up to
	 * `CameraRingSlots` (= K) frames, while the pool recycles a transient at the NEXT BeginFrame -- so a
	 * frame could write the bytes a previous frame was still reading, and the mesh would draw with the
	 * following frame's matrices. That is invisible while the camera is static (identical bytes are
	 * written again) and shows up as a live flicker the moment it moves every frame. A slot owned by one
	 * frame removes the race by construction.
	 */
	std::vector<FRDGBufferRef> CameraConstantBuffers;

	/**
	 * SLOTS IN THE CAMERA-CONSTANT RING -- the pipeline depth K, a RUN-TIME value.
	 *
	 * It is NOT a compile-time constant any more: the depth is injected by the host
	 * (`r.FramesInFlight`, sealed into the frame graph before the first graph exists) and handed down
	 * to every K-deep ring, so this feature ASKS the owning render layer for it at the FIRST `Render`
	 * (`FRender::GetFrameSlotCount`, which reports the device's slot count / the process-wide depth).
	 * Sizing the ring from a compiled `MAHO_FRAMES_IN_FLIGHT` was the one place left where "the
	 * declared depth" and "the enforced depth" could disagree the moment a host configured a
	 * different K.
	 *
	 * 0 until the first frame: the ring (`CameraConstantBuffers`) is only resized once this is known,
	 * so a zero here is "not read yet", never "a zero-slot ring".
	 */
	std::uint32_t CameraRingSlots = 0;

	/** Frames seen without a camera -- a startup race must not be reported as "the game has no
	 *  camera", but a real one must be reported (exactly once). */
	std::uint32_t FramesWithoutCamera = 0;
	bool bReportedNoCamera = false;

	/** One-shot report flag for a degenerate scene-target size (the aspect division is this side's
	 *  now, so its failure mode must leave a trace instead of a silent sqrt of a NaN). */
	bool bReportedDegenerateTarget = false;

	/** Diagnostics: frames rendered, and the signature of the last reported read. */
	std::uint32_t FramesSeen = 0;
	bool bReportedNoRegistry = false;
	std::size_t ReportedPrimitives = static_cast<std::size_t>(-1);
	bool bReportedCamera = false;
};

} // namespace Maho
