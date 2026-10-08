#include "MeshFeature.h"

#include <Core/FrameGraph.h>   // MAHO_FRAMES_IN_FLIGHT: the camera-constant ring's slot count

#include <Log.h>
#include <Scene.h>
#include <ShaderParameterStruct.h>
#include <Trace.h>

// The projection is composed HERE (see Render): the aspect belongs to the draw target, and this
// feature is the owner of it. glm rides in PUBLIC through the engine core (see the Maho target in
// Tools/maho_tools.py), so this is not a build change.
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Maho
{

namespace
{
	// -------------------------------------------------------------------------------------------
	// FALLBACK SOURCES (task 6.5). The normal path does not use these: a material names a
	// `Content/Shaders/<名>.shader` and the ShaderLibrary supplies bytecode, layout and render state.
	// These two strings are what runs when there is NO ShaderLibrary at all (a build/run without the
	// plugin) -- a floor to fall to, not a second pipeline to maintain.
	// -------------------------------------------------------------------------------------------
	constexpr const char* kVertexShader = R"(
#version 460
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

layout(set = 0, binding = 0) uniform ViewConstants { mat4 ViewProj; } VC;
layout(push_constant) uniform Push { mat4 Object; } PC;

layout(location = 0) out vec3 FragNormal;

void main()
{
	vec4 World = PC.Object * vec4(aPos, 1.0);
	gl_Position = VC.ViewProj * World;
	FragNormal = mat3(PC.Object) * aNormal;
}
)";

	constexpr const char* kFragmentShader = R"(
#version 460
layout(location = 0) in vec3 FragNormal;
layout(location = 0) out vec4 OutColor;

void main()
{
	vec3 N = normalize(FragNormal);
	vec3 L = normalize(vec3(0.45, 0.75, -0.5));
	float Diffuse = max(dot(N, L), 0.0);
	OutColor = vec4(vec3(0.30, 0.58, 0.95) * (0.25 + 0.75 * Diffuse), 1.0);
}
)";

	/**
	 * The pass' binding table, i.e. the C++ side of the injected GLSL blocks:
	 *
	 *   set0 b0  `ViewConstants`  -- the frame's view-projection (this feature's one per-frame UBO)
	 *   set1 b0  (placeholder)    -- see below
	 *   set2 b0  `MahoMaterial`   -- the material parameter block, packed by `FMaterialLayout`
	 *   set2 b1..N                -- the material's texture properties, in declaration order
	 *   push     `_Push.ObjectMatrix`
	 *
	 * SET1 IS A PLACEHOLDER ON PURPOSE, and it must not simply be omitted: a Vulkan pipeline layout is
	 * POSITIONAL (`pSetLayouts[i]` IS set `i`), and `FRender::AddPass` builds it from the sets this
	 * struct declares. Skipping set1 would put the material layout at index 1 while the shader reads
	 * it as set 2. So set1 exists, with one real binding (the same view UBO) nobody reads -- the
	 * cheapest legal content for the reserved slot D3 left for a future per-object UBO.
	 *
	 * The four sampler slots are fixed CAPACITY, not "the shader has four textures": see
	 * kMaxMaterialSamplers.
	 */
	BEGIN_SHADER_PARAMETER_STRUCT(FMeshParameters)
		SHADER_PARAMETER_BUFFER(UniformBuffer, ViewConstants, 0, 0, ERHIShaderStage::Vertex,
			ERHIDescriptorType::UniformBuffer)
		SHADER_PARAMETER_BUFFER(UniformBuffer, Set1Placeholder, 1, 0, ERHIShaderStage::Vertex,
			ERHIDescriptorType::UniformBuffer)
		SHADER_PARAMETER_BUFFER(UniformBuffer, MaterialParams, 2, 0, ERHIShaderStage::AllGraphics,
			ERHIDescriptorType::UniformBuffer)
		SHADER_PARAMETER_TEXTURE_SAMPLER(Texture2D, MaterialTexture0, MaterialSampler0, 2, 1, ERHIShaderStage::Fragment)
		SHADER_PARAMETER_TEXTURE_SAMPLER(Texture2D, MaterialTexture1, MaterialSampler1, 2, 2, ERHIShaderStage::Fragment)
		SHADER_PARAMETER_TEXTURE_SAMPLER(Texture2D, MaterialTexture2, MaterialSampler2, 2, 3, ERHIShaderStage::Fragment)
		SHADER_PARAMETER_TEXTURE_SAMPLER(Texture2D, MaterialTexture3, MaterialSampler3, 2, 4, ERHIShaderStage::Fragment)
		SHADER_PARAMETER_ARRAY(float, ObjectMatrix, 16)
	END_SHADER_PARAMETER_STRUCT()

	/**
	 * A unit cube as SoA arrays matching `FStaticMesh`'s payload: 24 positions/normals (4 per face, so
	 * each face carries its own normal -- flat shading without a geometry stage), 48 UV floats and 36
	 * indices. Procedural on purpose: this stands in for the `.casset` importer, and the feature
	 * interleaves it exactly as it would interleave imported data.
	 */
	void BuildCubeSoA(std::vector<float>& OutPositions, std::vector<float>& OutNormals,
		std::vector<float>& OutUVs, std::vector<std::uint32_t>& OutIndices)
	{
		struct FFace
		{
			float NX, NY, NZ;   // outward normal
			float TX, TY, TZ;   // tangent
			float BX, BY, BZ;   // bitangent (tangent x bitangent == normal)
		};

		const float H = 0.5f;
		const FFace Faces[6] = {
			{ 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f },      // +Z
			{ 0.f, 0.f, -1.f, -1.f, 0.f, 0.f, 0.f, 1.f, 0.f },    // -Z
			{ 1.f, 0.f, 0.f, 0.f, 0.f, -1.f, 0.f, 1.f, 0.f },     // +X
			{ -1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f },     // -X
			{ 0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, -1.f },     // +Y
			{ 0.f, -1.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f },     // -Y
		};

		OutPositions.clear();
		OutNormals.clear();
		OutUVs.clear();
		OutIndices.clear();
		for (const FFace& Face : Faces)
		{
			const std::uint32_t Base = static_cast<std::uint32_t>(OutPositions.size() / 3);
			for (int Corner = 0; Corner < 4; ++Corner)
			{
				const float U = (Corner == 1 || Corner == 2) ? 1.0f : 0.0f;
				const float V = (Corner >= 2) ? 1.0f : 0.0f;
				OutPositions.push_back(Face.NX * H + Face.TX * (U - 0.5f) + Face.BX * (V - 0.5f));
				OutPositions.push_back(Face.NY * H + Face.TY * (U - 0.5f) + Face.BY * (V - 0.5f));
				OutPositions.push_back(Face.NZ * H + Face.TZ * (U - 0.5f) + Face.BZ * (V - 0.5f));
				OutNormals.push_back(Face.NX);
				OutNormals.push_back(Face.NY);
				OutNormals.push_back(Face.NZ);
				OutUVs.push_back(U);
				OutUVs.push_back(V);
			}
			OutIndices.push_back(Base + 0);
			OutIndices.push_back(Base + 1);
			OutIndices.push_back(Base + 2);
			OutIndices.push_back(Base + 0);
			OutIndices.push_back(Base + 2);
			OutIndices.push_back(Base + 3);
		}
	}

	/** How many camera-less frames count as "the game never published one" instead of "this frame the
	 *  game side had not run yet" (the two collectors are not ordered, so frame 0 races). */
	constexpr std::uint32_t kNoCameraGraceFrames = 120;

	/** The explicit aspect the projection falls back to when the draw target reports a degenerate size.
	 *  16:9 is the shape every target in this engine has had so far; the point is that it is a NAMED
	 *  constant with a REPORT next to it, never a silent `0.0f / 0.0f` and never a NaN in the frame's
	 *  uniform buffer. */
	constexpr float kFallbackAspect = 16.0f / 9.0f;
	/** Cap on the change-triggered canvas reports (see the CANVAS WATCH below): enough to show an
	 *  alternating pair for many frames, small enough that a resize loop cannot flood the log. */
	constexpr int kMaxCanvasSwitchReports = 32;

	/**
	 * Slots in the camera-constant ring, and the number is `MAHO_FRAMES_IN_FLIGHT` on purpose.
	 *
	 * The ring's size is not a taste decision -- it is the number of frames whose host-visible writes may
	 * be in the GPU's hands at once, and that number is the RHI's per-slot fence depth (`kFrameSlots` in
	 * VulkanRHI.h, which IS `MAHO_FRAMES_IN_FLIGHT`): `BeginFrame` for frame N waits the fence of frame
	 * N - MAHO_FRAMES_IN_FLIGHT before anything of frame N is recorded (see FVulkanRHI::BeginFrame), so
	 * a slot written at frame N was last read by frame N - MAHO_FRAMES_IN_FLIGHT, which is retired by
	 * then. Exactly MAHO_FRAMES_IN_FLIGHT slots is therefore both NECESSARY (fewer would overwrite a
	 * buffer an in-flight frame still reads) and SUFFICIENT (one more only wastes memory).
	 *
	 * HISTORY -- why this is written down. This was once 3 with a single frame fence in the RHI: the
	 * declaration said 3 frames in flight and nothing enforced it, so a slot could be rewritten while
	 * the GPU still read it, and a moving camera turned that into a per-frame flicker (tear: part of
	 * the geometry transformed by the following frame's matrices, and sometimes an actor vanishing
	 * because its draw used another frame's view-projection). An 8-slot PROBE made the flicker stop,
	 * which is what proved the ring was being overwritten rather than the shader/material being wrong.
	 * With the RHI's per-slot fences in place the enforced depth and this ring agree again.
	 *
	 * NOTE the ring INDEX is the frame slot (`FramesSeen % kCameraRingSlots`, advanced once per frame
	 * even on frames that draw nothing) -- the same index space as the RHI's frame slot, not "slides
	 * I happened to allocate".
	 */
	constexpr std::size_t kCameraRingSlots = MAHO_FRAMES_IN_FLIGHT;

	/** Case-insensitive compare for parameter NAMES: the game side publishes the authoritative
	 *  spelling, but a shader file and a component are edited by different people, and `_basecolor`
	 *  versus `_BaseColor` must not silently become "the shader's default". */
	bool EqualsIgnoreCase(std::string_view A, std::string_view B)
	{
		if (A.size() != B.size())
		{
			return false;
		}
		for (std::size_t I = 0; I < A.size(); ++I)
		{
			const unsigned char CA = static_cast<unsigned char>(A[I]);
			const unsigned char CB = static_cast<unsigned char>(B[I]);
			if (std::tolower(CA) != std::tolower(CB))
			{
				return false;
			}
		}
		return true;
	}

	/** `ECullMode` (the .shader's vocabulary) -> the RHI's. An unmappable value is REPORTED, never
	 *  silently treated as the default (D7's rule). */
	bool ToCullMode(ECullMode Cull, ERHICullMode& Out)
	{
		switch (Cull)
		{
		case ECullMode::Back:  Out = ERHICullMode::Back;  return true;
		case ECullMode::Front: Out = ERHICullMode::Front; return true;
		case ECullMode::Off:   Out = ERHICullMode::None;  return true;
		}
		return false;
	}
} // namespace

