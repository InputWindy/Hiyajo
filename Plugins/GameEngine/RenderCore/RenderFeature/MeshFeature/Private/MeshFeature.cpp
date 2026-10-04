#include "MeshFeature.h"

#include <GameWorld.h>
#include <Log.h>
#include <Scene.h>
#include <ShaderParameterStruct.h>
#include <Trace.h>

#include <cmath>
#include <cstring>
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

	// -- matrices: column-major (GLSL mat4 memory order), M[column * 4 + row] ----------------

	void MakePerspective(float FovYRadians, float Aspect, float Near, float Far, float* M)
	{
		std::memset(M, 0, sizeof(float) * 16);
		const float F = 1.0f / std::tan(FovYRadians * 0.5f);
		M[0] = F / Aspect;
		// Vulkan clip space is Y-DOWN, so the world's +Y must be flipped here or the image is upside
		// down. Z maps to [0,1] (Vulkan), which is what picks this form over the GL [-1,1] one.
		M[5] = -F;
		M[10] = Far / (Near - Far);
		M[11] = -1.0f;
		M[14] = (Far * Near) / (Near - Far);
	}

	void MakeTranslation(float X, float Y, float Z, float* M)
	{
		std::memset(M, 0, sizeof(float) * 16);
		M[0] = M[5] = M[10] = M[15] = 1.0f;
		M[12] = X;
		M[13] = Y;
		M[14] = Z;
	}

	void MakeRotationY(float Angle, float* M)
	{
		const float C = std::cos(Angle);
		const float S = std::sin(Angle);
		std::memset(M, 0, sizeof(float) * 16);
		M[0] = C;
		M[2] = -S;
		M[5] = 1.0f;
		M[8] = S;
		M[10] = C;
		M[15] = 1.0f;
	}

	void MakeRotationX(float Angle, float* M)
	{
		const float C = std::cos(Angle);
		const float S = std::sin(Angle);
		std::memset(M, 0, sizeof(float) * 16);
		M[0] = 1.0f;
		M[5] = C;
		M[6] = S;
		M[9] = -S;
		M[10] = C;
		M[15] = 1.0f;
	}

	/** Out = A * B (both column-major). */
	void Multiply(const float* A, const float* B, float* Out)
	{
		float R[16] = {};
		for (int Col = 0; Col < 4; ++Col)
		{
			for (int Row = 0; Row < 4; ++Row)
			{
				float Sum = 0.0f;
				for (int K = 0; K < 4; ++K)
				{
					Sum += A[K * 4 + Row] * B[Col * 4 + K];
				}
				R[Col * 4 + Row] = Sum;
			}
		}
		std::memcpy(Out, R, sizeof(R));
	}

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
}

void FMeshFeature::InitViews(FRender& R, FRenderContext&)
{
	MAHO_TRACE_STAGE(IInitViews, "Mesh view init", "upload the cube's vertex/index buffers once");
	EnsureCubeBuffers(R);
}

void FMeshFeature::EnsureCubeBuffers(FRender& R)
{
	if (CubeVertices.IsValid() && CubeIndices.IsValid())
	{
		return;
	}

	std::vector<float> Positions;
	std::vector<float> Normals;
	std::vector<float> UVs;
	std::vector<std::uint32_t> Indices;
	BuildCubeSoA(Positions, Normals, UVs, Indices);
	if (CubeMesh == nullptr)
	{
		CubeMesh = std::make_unique<FCubeMesh>("Mesh.Cube", Positions, Normals, UVs, Indices);
	}

	// The geometry now comes from the ASSET: everything below is asset -> GPU. Interleaving happens
	// here because the RHI's vertex attributes carry no binding index (binding 0 is implicit), so an
	// SoA layout cannot be bound as separate streams.
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
	Indices = SrcIndices;

	// PERSISTENT, not Transient: these live for the feature's whole lifetime. A Transient handle is
	// frame-scoped by design (the pool advances its generation at every frame boundary), so holding
	// one across frames is a DETECTED error, not a slow path.
	FRHIBufferDesc VertexDesc;
	VertexDesc.Size = static_cast<std::uint64_t>(Vertices.size()) * sizeof(FMeshVertex);
	VertexDesc.Usage = ERHIBufferUsage::Vertex | ERHIBufferUsage::TransferDst;
	VertexDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
	CubeVertices = R.CreateBuffer(VertexDesc, ERDGResourceLifetime::Persistent);

	FRHIBufferDesc IndexDesc;
	IndexDesc.Size = static_cast<std::uint64_t>(Indices.size()) * sizeof(std::uint32_t);
	IndexDesc.Usage = ERHIBufferUsage::Index | ERHIBufferUsage::TransferDst;
	IndexDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
	CubeIndices = R.CreateBuffer(IndexDesc, ERDGResourceLifetime::Persistent);

	if (!CubeVertices.IsValid() || !CubeIndices.IsValid())
	{
		MAHO_LOG_CORE_ERROR("MeshFeature: cube buffer creation failed");
		return;
	}

	// One transfer pass, recorded off the frame graph's pass table: UpdateBuffer writes a host-visible
	// buffer directly, otherwise it stages the copy. It is submitted by the next IPresent, so the
	// buffers must be Persistent (see above) -- a frame-external pass outlives a Transient slot.
	const std::vector<FMeshVertex> VertexCopy = Vertices;
	const std::vector<std::uint32_t> IndexCopy = Indices;
	const FRDGBufferRef VertexRef = CubeVertices;
	const FRDGBufferRef IndexRef = CubeIndices;
	R.AddPass(ERHICommandListType::Graphics, [VertexRef, IndexRef, VertexCopy, IndexCopy](FRHICommandList& Cmd)
	{
		Cmd.UpdateBuffer(VertexRef.GetRHI(), 0,
			static_cast<std::uint64_t>(VertexCopy.size()) * sizeof(FMeshVertex), VertexCopy.data());
		Cmd.UpdateBuffer(IndexRef.GetRHI(), 0,
			static_cast<std::uint64_t>(IndexCopy.size()) * sizeof(std::uint32_t), IndexCopy.data());
	});

	CubeIndexCount = static_cast<std::uint32_t>(Indices.size());
	MAHO_LOG_CORE_INFO("MeshFeature: mesh '{}' uploaded ({} vertices, {} indices, stride {})",
		Mesh.GetPath(), static_cast<unsigned>(Vertices.size()), CubeIndexCount, sizeof(FMeshVertex));
}

