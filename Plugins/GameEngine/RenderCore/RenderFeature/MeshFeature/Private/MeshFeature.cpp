#include "MeshFeature.h"

#include <Log.h>
#include <Scene.h>
#include <ShaderParameterStruct.h>
#include <Trace.h>

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace Maho
{

namespace
{
	// View-projection comes from the per-frame uniform buffer; the object matrix from the push block.
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

	/** The pass' binding table: one uniform buffer (view-projection) + the per-object push block. */
	BEGIN_SHADER_PARAMETER_STRUCT(FMeshParameters)
		SHADER_PARAMETER_BUFFER(UniformBuffer, ViewConstants, 0, 0, ERHIShaderStage::Vertex,
			ERHIDescriptorType::UniformBuffer)
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

	/** The per-frame uniform block, matching the shader's `ViewConstants`. */
	struct FViewConstants
	{
		float ViewProj[16];
	};

	/** How many camera-less frames count as "the game never published one" instead of "this frame the
	 *  game side had not run yet" (the two collectors are not ordered, so frame 0 races). */
	constexpr std::uint32_t kNoCameraGraceFrames = 120;
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

	// The DEMO triangle (`FDrawTriangleFeature`) is a fullscreen triangle that writes NO depth, so the
	// two `IRender` passes are a plain painter's-order pair and the graph orders nothing: whoever
	// records second wins. Without this edge the mesh is covered by the triangle about half the runs
	// (the cube was gone in one screenshot and in front in the next -- same binary). Declared from the
	// side that knows it, BY NAME, so this feature keeps no build dependency on the demo plugin
	// (`FUIFeature` declares the same edge the same way).
	MyStage<IRender>().IsWaiting("FDrawTriangleFeature").ForStage<IRender>();
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

	std::shared_ptr<const std::vector<FRenderPrimitive>> Snapshot = Registry->ReadLatest();
	std::shared_ptr<const FRenderCamera> Camera = Registry->ReadLatestCamera();

	Scene::FScene* Scene = Scene::GetScene();
	if (Scene == nullptr || !Scene->GetSceneColor().IsValid())
	{
		return;   // scene target not ready this frame
	}

	const std::uint32_t TargetW = Scene->GetSceneColor().GetWidth();
	const std::uint32_t TargetH = Scene->GetSceneColor().GetHeight();
	const ERHIFormat ColorFormat = Scene->GetSceneColor().GetFormat();
	if (TargetW == 0 || TargetH == 0)
	{
		return;
	}

	SyncActors(Snapshot ? *Snapshot : std::vector<FRenderPrimitive>{});
	if (Actors.empty())
	{
		ReportRead(0, Camera != nullptr, Registry->GetPublishCount());
		return;   // the game side says there is nothing to draw (an empty list is not an error)
	}

	if (Camera == nullptr)
	{
		ReportRead(Actors.size(), false, Registry->GetPublishCount());
		// No camera -> NO draw (a zero/hardcoded camera would silently show the wrong thing). Reported
		// ONCE after a grace period: frame 0 legitimately races the game side, a camera that never
		// arrives (or disappears later) does not, and either way a per-frame log is not a report.
		++FramesWithoutCamera;
		if (!bReportedNoCamera && FramesWithoutCamera > kNoCameraGraceFrames)
		{
			bReportedNoCamera = true;
			MAHO_LOG_CORE_ERROR(
				"MeshFeature: {} actors and no camera published for {} frames -- skipping the draw "
				"(the game side must publish a camera; reported once)", Actors.size(), FramesWithoutCamera);
		}
		return;
	}
	// A camera arrived: the grace period restarts, and a LATER disappearance gets its own report.
	FramesWithoutCamera = 0;
	bReportedNoCamera = false;

	ReportRead(Actors.size(), true, Registry->GetPublishCount());

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
		MAHO_LOG_CORE_ERROR("MeshFeature: shader not ready");
		return;
	}

	// The camera's view-projection, straight from the snapshot -- no matrix math on this side.
	FViewConstants ViewConstants{};
	std::memcpy(ViewConstants.ViewProj, Camera->ViewProj, sizeof(ViewConstants.ViewProj));

	// The per-frame uniform buffer is TRANSIENT: a fresh slot each frame, recycled only after the
	// frame's fence, so writing it while the previous frame is still reading is safe by construction.
	FRHIBufferDesc UniformDesc;
	UniformDesc.Size = sizeof(FViewConstants);
	UniformDesc.Usage = ERHIBufferUsage::Uniform;
	UniformDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
	const FRDGBufferRef ViewBuffer = R.CreateBuffer(UniformDesc, ERDGResourceLifetime::Transient);
	if (!ViewBuffer.IsValid())
	{
		MAHO_LOG_CORE_ERROR("MeshFeature: view-constant buffer creation failed");
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
	// Attributes carry no binding index (binding 0 only) -- hence the interleaved vertex.
	PipelineDesc.Attributes = {
		{ 0, ERHIFormat::R32G32B32_SFLOAT, offsetof(FMeshVertex, X) },
		{ 1, ERHIFormat::R32G32B32_SFLOAT, offsetof(FMeshVertex, NX) },
		{ 2, ERHIFormat::R32G32_SFLOAT, offsetof(FMeshVertex, U) },
	};
	PipelineDesc.CullMode = ERHICullMode::None;
	PipelineDesc.FillMode = ERHIFillMode::Solid;
	PipelineDesc.ColorFormat = ColorFormat;
	PipelineDesc.DepthFormat = ERHIFormat::D32_SFLOAT;
	PipelineDesc.bDepthTest = true;
	PipelineDesc.bDepthWrite = true;   // the demo triangle leaves this off; a mesh needs it

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

		FMeshParameters* Params = R.AllocParameters<FMeshParameters>();
		if (Params == nullptr)
		{
			continue;
		}
		Params->ViewConstants = ViewBuffer;
		// The push block is filled BEFORE AddPass: the typed path packs it into the pipeline's push
		// range while it records, so writing it from inside the lambda below could be too late. One
		// AllocParameters call per actor -- the pool is a bump allocator, so each actor's matrix has
		// its own storage.
		std::memcpy(Params->ObjectMatrix, Primitive.ObjectMatrix, sizeof(Primitive.ObjectMatrix));

		// Buffers are captured BY VALUE (the lambda may run when the pass is submitted, not here), and
		// the view-constant upload rides in EVERY actor's list: passes record in parallel, so an
		// upload that only lived in one of them would not be ordered against the others. The value is
		// identical in each, so the redundancy is free of consequence.
		const FRDGBufferRef VertexRef = Mesh->Vertices;
		const FRDGBufferRef IndexRef = Mesh->Indices;
		const std::uint32_t IndexCount = Mesh->IndexCount;
		MAHO_TRACE_SECTION("record the mesh draw pass", nullptr);
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
	CubeMesh.reset();
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_MESHFEATURE_API Maho::FFrameExtension* CreateFrame()
{
	return Maho::FMeshFeature::CreateFrame();
}