const char* FMeshShader::GetVertexSource()       { return kVertexShader; }
const char* FMeshShader::GetFragmentSource()     { return kFragmentShader; }
const char* FMeshShader::GetVertexEntryPoint()   { return "main"; }
const char* FMeshShader::GetFragmentEntryPoint() { return "main"; }

FCubeMesh::FCubeMesh(std::string Path, std::vector<float> Positions, std::vector<float> Normals,
	std::vector<float> UVs, std::vector<std::uint32_t> Indices)
	: Resource::FStaticMesh(std::move(Path), std::string(), std::move(Positions), std::move(Normals),
		  std::move(UVs), std::move(Indices))
{
}

FMeshFeature::FMeshFeature()
{
	// Draw after the scene's clear, and record BEFORE the frame's submission point. The graph has no
	// stage barrier: a stage that records a pass must declare both edges or its pass lands in the
	// wrong frame's table (see FDrawTriangleFeature for the long version).
	MyStage<IRender>().IsWaiting<Scene::FScene>().ForStage<IEndRender>();
	MyStage<IRender>().IsBlocking<Scene::FScene>().OnStage<IPresent>();

	// (The demo fullscreen triangle that used to share this target is retired -- `Render.cplugin` no
	// longer installs `FDrawTriangleFeature`. It never wrote depth and drew at NDC z = 0, so it and the
	// mesh were a painter's-order pair across frame phases: whichever recorded last won, and the model
	// vanished on roughly half the presented frames. Rewriting it as a far-plane backdrop with a depth
	// write was not enough either (two shots 4 s apart: one with the cube, one without) -- so the demo
	// content goes, and real geometry is the only writer of SceneColor/SceneDepth.)
}

void FMeshFeature::InitViews(FRender&, FRenderContext&)
{
	// Nothing is uploaded here any more: geometry is created LAZILY by mesh name, on the first frame
	// an actor names it (EnsureMesh). Keeping the stage mounted documents "this feature owns render
	// resources", and it is the hook a future asset-path preload would use.
	MAHO_TRACE_STAGE(IInitViews, "Mesh view init", "geometry is created lazily per mesh name (see EnsureMesh)");
}

const FMeshFeature::FMeshBuffers* FMeshFeature::EnsureMesh(FRender& R, Name::FName MeshName)
{
	if (const auto It = Meshes.find(MeshName); It != Meshes.end())
	{
		return &It->second;
	}

	// -- provider table: mesh NAME -> source geometry ------------------------------------------------
	// TODO: route this through the asset system (a `Mesh.Cube` name should resolve to an imported
	// `.casset`), at which point this table becomes "the built-in/no-file fallback" it already is.
	std::vector<float> Positions;
	std::vector<float> Normals;
	std::vector<float> UVs;
	std::vector<std::uint32_t> Indices;
	if (MeshName == Name::FName("Mesh.Cube"))
	{
		BuildCubeSoA(Positions, Normals, UVs, Indices);
	}
	else
	{
		return nullptr;   // unknown name: reported once by the caller, not per frame
	}

	if (CubeMesh == nullptr)
	{
		CubeMesh = std::make_unique<FCubeMesh>("Mesh.Cube", Positions, Normals, UVs, Indices);
	}

	// The geometry comes from the ASSET: everything below is asset -> GPU. Interleaving happens here
	// because the RHI's vertex attributes carry no binding index (binding 0 is implicit), so an SoA
	// layout cannot be bound as separate streams. The interleaved SELF is cached (not rebuilt per
	// frame) even though it is not a render resource -- a name that shows up once should be uploaded
	// once.
	FMeshBuffers Buffers;

	const Resource::FStaticMesh& Mesh = *CubeMesh;
	const std::vector<float>& SrcPositions = Mesh.GetPositions();
	const std::vector<float>& SrcNormals = Mesh.GetNormals();
	const std::vector<float>& SrcUVs = Mesh.GetUVs();
	const std::vector<std::uint32_t>& SrcIndices = Mesh.GetIndices();

	const std::size_t VertexCount = SrcPositions.size() / 3;
	std::vector<FMeshVertex> Vertices;
	Vertices.reserve(VertexCount);
	for (std::size_t Index = 0; Index < VertexCount; ++Index)
	{
		FMeshVertex Vertex;
		Vertex.X = SrcPositions[Index * 3 + 0];
		Vertex.Y = SrcPositions[Index * 3 + 1];
		Vertex.Z = SrcPositions[Index * 3 + 2];
		if (SrcNormals.size() >= VertexCount * 3)
		{
			Vertex.NX = SrcNormals[Index * 3 + 0];
			Vertex.NY = SrcNormals[Index * 3 + 1];
			Vertex.NZ = SrcNormals[Index * 3 + 2];
		}
		if (SrcUVs.size() >= VertexCount * 2)
		{
			Vertex.U = SrcUVs[Index * 2 + 0];
			Vertex.V = SrcUVs[Index * 2 + 1];
		}
		Vertices.push_back(Vertex);
	}
	std::vector<std::uint32_t> IndexData = SrcIndices;

	// PERSISTENT, not Transient: these live for the feature's whole lifetime. A Transient handle is
	// frame-scoped by design (the pool advances its generation at every frame boundary), so holding
	// one across frames is a DETECTED error, not a slow path.
	FRHIBufferDesc VertexDesc;
	VertexDesc.Size = static_cast<std::uint64_t>(Vertices.size()) * sizeof(FMeshVertex);
	VertexDesc.Usage = ERHIBufferUsage::Vertex | ERHIBufferUsage::TransferDst;
	VertexDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
	Buffers.Vertices = R.CreateBuffer(VertexDesc, ERDGResourceLifetime::Persistent);

	FRHIBufferDesc IndexDesc;
	IndexDesc.Size = static_cast<std::uint64_t>(IndexData.size()) * sizeof(std::uint32_t);
	IndexDesc.Usage = ERHIBufferUsage::Index | ERHIBufferUsage::TransferDst;
	IndexDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
	Buffers.Indices = R.CreateBuffer(IndexDesc, ERDGResourceLifetime::Persistent);

	if (!Buffers.Vertices.IsValid() || !Buffers.Indices.IsValid())
	{
		MAHO_LOG_CORE_ERROR("MeshFeature: GPU buffers for mesh '{}' could not be created", Mesh.GetPath());
		return nullptr;
	}

	// One transfer pass, recorded off the frame graph's pass table: UpdateBuffer writes a host-visible
	// buffer directly, otherwise it stages the copy. It is submitted by the next IPresent, so the
	// buffers must be Persistent (see above) -- a frame-external pass outlives a Transient slot.
	// Registered BEFORE the actor passes that will use it (this runs inside IRender, while the draws
	// are recorded right after), and the frame's table is submitted in registration order.
	const std::vector<FMeshVertex> VertexCopy = Vertices;
	const std::vector<std::uint32_t> IndexCopy = IndexData;
	const FRDGBufferRef VertexRef = Buffers.Vertices;
	const FRDGBufferRef IndexRef = Buffers.Indices;
	R.AddPass(ERHICommandListType::Graphics, [VertexRef, IndexRef, VertexCopy, IndexCopy](FRHICommandList& Cmd)
	{
		Cmd.UpdateBuffer(VertexRef.GetRHI(), 0,
			static_cast<std::uint64_t>(VertexCopy.size()) * sizeof(FMeshVertex), VertexCopy.data());
		Cmd.UpdateBuffer(IndexRef.GetRHI(), 0,
			static_cast<std::uint64_t>(IndexCopy.size()) * sizeof(std::uint32_t), IndexCopy.data());
	});

	Buffers.IndexCount = static_cast<std::uint32_t>(IndexData.size());
	MAHO_LOG_CORE_INFO("MeshFeature: mesh '{}' uploaded ({} vertices, {} indices, stride {})",
		Mesh.GetPath(), static_cast<unsigned>(Vertices.size()), Buffers.IndexCount, sizeof(FMeshVertex));

	const auto Inserted = Meshes.emplace(MeshName, Buffers);
	return &Inserted.first->second;
}