void FMeshFeature::Render(FRender& R, FRenderContext&)
{
	MAHO_TRACE_STAGE(IRender, "Mesh pass", "upload the view constants and record the cube draw");

	Scene::FScene* Scene = Scene::GetScene();
	if (Scene == nullptr || !Scene->GetSceneColor().IsValid() || !CubeVertices.IsValid())
	{
		return;   // scene target or geometry not ready this frame
	}

	const std::uint32_t TargetW = Scene->GetSceneColor().GetWidth();
	const std::uint32_t TargetH = Scene->GetSceneColor().GetHeight();
	const ERHIFormat ColorFormat = Scene->GetSceneColor().GetFormat();
	if (TargetW == 0 || TargetH == 0)
	{
		return;
	}

	const float Delta = GameWorld::GetGameWorld() != nullptr
		? GameWorld::GetGameWorld()->GetDeltaSeconds()
		: 0.f;
	SpinAngle += Delta * 0.6f;

	// Camera: fixed eye pulled back along +Z looking down -Z, so the "view" is just a translation.
	// Perspective, not ortho -- this is the first place in the engine that needs one.
	FViewConstants ViewConstants{};
	float Projection[16] = {};
	float View[16] = {};
	MakePerspective(1.0471975512f /*60 deg*/, static_cast<float>(TargetW) / static_cast<float>(TargetH),
		0.1f, 100.0f, Projection);
	MakeTranslation(0.f, 0.f, -3.0f, View);
	Multiply(Projection, View, ViewConstants.ViewProj);

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

	// Per-object transform: rotate about Y (spin) with a fixed tilt about X, so all three axes of the
	// matrix are exercised and the spin is visible.
	float RotY[16] = {};
	float RotX[16] = {};
	float ObjectMatrix[16] = {};
	MakeRotationY(SpinAngle, RotY);
	MakeRotationX(0.42f, RotX);
	Multiply(RotY, RotX, ObjectMatrix);

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

	FMeshParameters* Params = R.AllocParameters<FMeshParameters>();
	Params->ViewConstants = ViewBuffer;
	// The push block is filled BEFORE AddPass: the typed path packs it into the pipeline's push range
	// while it records, so writing it from inside the lambda below could be too late.
	std::memcpy(Params->ObjectMatrix, ObjectMatrix, sizeof(ObjectMatrix));

	// Buffers are captured BY VALUE (the lambda may run when the pass is submitted, not here), and the
	// view-constant upload rides in the same list as the draw, so it is ordered before it.
	const FRDGBufferRef VertexRef = CubeVertices;
	const FRDGBufferRef IndexRef = CubeIndices;
	const std::uint32_t IndexCount = CubeIndexCount;
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

void FMeshFeature::PreUnInstall(FRender& R, FRenderContext&)
{
	MAHO_TRACE_STAGE(IPreUnInstall, "Mesh feature teardown", "release the geometry buffers");
	if (CubeVertices.IsValid())
	{
		R.ReleaseBuffer(CubeVertices);
	}
	if (CubeIndices.IsValid())
	{
		R.ReleaseBuffer(CubeIndices);
	}
	CubeIndexCount = 0;
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_MESHFEATURE_API Maho::FFrameExtension* CreateFrame()
{
	return Maho::FMeshFeature::CreateFrame();
}