void FMeshFeature::SyncActors(const std::vector<FRenderPrimitive>& Snapshot)
{
	std::map<std::uint32_t, FRenderPrimitive> Next;
	for (const FRenderPrimitive& Primitive : Snapshot)
	{
		// Later duplicates of the same id win: the game side owns identity, this side only mirrors it.
		Next[Primitive.ActorId] = Primitive;
	}

	if (Next.size() != Actors.size())
	{
		MAHO_LOG_CORE_INFO("MeshFeature: actor set is now {} (was {})", Next.size(), Actors.size());
	}
	Actors.swap(Next);   // geometry is keyed by mesh name and lives on, so disappearing actors just stop
}

void FMeshFeature::ReportRead(std::size_t PrimitiveCount, bool bCamera, std::uint64_t Publishes)
{
	if (PrimitiveCount == ReportedPrimitives && bCamera == bReportedCamera)
	{
		return;   // state unchanged (the publish counter is a per-line reading, not part of the key)
	}
	ReportedPrimitives = PrimitiveCount;
	bReportedCamera = bCamera;

	MAHO_LOG_CORE_INFO("MeshFeature: read {} primitive(s) from the mirror table, camera={} (mirror publishes={})",
		PrimitiveCount, bCamera ? "published" : "MISSING", Publishes);
}

void FMeshFeature::Render(FRender& R, FRenderContext&)
{
	MAHO_TRACE_STAGE(IRender, "Mesh pass", "read the mirror table and record one pass per actor");

	++FramesSeen;

	// THIS frame's ring slot, taken before every early return below: the ring must advance once per
	// FRAME, not once per drawn frame, or a frame that draws nothing would rotate the ring by zero and
	// the camera buffers would drift out of step with the RHI's frame slot (the two are the same index
	// space -- see kCameraRingSlots).
	const std::size_t FrameSlot = static_cast<std::size_t>(FramesSeen % kCameraRingSlots);

	// The mirror table: what to draw + the camera to draw it with. It is the ONLY game/render
	// interface -- this feature knows no entity, no component and no transform.
	FPrimitiveRegistry* Registry = GetPrimitiveRegistry();
	if (Registry == nullptr)
	{
		// Not a race this time: this is "the table's IInit has not run" (a different collector, no
		// ordering edge). Reported the first time and again when it clears -- a feature that never
		// reads anything must leave a trace of why, not just draw nothing.
		if (!bReportedNoRegistry)
		{
			bReportedNoRegistry = true;
			MAHO_LOG_CORE_WARN("MeshFeature: GetPrimitiveRegistry() is NULL (frame {}) -- the mirror table frame "
				"has not initialized; nothing is read or drawn this frame", FramesSeen);
		}
		return;
	}
	if (bReportedNoRegistry)
	{
		bReportedNoRegistry = false;
		MAHO_LOG_CORE_INFO("MeshFeature: primitive registry available (frame {}) -- reading the mirror table",
			FramesSeen);
	}

	// ONE read gives the whole scene: the primitives, the material table they index, and the
	// parameter values. Three tables, one snapshot -- see FRenderScene (they must not be read
	// separately, or a primitive could be paired with the previous frame's materials).
	std::shared_ptr<const FRenderScene> Snapshot = Registry->ReadLatest();
	std::shared_ptr<const FRenderCamera> Camera = Registry->ReadLatestCamera();

	Scene::FScene* Scene = Scene::GetScene();
	if (Scene == nullptr || !Scene->GetSceneColor().IsValid())
	{
		return;   // scene target not ready this frame
	}

	// THE DRAW TARGET'S OWN SIZE, and this is where the frame's aspect comes from. `GetSceneColor()`
	// is the shared scene color target FScene builds in `EnsureTargets` from
	// `R.GetCanvasWidth()/GetCanvasHeight()` (Scene/Private/Scene.cpp:104-116) -- `FRender::GetCanvasWidth`
	// -> `IRHI::GetFramebufferWidth` -> the SWAPCHAIN geometry, i.e. the offscreen CANVAS this frame is
	// drawn into (measured: 1264x761 in a 1280x800 window, 729x1421 after the window was resized to
	// 745x1460 -- the same target `SetViewport` below covers).
	// It is NOT the platform window's size: `Platform::GetWindowWidth/Height` is `glfwGetWindowSize`
	// (logical/DPI-scaled, and published on the platform thread's pump -- a few frames behind a resize),
	// which is the number the game side used to build its projection from. Whoever owns the draw target
	// owns the aspect, so the division lives HERE.
	const std::uint32_t TargetW = Scene->GetSceneColor().GetWidth();
	const std::uint32_t TargetH = Scene->GetSceneColor().GetHeight();

	// Defense: the aspect division is OURS now, so it is guarded here. A degenerate size falls back to
	// an explicit constant and is REPORTED (once) -- the frame is skipped right after (there is nothing
	// to draw into), but neither the constant nor the reason is silent.
	float Aspect = kFallbackAspect;
	if (TargetW > 0 && TargetH > 0)
	{
		Aspect = static_cast<float>(TargetW) / static_cast<float>(TargetH);
	}
	else
	{
		if (!bReportedDegenerateTarget)
		{
			bReportedDegenerateTarget = true;
			MAHO_LOG_CORE_ERROR(
				"MeshFeature: the scene target reports a degenerate size {}x{} -- the frame's aspect falls "
				"back to the explicit constant {:.3f} and this frame is SKIPPED (the target is not ready, or "
				"a resize raced the read; reported once)", TargetW, TargetH, kFallbackAspect);
		}
		return;
	}

	static const FRenderScene EmptyScene;
	SyncActors(Snapshot ? Snapshot->Primitives : EmptyScene.Primitives);
	if (Actors.empty())
	{
		ReportRead(0, Camera != nullptr, Registry->GetPublishCount());
		return;   // the game side says there is nothing to draw (an empty list is not an error)
	}

	// CANVAS WATCH -- the aspect above IS the frame's projection scale, and the only way it can make
	// the image flip between two states is by taking two different values on alternating frames. This
	// logs the TRANSITION, not the steady state, so a stable canvas costs ZERO lines and a two-state
	// canvas writes one line per switch (capped: a pathological resize loop must not flood the log).
	// The periodic DIAG further down stays the steady-state sample.
	{
		static std::uint32_t LastW = 0;
		static std::uint32_t LastH = 0;
		static int ReportedSwitches = 0;
		if ((TargetW != LastW || TargetH != LastH) && ReportedSwitches < kMaxCanvasSwitchReports)
		{
			++ReportedSwitches;
			MAHO_LOG_CORE_INFO(
				"MeshFeature CANVAS: frame {} canvas={}x{} aspect={:.4f} (previous {}x{}, switch {}/{})",
				FramesSeen, TargetW, TargetH, Aspect, LastW, LastH, ReportedSwitches,
				kMaxCanvasSwitchReports);
			LastW = TargetW;
			LastH = TargetH;
		}
	}

	// A camera must be DRAWABLE, not merely present: the mirror publishes an EMPTY `FRenderCamera` at
	// game-side teardown, and the channel now carries frustum PARAMETERS -- so "present but degenerate"
	// (fov <= 0, near <= 0, far <= near) is treated as "no camera" rather than composed into a
	// projection (`tan(0)` divides by zero and a NaN would reach the frame's UBO). Same one-shot report
	// path as a missing camera, so an empty camera at teardown costs nothing extra.
	const bool bCameraUsable = Camera != nullptr && Camera->FovY > 0.0f && Camera->Near > 0.0f
		&& Camera->Far > Camera->Near;
	if (!bCameraUsable)
	{
		ReportRead(Actors.size(), false, Registry->GetPublishCount());
		// No usable camera -> NO draw (a zero/hardcoded camera would silently show the wrong thing).
		// Reported ONCE after a grace period: frame 0 legitimately races the game side, a camera that
		// never arrives (or disappears later) does not, and either way a per-frame log is not a report.
		++FramesWithoutCamera;
		if (!bReportedNoCamera && FramesWithoutCamera > kNoCameraGraceFrames)
		{
			bReportedNoCamera = true;
			MAHO_LOG_CORE_ERROR(
				"MeshFeature: {} actors and no USABLE camera published for {} frames -- skipping the draw "
				"(the game side must publish a camera whose fov > 0 and 0 < near < far; the empty camera it "
				"publishes at teardown is not drawable; reported once)",
				Actors.size(), FramesWithoutCamera);
		}
		return;
	}
	// A camera arrived: the grace period restarts, and a LATER disappearance gets its own report.
	FramesWithoutCamera = 0;
	bReportedNoCamera = false;

	ReportRead(Actors.size(), true, Registry->GetPublishCount());

	// -- the frame's view-projection, composed HERE (not on the game side) --------------------------
	// The camera channel carries a VIEW matrix plus fov/near/far; the projection's aspect belongs to
	// the side that owns the draw target, which is this one. The Y flip is right-multiplied, exactly
	// as the game side used to do it (Vulkan clip space is y-down; flipping on the other side of the
	// multiplication flips the image instead of the axis).
	const glm::mat4 Projection = glm::perspectiveRH_ZO(Camera->FovY, Aspect, Camera->Near, Camera->Far)
	                           * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f));
	FViewConstants ViewConstants{};
	const glm::mat4 View = glm::make_mat4(Camera->View);
	std::memcpy(ViewConstants.ViewProj,
		glm::value_ptr(Projection * View),
		sizeof(ViewConstants.ViewProj));

	// 诊断（材质闭环验证期）：每秒一次，把渲染侧**实际拿到**的相机与 actor 数写下来 —— 与游戏侧
	// 每 600 帧那条 `camera published` 并排看，就能区分"快照本身错"与"渲染侧用错/没画"。
	// `canvas=` / `aspect=` 是这次排查的抓手：aspect 就是上面对 canvas 宽高做的那个除法，必须逐位相等。
	// 那几个 vp 值是 view-projection 指纹：v5 = -1/tan(fovy/2) 与 aspect 无关，v0 含 view 的旋转项，
	// 只有相机朝向为 0 时才等于 1/(aspect*tan(fovy/2))。
	{
		static std::uint32_t DiagTick = 0;
		if ((++DiagTick % 600) == 1)
		{
				MAHO_LOG_CORE_INFO(
					"MeshFeature DIAG: frame={} slot={} actors={} canvas={}x{} aspect={:.3f} vp0={:.3f} vp5={:.3f} vp10={:.3f} "
					"vp11={:.3f} vp14={:.3f} vp15={:.3f}",
					FramesSeen, FrameSlot, Actors.size(), TargetW, TargetH, Aspect,
				ViewConstants.ViewProj[0], ViewConstants.ViewProj[5],
				ViewConstants.ViewProj[10], ViewConstants.ViewProj[11],
				ViewConstants.ViewProj[14], ViewConstants.ViewProj[15]);
		}
	}

	// Camera constants: ONE buffer PER FRAME-IN-FLIGHT SLOT, Persistent, written only by the frame that
	// owns it. A frame-scoped transient (what this was) is recycled at the NEXT BeginFrame while the CPU
	// may already be MAHO_FRAMES_IN_FLIGHT frames ahead of the GPU -- so a frame would overwrite bytes a
	// previous frame is still reading, and the mesh draws with the FOLLOWING frame's matrices. Static
	// cameras hide it (identical bytes written again); a camera that moves every frame makes it a live
	// flicker.
	FRHIBufferDesc UniformDesc;
	UniformDesc.Size = sizeof(FViewConstants);
	UniformDesc.Usage = ERHIBufferUsage::Uniform;
	UniformDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
	if (CameraConstantBuffers.size() != kCameraRingSlots)
	{
		CameraConstantBuffers.resize(kCameraRingSlots);
	}
	const std::size_t ViewSlot = FrameSlot;
	if (!CameraConstantBuffers[ViewSlot].IsValid())
	{
		CameraConstantBuffers[ViewSlot] = R.CreateBuffer(UniformDesc, ERDGResourceLifetime::Persistent);
	}
	const FRDGBufferRef ViewBuffer = CameraConstantBuffers[ViewSlot];
	if (!ViewBuffer.IsValid())
	{
		MAHO_LOG_CORE_ERROR("MeshFeature: view-constant buffer creation failed (slot {})", ViewSlot);
		return;
	}

	FRenderTarget Target;
	FRenderTarget::FAttachment Color;
	Color.View = Scene->GetSceneColor();
	Color.LoadOp = ERHILoadOp::Load;
	Color.StoreOp = ERHIStoreOp::Store;
	Target.AddColor(Color);
	if (Scene->GetSceneDepth().IsValid())
	{
		FRenderTarget::FAttachment Depth;
		Depth.View = Scene->GetSceneDepth();
		Depth.LoadOp = ERHILoadOp::Load;
		Depth.StoreOp = ERHIStoreOp::DontCare;
		Target.SetDepth(Depth);
	}

	const FRenderScene& SceneSnapshot = Snapshot != nullptr ? *Snapshot : EmptyScene;

	// TWO PATHS, ONE OF WHICH IS THE REAL ONE:
	//   - the ShaderLibrary path: every material's shader comes from `Content/Shaders`, with its own
	//     layout, parameter block and render state (tasks 6.1-6.4);
	//   - the built-in GLSL path: only when the ShaderLibrary is not available at all (task 6.5).
	if (GetShaderLibrary() != nullptr)
	{
		DrawWithMaterials(R, SceneSnapshot, Target, TargetW, TargetH, ViewConstants, ViewBuffer);
	}
	else
	{
		static bool bReportedNoLibrary = false;
		if (!bReportedNoLibrary)
		{
			bReportedNoLibrary = true;
			MAHO_LOG_CORE_WARN("MeshFeature: GetShaderLibrary() is NULL -- drawing with the built-in GLSL "
				"fallback (no materials, no .shader file is read; reported once)");
		}
		DrawWithBuiltinShader(R, SceneSnapshot, Target, TargetW, TargetH, ViewConstants, ViewBuffer);
	}
}

bool FMeshFeature::EnsureWhiteTexture(FRender& R)
{
	if (WhiteTexture.IsValid() && WhiteSampler != nullptr)
	{
		return true;
	}
	if (bWhiteTextureFailed)
	{
		return false;   // already reported; do not retry every frame
	}

	// A 1x1 white texel. It exists so a material whose texture cannot be resolved still DRAWS --
	// "no texture" must cost a log line, not a black screen (and a white multiply in the shader is
	// the neutral choice: only the tint shows).
	FRHITextureDesc TextureDesc;
	TextureDesc.Format = ERHIFormat::R8G8B8A8_UNORM;
	TextureDesc.Dimension = ERHITextureDimension::Tex2D;
	TextureDesc.Extent = FRHIExtent3D{ 1u, 1u, 1u };
	TextureDesc.Usage = ERHITextureUsage::Sampled | ERHITextureUsage::TransferDst;
	TextureDesc.MemoryUsage = ERHIMemoryUsage::GPUOnly;
	WhiteTexture = R.CreateTexture(TextureDesc, ERDGResourceLifetime::Persistent);

	FRHIBufferDesc StagingDesc;
	StagingDesc.Size = 4;
	StagingDesc.Usage = ERHIBufferUsage::TransferSrc;
	StagingDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
	WhiteTextureStaging = R.CreateBuffer(StagingDesc, ERDGResourceLifetime::Persistent);

	FRHISamplerDesc SamplerDesc;   // linear + repeat: 1x1 texel, so the address mode cannot matter
	WhiteSampler = R.CreateSampler(SamplerDesc);

	if (!WhiteTexture.IsValid() || !WhiteTextureStaging.IsValid() || WhiteSampler == nullptr)
	{
		bWhiteTextureFailed = true;
		MAHO_LOG_CORE_ERROR("MeshFeature: the white fallback texture could not be created -- a material with an "
			"unresolvable texture will be SKIPPED (reported once)");
		return false;
	}

	const std::uint32_t White = 0xFFFFFFFFu;
	const FRDGBufferRef StagingRef = WhiteTextureStaging;
	const FRDGTextureRef TextureRef = WhiteTexture;
	R.AddPass(ERHICommandListType::Graphics, [StagingRef, TextureRef, White](FRHICommandList& Cmd)
	{
		Cmd.UpdateBuffer(StagingRef.GetRHI(), 0, sizeof(White), &White);
		FRHITextureCopyRegion Region;
		Region.MipLevel = 0;
		Region.BaseArrayLayer = 0;
		Region.LayerCount = 1;
		Region.Extent = FRHIExtent3D{ 1u, 1u, 1u };
		// The copy carries its own transitions (Common -> ShaderResource), so the sampled read in a
		// later pass is in a legal layout without a hand-written barrier here.
		Cmd.CopyBufferToTexture(StagingRef.GetRHI(), 0, TextureRef.GetRHI(), Region);
	});

	MAHO_LOG_CORE_INFO("MeshFeature: white fallback texture created (1x1, {} bytes uploaded)",
		static_cast<unsigned>(sizeof(White)));
	return true;
}

const FMeshFeature::FShaderEntry* FMeshFeature::ResolveShader(FRender& R, Name::FName ShaderName,
	ERHIFormat ColorFormat, ERHIFormat DepthFormat)
{
	FShaderLibrary* Library = GetShaderLibrary();
	if (Library == nullptr)
	{
		return nullptr;
	}

	const FShaderDesc* Desc = Library->Get(ShaderName);
	if (Desc == nullptr)
	{
		// Unknown/invalid shader: fall back, but SAY SO. A silently magenta cube looks like a material
		// that "works", and the typo in the name is then found by reading code, not the log.
		if (std::find(UnresolvedShaders.begin(), UnresolvedShaders.end(), ShaderName) == UnresolvedShaders.end())
		{
			UnresolvedShaders.push_back(ShaderName);
			const FShaderDesc* Fallback = Library->GetFallback();
			MAHO_LOG_CORE_ERROR("MeshFeature: shader '{}' is not in the ShaderLibrary -- using the fallback '{}' "
				"(reported once; check Content/Shaders and the ShaderLibrary init log)",
				std::string(ShaderName.ToString()),
				Fallback != nullptr ? std::string(Fallback->Name.ToString()) : std::string("<none>"));
		}
		Desc = Library->GetFallback();
	}
	if (Desc == nullptr)
	{
		if (std::find(UnresolvedShaders.begin(), UnresolvedShaders.end(), ShaderName) == UnresolvedShaders.end())
		{
			UnresolvedShaders.push_back(ShaderName);
			MAHO_LOG_CORE_ERROR("MeshFeature: shader '{}' AND the built-in fallback are unusable -- the material "
				"cannot be drawn (see the ShaderLibrary init log; reported once)", std::string(ShaderName.ToString()));
		}
		return nullptr;
	}

	const std::uint64_t VertexHash = HashShaderWords(Desc->VertexBytecode.data(), Desc->VertexBytecode.size());
	const std::uint64_t FragmentHash = HashShaderWords(Desc->FragmentBytecode.data(), Desc->FragmentBytecode.size());

	// The pass' render state maps to the pipeline (D7). Culling is the one with an unmappable value on
	// this side, so it is resolved with a checked conversion: an unknown mode is REPORTED and the
	// pipeline keeps the shader's own intent as far as it can (Back) instead of quietly dropping the key.
	ERHICullMode Cull = ERHICullMode::Back;
	if (!ToCullMode(Desc->Pass.Cull, Cull))
	{
		MAHO_LOG_CORE_ERROR("MeshFeature: shader '{}' declares a Cull mode this build cannot map (value {}) -- "
			"using Back; the render state key would otherwise be silently ignored",
			std::string(Desc->Name.ToString()), static_cast<int>(Desc->Pass.Cull));
		Cull = ERHICullMode::Back;
	}

	FPipelineKey Key;
	Key.ShaderId = ShaderName.GetId();
	Key.ShaderHash = VertexHash ^ (FragmentHash * 0x9E3779B97F4A7C15ull);
	Key.Cull = Cull;
	Key.bDepthWrite = Desc->Pass.bZWrite;
	Key.bDepthTest = Desc->Pass.bZTest;
	Key.bBlend = Desc->Pass.Blend != EBlendMode::Opaque;
	Key.ColorFormat = ColorFormat;
	Key.DepthFormat = DepthFormat;

	if (const auto It = Pipelines.find(Key); It != Pipelines.end())
	{
		return &It->second;
	}

	FShaderEntry Entry;
	Entry.Description = Desc;
	Entry.VertexEntry = Desc->VertexEntry;
	Entry.FragmentEntry = Desc->FragmentEntry;

	FRHIShaderModuleDesc VertexModule;
	VertexModule.Stage = ERHIShaderStage::Vertex;
	VertexModule.Bytecode = Desc->VertexBytecode.data();
	VertexModule.BytecodeSize = Desc->VertexBytecode.size() * sizeof(std::uint32_t);
	VertexModule.EntryPoint = Entry.VertexEntry.c_str();
	Entry.Vertex = R.GetOrCreateShaderModule(VertexModule);

	FRHIShaderModuleDesc FragmentModule;
	FragmentModule.Stage = ERHIShaderStage::Fragment;
	FragmentModule.Bytecode = Desc->FragmentBytecode.data();
	FragmentModule.BytecodeSize = Desc->FragmentBytecode.size() * sizeof(std::uint32_t);
	FragmentModule.EntryPoint = Entry.FragmentEntry.c_str();
	Entry.Fragment = R.GetOrCreateShaderModule(FragmentModule);

	if (Entry.Vertex == nullptr || Entry.Fragment == nullptr)
	{
		MAHO_LOG_CORE_ERROR("MeshFeature: shader modules for '{}' could not be created (vs {}, fs {}) -- material skipped",
			std::string(Desc->Name.ToString()), Entry.Vertex != nullptr ? "ok" : "null",
			Entry.Fragment != nullptr ? "ok" : "null");
		return nullptr;
	}

	FRHIGraphicsPipelineDesc& Pipeline = Entry.Pipeline;
	Pipeline.VertexShader = Entry.Vertex;
	Pipeline.FragmentShader = Entry.Fragment;
	Pipeline.VertexEntryPoint = Entry.VertexEntry.c_str();
	Pipeline.FragmentEntryPoint = Entry.FragmentEntry.c_str();
	Pipeline.VertexShaderHash = VertexHash;
	Pipeline.FragmentShaderHash = FragmentHash;
	Pipeline.RenderPass = nullptr;   // dynamic rendering
	Pipeline.Topology = ERHIPrimitiveTopology::TriangleList;
	Pipeline.VertexStride = sizeof(FMeshVertex);
	// Attributes carry no binding index (binding 0 only) -- hence the interleaved vertex.
	Pipeline.Attributes = {
		{ 0, ERHIFormat::R32G32B32_SFLOAT, offsetof(FMeshVertex, X) },
		{ 1, ERHIFormat::R32G32B32_SFLOAT, offsetof(FMeshVertex, NX) },
		{ 2, ERHIFormat::R32G32_SFLOAT, offsetof(FMeshVertex, U) },
	};
	Pipeline.FillMode = ERHIFillMode::Solid;
	Pipeline.ColorFormat = ColorFormat;
	Pipeline.DepthFormat = DepthFormat;
	Pipeline.CullMode = Cull;
	// Our projection flips Y into Vulkan clip space, which flips the screen-space winding of every
	// triangle -- so the FRONT face is the clockwise one, and a `.shader`'s `Cull Back` then means what
	// its author expects. Leaving this at the Vulkan default culls the faces you can see: no error, no
	// log, the model just is not there (found the hard way, see ERHIFrontFace).
	Pipeline.FrontFace = ERHIFrontFace::Clockwise;
	Pipeline.bDepthTest = Desc->Pass.bZTest;
	Pipeline.bDepthWrite = Desc->Pass.bZWrite;
	// `ZTest Off` is an on/off switch in this slice (the parser accepts exactly LEqual/Off), so the
	// comparison function is the one LEqual means.
	Pipeline.DepthCompare = ERHICompareOp::LessOrEqual;
	switch (Desc->Pass.Blend)
	{
	case EBlendMode::Opaque:
		break;   // AttachmentBlends stays empty = blending off
	case EBlendMode::AlphaBlend:
	{
		FRHIAttachmentBlend Blend;
		Blend.bBlend = true;
		Blend.SrcColorFactor = ERHIBlendFactor::SrcAlpha;
		Blend.DstColorFactor = ERHIBlendFactor::OneMinusSrcAlpha;
		Blend.SrcAlphaFactor = ERHIBlendFactor::One;
		Blend.DstAlphaFactor = ERHIBlendFactor::OneMinusSrcAlpha;
		Blend.ColorOp = ERHIBlendOp::Add;
		Blend.AlphaOp = ERHIBlendOp::Add;
		Pipeline.AttachmentBlends = { Blend };
		break;
	}
	default:
		// Unreachable while `EBlendMode` has two values, and written out anyway: the rule is that a
		// render state the renderer cannot express is REPORTED, never silently ignored (D7).
		MAHO_LOG_CORE_ERROR("MeshFeature: shader '{}' declares a Blend mode this build cannot map (value {}) -- "
			"drawing opaque", std::string(Desc->Name.ToString()), static_cast<int>(Desc->Pass.Blend));
		break;
	}

	const auto Inserted = Pipelines.emplace(Key, std::move(Entry));
	FShaderEntry& Stored = Inserted.first->second;

	// ★ 两个入口名必须在**移动之后**重新指向容器里那一份 `std::string` 的缓冲。
	// `std::string` 的短字符串优化把字符存在**对象内部**，所以 `Entry.VertexEntry.c_str()`
	// （移动前取的）指向的是**被移动走的那个局部变量**的栈内存 —— 函数一返回，那儿就是复用的
	// 栈（MSVC 填 0xCC）。症状取决于运气：Release 里是 UB，Debug 里驱动直接拒收
	// （`pName` = "烫烫烫…" ⇒ VUID-...-pName-parameter / -00707 ⇒ vkCreateGraphicsPipelines
	// 返回 VK_ERROR_UNKNOWN）。std::map 的节点地址稳定，所以指向节点内部的那一份是长期有效的。
	Stored.Pipeline.VertexEntryPoint = Stored.VertexEntry.c_str();
	Stored.Pipeline.FragmentEntryPoint = Stored.FragmentEntry.c_str();
	return &Stored;
}

void FMeshFeature::PackMaterialParameters(const FShaderDesc& Desc, const FRenderMaterial& Material,
	const std::vector<FRenderMaterialParam>& Params, std::vector<std::byte>& Out) const
{
	// The buffer is opened at the layout's own size (never smaller than one std140 vec4: a shader with
	// no scalar properties still gets a valid, if empty, uniform block to bind).
	Out.assign(std::max<std::uint32_t>(Desc.Material.UniformSize, 16u), std::byte{ 0 });

	for (const FMaterialProperty& Property : Desc.Material.Properties)
	{
		if (Property.Type == EMaterialPropertyType::Texture2D || Property.Type == EMaterialPropertyType::TextureCube)
		{
			continue;   // samplers are set2 bindings 1..N, not members of the constant block
		}
		if (static_cast<std::size_t>(Property.Offset) + Property.Size > Out.size())
		{
			continue;   // cannot happen while the layout and the block come from one declaration order
		}

		// Override wins; otherwise the .shader's own default. "Not set" is "the shader's default",
		// never zero -- that is what makes a component with one line in it usable.
		const FRenderMaterialParam* Override = nullptr;
		for (std::uint32_t I = 0; I < Material.ParamCount; ++I)
		{
			const std::size_t Index = static_cast<std::size_t>(Material.FirstParam) + I;
			if (Index >= Params.size())
			{
				break;
			}
			if (EqualsIgnoreCase(Params[Index].Name.ToString(), Property.Name))
			{
				Override = &Params[Index];
				break;
			}
		}

		if (Property.Type == EMaterialPropertyType::Int)
		{
			// Int is stored as its bit pattern in the layout; a float override is an int for the shader.
			std::int32_t Value = 0;
			if (Override != nullptr)
			{
				Value = static_cast<std::int32_t>(Override->Value[0]);
			}
			else
			{
				std::memcpy(&Value, &Property.Default[0], sizeof(Value));
			}
			std::memcpy(Out.data() + Property.Offset, &Value, sizeof(Value));
			continue;
		}

		float Value[4] = { Property.Default[0], Property.Default[1], Property.Default[2], Property.Default[3] };
		if (Override != nullptr)
		{
			for (int Lane = 0; Lane < 4; ++Lane)
			{
				Value[Lane] = Override->Value[Lane];
			}
		}
		const std::uint32_t Bytes = std::min<std::uint32_t>(Property.Size, sizeof(Value));
		std::memcpy(Out.data() + Property.Offset, Value, Bytes);
	}
}

void FMeshFeature::ResolveMaterial(FRender& R, const FRenderScene& Scene, std::size_t MaterialIndex,
	ERHIFormat ColorFormat, ERHIFormat DepthFormat, FMaterialFrame& Out)
{
	if (MaterialIndex >= Scene.Materials.size())
	{
		return;   // no material table entry (an empty snapshot): the actor is skipped by the caller
	}
	const FRenderMaterial& Material = Scene.Materials[MaterialIndex];

	Out.Shader = ResolveShader(R, Material.Shader, ColorFormat, DepthFormat);
	const FShaderDesc* Desc = Out.Shader != nullptr ? Out.Shader->Description : nullptr;

	// -- set2 binding0: the material's parameter block ------------------------------------------
	std::uint32_t BufferSize = 16;
	if (Desc != nullptr)
	{
		PackMaterialParameters(*Desc, Material, Scene.Params, Out.Packed);
		BufferSize = static_cast<std::uint32_t>(Out.Packed.size());
	}
	FRHIBufferDesc ParamDesc;
	ParamDesc.Size = BufferSize;
	ParamDesc.Usage = ERHIBufferUsage::Uniform;
	ParamDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
	// TRANSIENT: one slot per frame, recycled after the frame's fence -- the pool keeps the native
	// across frames (stable descriptor key, design D5's "no churn" note), and a slot is never written
	// while an in-flight frame still reads it.
	Out.ParamBuffer = R.CreateBuffer(ParamDesc, ERDGResourceLifetime::Transient);
	if (!Out.ParamBuffer.IsValid())
	{
		Out.Shader = nullptr;   // without the block there is nothing to bind at set2 binding0
		return;
	}

	// -- set2 binding1..N: the material's texture properties, in declaration order ----------------
	// Every declared slot must hold a VALID texture + sampler, used or not: the typed AddPass writes
	// every resource member of the parameter struct into the set, so an unfilled slot would be a null
	// descriptor write. Unused slots therefore get the white texel.
	const bool bHasWhite = EnsureWhiteTexture(R);
	if (!bHasWhite)
	{
		Out.Shader = nullptr;   // no valid texture to bind at all: skip the actor (EnsureWhiteTexture reported)
		return;
	}
	for (std::uint32_t Slot = 0; Slot < kMaxMaterialSamplers; ++Slot)
	{
		Out.Textures[Slot] = WhiteTexture;
		Out.Samplers[Slot] = WhiteSampler;
	}
	if (Desc == nullptr)
	{
		return;   // modules/params failed: reported by ResolveShader; sampled slots stay white
	}

	for (const FMaterialProperty& Property : Desc->Material.Properties)
	{
		if (Property.SamplerBinding == 0)
		{
			continue;   // not a texture property
		}
		if (Property.SamplerBinding > kMaxMaterialSamplers)
		{
			if (std::find(UnresolvedShaders.begin(), UnresolvedShaders.end(), Material.Shader) == UnresolvedShaders.end())
			{
				UnresolvedShaders.push_back(Material.Shader);
				MAHO_LOG_CORE_ERROR("MeshFeature: shader '{}' declares more than {} texture properties ('{}' is binding {}) "
					"-- the extra samplers are NOT bound (this slice's set2 has {} sampler slots)",
					std::string(Desc->Name.ToString()), kMaxMaterialSamplers, Property.Name, Property.SamplerBinding,
					kMaxMaterialSamplers);
			}
			continue;
		}
		const std::uint32_t Slot = Property.SamplerBinding - 1;

		// Which texture? The game side's override for this property, else the .shader's own default
		// (`= "white" {}` / an asset name a designer wrote in the shader).
		Name::FName TextureName = Property.DefaultTexture.empty() ? Name::FName() : Name::FName(Property.DefaultTexture);
		for (std::uint32_t I = 0; I < Material.ParamCount; ++I)
		{
			const std::size_t Index = static_cast<std::size_t>(Material.FirstParam) + I;
			if (Index >= Scene.Params.size())
			{
				break;
			}
			if (EqualsIgnoreCase(Scene.Params[Index].Name.ToString(), Property.Name) && !Scene.Params[Index].Texture.IsNone())
			{
				TextureName = Scene.Params[Index].Texture;
				break;
			}
		}

		// UNITY'S BUILT-IN DEFAULTS are not asset names: `= "white" {}` (and a text-less default) mean
		// "the white texel", which is exactly what the slot already holds -- so this is NOT a missing
		// asset and must not be reported as one. Without this guard the default shader complains on every
		// run about a texture nobody referenced, and a real typo drowns in that noise. Every OTHER name is
		// still a genuine lookup (a typo must stay visible, see the report below).
		if (TextureName.IsNone() || EqualsIgnoreCase(TextureName.ToString(), "white"))
		{
			continue;
		}

		const FRDGResourceRef* Mirror = TextureName.IsNone() ? nullptr : R.GetMirror(TextureName);
		const FRDGTextureRef* MirrorTexture = Mirror != nullptr ? std::get_if<FRDGTextureRef>(Mirror) : nullptr;
		if (MirrorTexture != nullptr && MirrorTexture->IsValid())
		{
			Out.Textures[Slot] = *MirrorTexture;
			if (FRHISampler* MirrorSampler = R.GetMirrorSampler(TextureName))
			{
				Out.Samplers[Slot] = MirrorSampler;
			}
			continue;
		}

		// MISSING TEXTURE: one report (per material+property), and the white texel keeps drawing.
		// Not silent -- a material that samples white because of a typo looks exactly like one that is
		// supposed to be tinted white, and the difference must be readable in the log.
		std::string ReportKey(Material.Name.ToString());
		ReportKey += '.';
		ReportKey += Property.Name;
		if (std::find(ReportedMissingTextures.begin(), ReportedMissingTextures.end(), ReportKey) == ReportedMissingTextures.end())
		{
			ReportedMissingTextures.push_back(ReportKey);
			MAHO_LOG_CORE_ERROR("MeshFeature: material '{}' property '{}' names texture '{}', which has no GPU mirror "
				"(not imported? typo?) -- binding the 1x1 white fallback texture instead and drawing on (reported once)",
				std::string(Material.Name.ToString()), Property.Name,
				TextureName.IsNone() ? std::string("<none>") : std::string(TextureName.ToString()));
		}
	}
}

void FMeshFeature::DrawWithMaterials(FRender& R, const FRenderScene& Scene, const FRenderTarget& Target,
	std::uint32_t TargetW, std::uint32_t TargetH, const FViewConstants& ViewConstants,
	const FRDGBufferRef& ViewBuffer)
{
	const ERHIFormat ColorFormat = Target.Color.empty() ? ERHIFormat::B8G8R8A8_UNORM : Target.Color[0].View.GetFormat();
	const ERHIFormat DepthFormat = Target.bHasDepth ? Target.Depth.View.GetFormat() : ERHIFormat::D32_SFLOAT;

	// One resolve per material the frame holds (not per actor): the parameter buffer and the sampler
	// bindings are the SAME objects for every actor that references that material -- which is what
	// "shared material" means on the GPU side.
	std::vector<FMaterialFrame> Materials(Scene.Materials.size());
	for (std::size_t Index = 0; Index < Scene.Materials.size(); ++Index)
	{
		ResolveMaterial(R, Scene, Index, ColorFormat, DepthFormat, Materials[Index]);
	}

	for (const auto& [ActorId, Primitive] : Actors)
	{
		// Geometry is per MESH NAME: several actors sharing one mesh share one buffer pair.
		const FMeshBuffers* Mesh = EnsureMesh(R, Primitive.Mesh);
		if (Mesh == nullptr)
		{
			if (std::find(UnresolvedMeshes.begin(), UnresolvedMeshes.end(), Primitive.Mesh) == UnresolvedMeshes.end())
			{
				UnresolvedMeshes.push_back(Primitive.Mesh);
				MAHO_LOG_CORE_ERROR("MeshFeature: actor {} names mesh '{}', which has no provider -- skipped",
					ActorId, std::string(Primitive.Mesh.ToString()));
			}
			continue;
		}

		const bool bHasMaterial = Primitive.MaterialIndex < Materials.size();
		const FMaterialFrame* Material = bHasMaterial ? &Materials[Primitive.MaterialIndex] : nullptr;
		if (Material == nullptr || Material->Shader == nullptr || !Material->ParamBuffer.IsValid())
		{
			// NOT silent, and it names WHICH of the three reasons it was: "the model is missing" with a
			// clean log is un-debuggable (that is how an intermittent missing actor cost an evening).
			static std::vector<std::uint32_t> ReportedSkips;
			if (std::find(ReportedSkips.begin(), ReportedSkips.end(), ActorId) == ReportedSkips.end())
			{
				ReportedSkips.push_back(ActorId);
				const char* Reason = Material == nullptr
					? "no material resolved for it (MaterialIndex is out of range of the published material table)"
					: (Material->Shader == nullptr
						  ? "its material has no shader (the root cause was reported by ResolveShader/ResolveMaterial)"
						  : "its material parameter buffer is INVALID this frame (the frame-scoped handle did not resolve)");
				MAHO_LOG_CORE_ERROR("MeshFeature: actor {} is NOT drawn -- {} (reported once per actor)", ActorId, Reason);
			}
			continue;
		}

		FMeshParameters* Params = R.AllocParameters<FMeshParameters>();
		if (Params == nullptr)
		{
			continue;
		}
		Params->ViewConstants = ViewBuffer;
		Params->Set1Placeholder = ViewBuffer;   // the reserved slot's placeholder (see FMeshParameters)
		Params->MaterialParams = Material->ParamBuffer;
		Params->MaterialTexture0 = Material->Textures[0];
		Params->MaterialTexture1 = Material->Textures[1];
		Params->MaterialTexture2 = Material->Textures[2];
		Params->MaterialTexture3 = Material->Textures[3];
		Params->MaterialSampler0 = Material->Samplers[0];
		Params->MaterialSampler1 = Material->Samplers[1];
		Params->MaterialSampler2 = Material->Samplers[2];
		Params->MaterialSampler3 = Material->Samplers[3];
		// The push block is filled BEFORE AddPass: the typed path packs it into the pipeline's push
		// range while it records, so writing it from inside the lambda below could be too late. One
		// AllocParameters call per actor -- the pool is a bump allocator, so each actor's matrix has
		// its own storage.
		std::memcpy(Params->ObjectMatrix, Primitive.ObjectMatrix, sizeof(Primitive.ObjectMatrix));

		// Buffers are captured BY VALUE (the lambda may run when the pass is submitted, not here), and
		// the uploads ride in EVERY actor's list: passes record in parallel, so an upload that only
		// lived in one of them would not be ordered against the others. The bytes are identical in
		// each, so the redundancy is free of consequence.
		const FRDGBufferRef VertexRef = Mesh->Vertices;
		const FRDGBufferRef IndexRef = Mesh->Indices;
		const std::uint32_t IndexCount = Mesh->IndexCount;
		const FRDGBufferRef ParamRef = Material->ParamBuffer;
		const std::vector<std::byte> Packed = Material->Packed;
		MAHO_TRACE_SECTION("record the mesh draw pass", nullptr);
		R.AddPass(ERHICommandListType::Graphics, Material->Shader->Pipeline, Target, Params,
			[VertexRef, IndexRef, IndexCount, ViewBuffer, ViewConstants, ParamRef, Packed, TargetW, TargetH](
				FRHICommandList& Cmd)
			{
				Cmd.UpdateBuffer(ViewBuffer.GetRHI(), 0, sizeof(FViewConstants), &ViewConstants);
				if (!Packed.empty())
				{
					Cmd.UpdateBuffer(ParamRef.GetRHI(), 0, static_cast<std::uint64_t>(Packed.size()), Packed.data());
				}

				Cmd.SetViewport(0.0f, 0.0f, static_cast<float>(TargetW), static_cast<float>(TargetH));
				Cmd.SetScissor(0, 0, TargetW, TargetH);
				Cmd.BindVertexBuffer(0, VertexRef.GetRHI(), 0);
				Cmd.BindIndexBuffer(IndexRef.GetRHI(), 0, true);
				Cmd.DrawIndexed(IndexCount, 1, 0, 0, 0);
			});
	}
}

void FMeshFeature::DrawWithBuiltinShader(FRender& R, const FRenderScene& Scene, const FRenderTarget& Target,
	std::uint32_t TargetW, std::uint32_t TargetH, const FViewConstants& ViewConstants,
	const FRDGBufferRef& ViewBuffer)
{
	// The floor: no ShaderLibrary, so no material table is consulted at all. Only the geometry, the
	// frame's view-projection and the per-object push constant survive from the pre-material path.
	(void)Scene;

	if (!EnsureWhiteTexture(R))
	{
		return;   // the empty sampler slots cannot be filled; reported once by EnsureWhiteTexture
	}

	TShaderHandle<FMeshShader> Shader = R.TryGetShader<FMeshShader>();
	FRHIShaderModule* VS = nullptr;
	FRHIShaderModule* FS = nullptr;
	if (Shader.Wait())
	{
		VS = Shader.GetVertex();
		FS = Shader.GetFragment();
	}
	if (VS == nullptr || FS == nullptr)
	{
		MAHO_LOG_CORE_ERROR("MeshFeature: the built-in fallback shader is not ready");
		return;
	}

	FRHIGraphicsPipelineDesc PipelineDesc;
	PipelineDesc.VertexShader = VS;
	PipelineDesc.FragmentShader = FS;
	PipelineDesc.VertexShaderHash = Shader.GetVertexHash();
	PipelineDesc.FragmentShaderHash = Shader.GetFragmentHash();
	PipelineDesc.VertexEntryPoint = Detail::GetVertexEntryPoint<FMeshShader>();
	PipelineDesc.FragmentEntryPoint = Detail::GetFragmentEntryPoint<FMeshShader>();
	PipelineDesc.RenderPass = nullptr;   // dynamic rendering
	PipelineDesc.Topology = ERHIPrimitiveTopology::TriangleList;
	PipelineDesc.VertexStride = sizeof(FMeshVertex);
	PipelineDesc.Attributes = {
		{ 0, ERHIFormat::R32G32B32_SFLOAT, offsetof(FMeshVertex, X) },
		{ 1, ERHIFormat::R32G32B32_SFLOAT, offsetof(FMeshVertex, NX) },
		{ 2, ERHIFormat::R32G32_SFLOAT, offsetof(FMeshVertex, U) },
	};
	PipelineDesc.CullMode = ERHICullMode::None;
	PipelineDesc.FillMode = ERHIFillMode::Solid;
	PipelineDesc.ColorFormat = Target.Color.empty() ? ERHIFormat::B8G8R8A8_UNORM : Target.Color[0].View.GetFormat();
	PipelineDesc.DepthFormat = ERHIFormat::D32_SFLOAT;
	PipelineDesc.bDepthTest = true;
	PipelineDesc.bDepthWrite = true;   // the demo triangle leaves this off; a mesh needs it

	for (const auto& [ActorId, Primitive] : Actors)
	{
		const FMeshBuffers* Mesh = EnsureMesh(R, Primitive.Mesh);
		if (Mesh == nullptr)
		{
			if (std::find(UnresolvedMeshes.begin(), UnresolvedMeshes.end(), Primitive.Mesh) == UnresolvedMeshes.end())
			{
				UnresolvedMeshes.push_back(Primitive.Mesh);
				MAHO_LOG_CORE_ERROR("MeshFeature: actor {} names mesh '{}', which has no provider -- skipped",
					ActorId, std::string(Primitive.Mesh.ToString()));
			}
			continue;
		}

		FMeshParameters* Params = R.AllocParameters<FMeshParameters>();
		if (Params == nullptr)
		{
			continue;
		}
		Params->ViewConstants = ViewBuffer;
		Params->Set1Placeholder = ViewBuffer;
		// No material at all: set2 gets the view buffer as a harmless valid buffer, and every sampler
		// slot the parameter struct declares gets the white texel. The slots must be FILLED rather than
		// left empty even though the built-in GLSL never declares set2 -- the typed AddPass writes every
		// resource member it sees, and a null view/sampler in a descriptor write is an invalid write.
		Params->MaterialParams = ViewBuffer;
		Params->MaterialTexture0 = WhiteTexture;
		Params->MaterialTexture1 = WhiteTexture;
		Params->MaterialTexture2 = WhiteTexture;
		Params->MaterialTexture3 = WhiteTexture;
		Params->MaterialSampler0 = WhiteSampler;
		Params->MaterialSampler1 = WhiteSampler;
		Params->MaterialSampler2 = WhiteSampler;
		Params->MaterialSampler3 = WhiteSampler;
		std::memcpy(Params->ObjectMatrix, Primitive.ObjectMatrix, sizeof(Primitive.ObjectMatrix));

		const FRDGBufferRef VertexRef = Mesh->Vertices;
		const FRDGBufferRef IndexRef = Mesh->Indices;
		const std::uint32_t IndexCount = Mesh->IndexCount;
		R.AddPass(ERHICommandListType::Graphics, PipelineDesc, Target, Params,
			[VertexRef, IndexRef, IndexCount, ViewBuffer, ViewConstants, TargetW, TargetH](FRHICommandList& Cmd)
			{
				Cmd.UpdateBuffer(ViewBuffer.GetRHI(), 0, sizeof(FViewConstants), &ViewConstants);
				Cmd.SetViewport(0.0f, 0.0f, static_cast<float>(TargetW), static_cast<float>(TargetH));
				Cmd.SetScissor(0, 0, TargetW, TargetH);
				Cmd.BindVertexBuffer(0, VertexRef.GetRHI(), 0);
				Cmd.BindIndexBuffer(IndexRef.GetRHI(), 0, true);
				Cmd.DrawIndexed(IndexCount, 1, 0, 0, 0);
			});
	}
}

void FMeshFeature::PreUnInstall(FRender& R, FRenderContext&)
{
	MAHO_TRACE_STAGE(IPreUnInstall, "Mesh feature teardown", "release the geometry buffers");

	// The geometry cache outlives individual actors (it is keyed by mesh name), so it is released
	// here, once -- not when the last actor using it disappears.
	for (auto& [MeshName, Buffers] : Meshes)
	{
		if (Buffers.Vertices.IsValid())
		{
			R.ReleaseBuffer(Buffers.Vertices);
		}
		if (Buffers.Indices.IsValid())
		{
			R.ReleaseBuffer(Buffers.Indices);
		}
		Buffers.IndexCount = 0;
	}
	Meshes.clear();
	Actors.clear();
	UnresolvedMeshes.clear();
	UnresolvedShaders.clear();
	ReportedMissingTextures.clear();
	// The pipeline cache holds `const FShaderDesc*`s that the ShaderLibrary owns: drop the entries here,
	// where the feature is still the one deciding, instead of leaving pointers that another layer's
	// teardown invalidates.
	Pipelines.clear();
	CubeMesh.reset();

	if (WhiteTextureStaging.IsValid())
	{
		R.ReleaseBuffer(WhiteTextureStaging);
	}
	if (WhiteTexture.IsValid())
	{
		R.ReleaseTexture(WhiteTexture);
	}
	WhiteSampler = nullptr;   // pool-owned; the pool dies with the render layer
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_MESHFEATURE_API Maho::FFrameExtension* CreateFrame()
{
	return Maho::FMeshFeature::CreateFrame();
}
