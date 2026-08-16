#include "Render/Forward/ForwardRendererFeature.h"
#include "Render/MahoCommonUniforms.h"

#include <Core/EngineBase.h>
#include <Core/Engine/Engine.h>
#include <Core/Misc/Log.h>
#include <Core/Misc/Utf8Path.h>
#include <ECS/World.h>
#include <ECS/Query.h>
#include "Game/Components/TransformComponent.h"
#include "Game/Components/CameraComponent.h"
#include <RDG/RDGBuilder.h>
#include <RenderSystem.h>
#include <Shader/ShaderCompiler.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

namespace Maho
{

namespace
{

/** Direct diagnostic log — bypasses spdlog so it always lands on disk. */
static void DebugLog(const char* Msg)
{
	static std::ofstream Out("C:/Users/luchunyi01/Desktop/forward_debug.log", std::ios::app);
	if (Out.is_open())
	{
		Out << Msg << std::endl;
		Out.flush();
	}
}

/** Minimal triangle (3 vertices) for the draw-path diagnostic. */
static const FGPUCubeVertex TriangleVertices[] =
{
	{ {  0.0f,  0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
	{ {  0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
	{ { -0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
};

static std::string LoadTextFile(const std::string& Path)
{
	std::ifstream In(Maho::PathFromUtf8(Path), std::ios::binary);
	if (!In)
	{
		return {};
	}
	std::ostringstream Ss;
	Ss << In.rdbuf();
	return Ss.str();
}

/** Gribb-Hartmann frustum plane extraction from ViewProj. */
static void ExtractFrustumPlanes(const float ViewProj[16], float OutPlanes[6][4])
{
	// ViewProj is column-major (M[col*4+row]).
	auto Row = [&](int R)
	{
		return std::array<float,4>{ ViewProj[R], ViewProj[4+R], ViewProj[8+R], ViewProj[12+R] };
	};
	std::array<float,4> R0 = Row(0), R1 = Row(1), R2 = Row(2), R3 = Row(3);
	auto Normalize = [](std::array<float,4>& P)
	{
		float L = std::sqrt(P[0]*P[0] + P[1]*P[1] + P[2]*P[2]);
		if (L > 1e-8f)
		{
			P[0] /= L;
			P[1] /= L;
			P[2] /= L;
			P[3] /= L;
		}
	};
	auto Add = [](const std::array<float,4>& A, const std::array<float,4>& B)
	{
		return std::array<float,4>{ A[0]+B[0], A[1]+B[1], A[2]+B[2], A[3]+B[3] };
	};
	auto Sub = [](const std::array<float,4>& A, const std::array<float,4>& B)
	{
		return std::array<float,4>{ A[0]-B[0], A[1]-B[1], A[2]-B[2], A[3]-B[3] };
	};

	std::array<float,4> P[6] = { Sub(R3,R0), Add(R3,R0), Sub(R3,R1), Add(R3,R1), Sub(R3,R2), Add(R3,R2) };
	for (int I = 0; I < 6; ++I)
	{
		Normalize(P[I]);
		OutPlanes[I][0] = P[I][0];
		OutPlanes[I][1] = P[I][1];
		OutPlanes[I][2] = P[I][2];
		OutPlanes[I][3] = P[I][3];
	}
}

} // namespace

void FForwardDrawContext::Gather(FWorld& World)
{
	Draws.clear();

	// Draw items: every entity with a transform, excluding the camera.
	{
		auto Query = World.Query<FTransformComponent>().Not<FCameraComponent>();
		Query.ForEach([this](FEntityHandle /*Handle*/, FTransformComponent& Transform)
		{
			Transform.ComputeLocalToWorld();
			FSceneDrawItem Item;
			Item.Type = EScenePrimitiveType::ColoredTriangle;
			std::memcpy(Item.LocalToWorld, glm::value_ptr(Transform.LocalToWorld), sizeof(Item.LocalToWorld));
			Draws.push_back(Item);
		});
	}

	// Camera: main camera entity.
	{
		auto Query = World.Query<FTransformComponent, FCameraComponent>();
		Query.ForEach([this](FEntityHandle /*Handle*/, FTransformComponent& CamTrans, const FCameraComponent& Cam)
		{
			if (!Cam.bMainCamera)
			{
				return;
			}

			CamTrans.ComputeLocalToWorld();

			Camera.FOV = Cam.FOV;
			Camera.NearPlane = Cam.NearPlane;
			Camera.FarPlane = Cam.FarPlane;
			Camera.AspectRatio = Cam.AspectRatio;
			Camera.bOrthographic = Cam.bOrthographic;
			Camera.OrthoSize = Cam.OrthoSize;

			const glm::mat4 View = glm::inverse(CamTrans.LocalToWorld);
			std::memcpy(Camera.View, glm::value_ptr(View), sizeof(Camera.View));
		});
	}
}

FForwardRendererFeature::FForwardRendererFeature()
	: TRenderFeatureWithContext("ForwardRenderer")
	, Ptr(std::make_unique<FImpl>())
{
}

FForwardRendererFeature::~FForwardRendererFeature() = default;

bool FForwardRendererFeature::OnRegister(FRenderSystem& RenderSystem)
{
	DebugLog("ForwardRenderer: OnRegister");
	if (!SetupPersistentResources(RenderSystem))
	{
		DebugLog("ForwardRenderer: persistent resource setup FAILED");
		MAHO_CORE_ERROR("ForwardRenderer: persistent resource setup failed");
		return false;
	}
	DebugLog("ForwardRenderer: persistent resources OK");
	MAHO_CORE_INFO("ForwardRenderer: registered (shader lazy)");
	return true;
}

void FForwardRendererFeature::OnUnregister(FRenderSystem& RenderSystem)
{
	auto& S = *Ptr;
	if (!S.bInitialized)
	{
		return;
	}

	if (S.GameViewImGuiTexture.IsValid())
	{
		RenderSystem.SetGameViewImGuiTexture({});
		RenderSystem.SetGameViewExtent(0, 0);
		RenderSystem.GetImGui().DestroyTexture(RenderSystem.GetRHIServer(), S.GameViewImGuiTexture);
	}

	IRHI* RHI = RenderSystem.GetRHIServer().GetRHI();
	if (!RHI)
	{
		S.bInitialized = false;
		return;
	}

	DestroyShaderResources();

	if (S.ViewportTexView)
	{
		RHI->DestroyTextureView(S.ViewportTexView);
		S.ViewportTexView = nullptr;
	}
	if (S.ViewportTex)
	{
		RHI->DestroyTexture(S.ViewportTex);
		S.ViewportTex = nullptr;
	}
	for (int Slot = 0; Slot < FrameRingSize; ++Slot)
	{
		if (S.FrameUniformBuf[Slot])
		{
			RHI->DestroyBuffer(S.FrameUniformBuf[Slot]);
			S.FrameUniformBuf[Slot] = nullptr;
		}
		if (S.ObjectUniformBuf[Slot])
		{
			RHI->DestroyBuffer(S.ObjectUniformBuf[Slot]);
			S.ObjectUniformBuf[Slot] = nullptr;
		}
	}
	if (S.CubeIBO)
	{
		RHI->DestroyBuffer(S.CubeIBO);
		S.CubeIBO = nullptr;
	}
	if (S.CubeVBO)
	{
		RHI->DestroyBuffer(S.CubeVBO);
		S.CubeVBO = nullptr;
	}

	S.bInitialized = false;
}

void FForwardRendererFeature::BuildCubeGeometry()
{
	auto& S = *Ptr;
	IRHI* RHI = S.RHI;

	{
		FRHIBufferDesc Desc;
		Desc.Size = sizeof(TriangleVertices);
		Desc.Usage = ERHIBufferUsage::Vertex;
		Desc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
		S.CubeVBO = RHI->CreateBuffer(Desc);
		RHI->UpdateBuffer(S.CubeVBO, 0, sizeof(TriangleVertices), TriangleVertices);
	}
}

bool FForwardRendererFeature::SetupPersistentResources(FRenderSystem& RenderSystem)
{
	auto& S = *Ptr;
	if (S.bInitialized)
	{
		return true;
	}

	S.RHI = RenderSystem.GetRHIServer().GetRHI();
	S.RenderSystem = &RenderSystem;
	if (!S.RHI)
	{
		return false;
	}

	BuildCubeGeometry();

	// ── Frame UBO + Object UBO (ring) ──
	for (int Slot = 0; Slot < FrameRingSize; ++Slot)
	{
		FRHIBufferDesc Desc;
		Desc.Size = sizeof(FFrameUniforms);
		Desc.Usage = ERHIBufferUsage::Uniform;
		Desc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
		S.FrameUniformBuf[Slot] = S.RHI->CreateBuffer(Desc);
	}
	for (int Slot = 0; Slot < FrameRingSize; ++Slot)
	{
		FRHIBufferDesc Desc;
		Desc.Size = sizeof(FObjectUniforms);
		Desc.Usage = ERHIBufferUsage::Uniform;
		Desc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
		S.ObjectUniformBuf[Slot] = S.RHI->CreateBuffer(Desc);

		FObjectUniforms Obj{};
		Obj.LocalToWorld[0] = Obj.LocalToWorld[5] = Obj.LocalToWorld[10] = Obj.LocalToWorld[15] = 1.0f;
		Obj.LocalToWorldInverseTranspose[0] = Obj.LocalToWorldInverseTranspose[5] = Obj.LocalToWorldInverseTranspose[10] = Obj.LocalToWorldInverseTranspose[15] = 1.0f;
		S.RHI->UpdateBuffer(S.ObjectUniformBuf[Slot], 0, sizeof(FObjectUniforms), &Obj);
	}

	// ── Viewport texture ──
	{
		FRHITextureDesc Desc;
		Desc.Format = ERHIFormat::B8G8R8A8_UNORM;
		Desc.Dimension = ERHITextureDimension::Tex2D;
		Desc.Extent = { S.VpWidth, S.VpHeight, 1 };
		Desc.Usage = ERHITextureUsage::ColorAttachment | ERHITextureUsage::Sampled;
		S.ViewportTex = S.RHI->CreateTexture(Desc);

		FRHITextureViewDesc ViewDesc;
		ViewDesc.Texture = S.ViewportTex;
		ViewDesc.Format = ERHIFormat::B8G8R8A8_UNORM;
		S.ViewportTexView = S.RHI->CreateTextureView(ViewDesc);
	}

	if (RenderSystem.GetImGui().IsInitialized())
	{
		FImGuiTextureHandle Handle;
		if (RenderSystem.GetImGui().RegisterExternalSampledTexture(
			RenderSystem.GetRHIServer(), S.ViewportTexView, Handle))
		{
			S.GameViewImGuiTexture = Handle;
			RenderSystem.SetGameViewImGuiTexture(Handle);
			RenderSystem.SetGameViewExtent(S.VpWidth, S.VpHeight);
		}
	}

	S.bInitialized = true;
	MAHO_CORE_INFO("ForwardRenderer: persistent resources ready ({}x{})", S.VpWidth, S.VpHeight);
	return true;
}

bool FForwardRendererFeature::EnsureShaderReady()
{
	auto& S = *Ptr;
	if (S.bShaderReady)
	{
		return true;
	}
	if (!S.RHI || !GEngine)
	{
		DebugLog("ForwardRenderer: EnsureShaderReady early-out (no RHI/GEngine)");
		return false;
	}
	DebugLog("ForwardRenderer: EnsureShaderReady compiling shaders...");

	if (!FShaderCompiler::Initialize())
	{
		MAHO_CORE_ERROR("ForwardRenderer: shader compiler init failed");
		return false;
	}

	DestroyShaderResources();

	const FConfig& Config = GEngine->GetConfig();

	// ── Compile vertex shader ──
	const std::string VertSrc = LoadTextFile(Config.ProjectShadersDir + "/Forward/Forward.vert");
	if (VertSrc.empty())
	{
		MAHO_CORE_ERROR("ForwardRenderer: missing Forward/Forward.vert");
		return false;
	}
	FShaderCompileDesc VertDesc;
	VertDesc.Source = VertSrc;
	FShaderCompileResult VertResult = FShaderCompiler::CompileStage(VertDesc, ERHIShaderStage::Vertex, "main");
	if (!VertResult.bSuccess)
	{
		DebugLog((std::string("ForwardRenderer: vert compile FAILED: ") + VertResult.ErrorLog).c_str());
		MAHO_CORE_ERROR("ForwardRenderer: vert shader compile failed: {}", VertResult.ErrorLog);
		return false;
	}
	DebugLog("ForwardRenderer: vert compile OK");

	// ── Compile fragment shader ──
	const std::string FragSrc = LoadTextFile(Config.ProjectShadersDir + "/Forward/Forward.frag");
	if (FragSrc.empty())
	{
		MAHO_CORE_ERROR("ForwardRenderer: missing Forward/Forward.frag");
		return false;
	}
	FShaderCompileDesc FragDesc;
	FragDesc.Source = FragSrc;
	FShaderCompileResult FragResult = FShaderCompiler::CompileStage(FragDesc, ERHIShaderStage::Fragment, "main");
	if (!FragResult.bSuccess)
	{
		DebugLog((std::string("ForwardRenderer: frag compile FAILED: ") + FragResult.ErrorLog).c_str());
		MAHO_CORE_ERROR("ForwardRenderer: frag shader compile failed: {}", FragResult.ErrorLog);
		return false;
	}
	DebugLog("ForwardRenderer: frag compile OK");

	IRHI* RHI = S.RHI;

	// ── Shader modules ──
	FRHIShaderModuleDesc VsDesc;
	VsDesc.Stage = ERHIShaderStage::Vertex;
	VsDesc.Bytecode = VertResult.Bytecode.data();
	VsDesc.BytecodeSize = VertResult.Bytecode.size() * sizeof(std::uint32_t);
	FRHIShaderModule* VsModule = RHI->CreateShaderModule(VsDesc);

	FRHIShaderModuleDesc FsDesc;
	FsDesc.Stage = ERHIShaderStage::Fragment;
	FsDesc.Bytecode = FragResult.Bytecode.data();
	FsDesc.BytecodeSize = FragResult.Bytecode.size() * sizeof(std::uint32_t);
	FRHIShaderModule* FsModule = RHI->CreateShaderModule(FsDesc);

	// ── Descriptor set layouts ──
	FRHIDescriptorSetLayout* ObjectSetLayout;
	FRHIDescriptorSetLayout* FrameSetLayout;
	{
		FRHIDescriptorSetLayoutDesc Desc;
		FRHIDescriptorBinding B0;
		B0.Binding = 0; B0.Type = ERHIDescriptorType::UniformBuffer; B0.Count = 1; B0.Stages = ERHIShaderStage::Vertex;
		Desc.Bindings.push_back(B0);
		ObjectSetLayout = RHI->CreateDescriptorSetLayout(Desc);
	}
	{
		FRHIDescriptorSetLayoutDesc Desc;
		FRHIDescriptorBinding B0;
		B0.Binding = 0; B0.Type = ERHIDescriptorType::UniformBuffer; B0.Count = 1; B0.Stages = ERHIShaderStage::AllGraphics;
		Desc.Bindings.push_back(B0);
		FrameSetLayout = RHI->CreateDescriptorSetLayout(Desc);
	}

	// ── Pipeline layout ──
	{
		FRHIPipelineLayoutDesc Desc;
		Desc.SetLayouts = { FrameSetLayout, ObjectSetLayout };
		S.DrawLayout = RHI->CreatePipelineLayout(Desc);
	}

	// ── Graphics pipeline (forward) ──
	{
		FRHIGraphicsPipelineDesc Desc;
		Desc.VertexShader = VsModule;
		Desc.FragmentShader = FsModule;
		Desc.Layout = S.DrawLayout;
		Desc.Topology = ERHIPrimitiveTopology::TriangleList;
		Desc.VertexStride = sizeof(FGPUCubeVertex);
		Desc.CullMode = ERHICullMode::None;   // TEMP: disable culling — winding may be flipped

		FRHIVertexAttribute Pos;
		Pos.Location = 0; Pos.Format = ERHIFormat::R32G32B32_SFLOAT; Pos.Offset = 0;
		Desc.Attributes.push_back(Pos);
		FRHIVertexAttribute Col;
		Col.Location = 5; Col.Format = ERHIFormat::R32G32B32_SFLOAT; Col.Offset = 12;
		Desc.Attributes.push_back(Col);

		Desc.ColorFormat = ERHIFormat::B8G8R8A8_UNORM;
		Desc.SampleCount = 1;
		S.DrawPipeline = RHI->CreateGraphicsPipeline(Desc);
	}

	// ── Descriptor pool + sets (ring: one set per frame slot) ──
	{
		FRHIDescriptorPoolDesc Desc;
		Desc.MaxSets = 2 * FrameRingSize;
		FRHIDescriptorPoolSize Sz; Sz.Type = ERHIDescriptorType::UniformBuffer; Sz.Count = 2 * FrameRingSize;
		Desc.PoolSizes.push_back(Sz);
		S.DescPool = RHI->CreateDescriptorPool(Desc);
	}
	for (int Slot = 0; Slot < FrameRingSize; ++Slot)
	{
		S.DrawDescSet[Slot] = RHI->AllocateDescriptorSet(S.DescPool, ObjectSetLayout);
		S.FrameDescSet[Slot] = RHI->AllocateDescriptorSet(S.DescPool, FrameSetLayout);

		{
			FRHIDescriptorWrite W{};
			W.Set = S.DrawDescSet[Slot]; W.Binding = 0; W.Type = ERHIDescriptorType::UniformBuffer;
			W.Buffer = S.ObjectUniformBuf[Slot]; W.Range = sizeof(FObjectUniforms);
			RHI->UpdateDescriptorSets(&W, 1);
		}
		{
			FRHIDescriptorWrite W{};
			W.Set = S.FrameDescSet[Slot]; W.Binding = 0; W.Type = ERHIDescriptorType::UniformBuffer;
			W.Buffer = S.FrameUniformBuf[Slot]; W.Range = sizeof(FFrameUniforms);
			RHI->UpdateDescriptorSets(&W, 1);
		}
	}

	S.bShaderReady = true;
	DebugLog("ForwardRenderer: shaders + pipelines READY");
	DebugLog((std::string("ForwardRenderer: DrawPipeline=") + (S.DrawPipeline ? "OK" : "NULL") + " VBO=" + (S.CubeVBO ? "OK" : "NULL") + " IBO=" + (S.CubeIBO ? "OK" : "NULL") + " DrawDescSet[0]=" + (S.DrawDescSet[0] ? "OK" : "NULL") + " FrameDescSet[0]=" + (S.FrameDescSet[0] ? "OK" : "NULL")).c_str());
	MAHO_CORE_INFO("ForwardRenderer: shaders compiled + pipelines ready");
	return true;
}

void FForwardRendererFeature::DestroyShaderResources()
{
	auto& S = *Ptr;
	IRHI* RHI = S.RHI;
	if (!RHI)
	{
		return;
	}

	if (S.DescPool)
	{
		RHI->DestroyDescriptorPool(S.DescPool);
		S.DescPool = nullptr;
	}
	for (int Slot = 0; Slot < FrameRingSize; ++Slot)
	{
		S.DrawDescSet[Slot] = nullptr;
		S.FrameDescSet[Slot] = nullptr;
	}
	if (S.DrawPipeline)
	{
		RHI->DestroyGraphicsPipeline(S.DrawPipeline);
		S.DrawPipeline = nullptr;
	}
	if (S.DrawLayout)
	{
		RHI->DestroyPipelineLayout(S.DrawLayout);
		S.DrawLayout = nullptr;
	}
	S.bShaderReady = false;
}

void FForwardRendererFeature::ComputeFrustumPlanes(const FCameraFrameData& Camera, float Aspect, FGPUCullParams& OutParams)
{
	float FOV = Camera.FOV;
	float Near = Camera.NearPlane;
	float Far = Camera.FarPlane;
	float F = 1.0f / std::tan((FOV * 0.5f) * 3.14159265f / 180.0f);
	float Proj[16] = {
		F / Aspect, 0, 0, 0,
		0, -F, 0, 0,
		0, 0, Far / (Near - Far), -1,
		0, 0, Far * Near / (Near - Far), 0,
	};
	float ViewProj[16];
	for (int Col = 0; Col < 4; ++Col)
		for (int Row = 0; Row < 4; ++Row)
		{
			float Sum = 0.0f;
			for (int K = 0; K < 4; ++K)
				Sum += Proj[K * 4 + Row] * Camera.View[Col * 4 + K];
			ViewProj[Col * 4 + Row] = Sum;
		}
	ExtractFrustumPlanes(ViewProj, OutParams.FrustumPlanes);
}

void FForwardRendererFeature::ExecuteStage(ERenderPipelineStage Stage, const FForwardDrawContext& Context, FFrameContext& FrameCtx, FRDGBuilder& GB)
{
	if (!Ptr->bInitialized)
	{
		return;
	}

	switch (Stage)
	{
	case ERenderPipelineStage::BeginFrame: DebugLog("ForwardRenderer: stage BeginFrame"); ExecuteBeginFrame(Context, FrameCtx, GB); break;
	case ERenderPipelineStage::BasePass:   DebugLog("ForwardRenderer: stage BasePass");   ExecuteBasePass(Context, FrameCtx, GB);   break;
	default: break;
	}
}

void FForwardRendererFeature::ExecuteBeginFrame(const FForwardDrawContext& Context, FFrameContext& FrameCtx, FRDGBuilder& GB)
{
	auto& S = *Ptr;
	if (!S.RenderSystem)
	{
		return;
	}
	if (!EnsureShaderReady())
	{
		return;
	}

	const std::uint32_t Slot = static_cast<std::uint32_t>(FrameCtx.FrameIndex % FrameRingSize);

	const auto& Scene = Context;
	if (Scene.Draws.empty())
	{
		return;
	}

	// ── Frame uniforms ──
	FFrameUniforms FrameUni{};
	std::memcpy(FrameUni.View, Scene.Camera.View, sizeof(FrameUni.View));
	{
		float F = 1.0f / std::tan((Scene.Camera.FOV * 0.5f) * 3.14159265f / 180.0f);
		float A = Scene.Camera.AspectRatio, N = Scene.Camera.NearPlane, Ff = Scene.Camera.FarPlane;
		// Vulkan clip space: depth [0,1] ([3][2]=-1, [2][2]=Ff/(N-Ff)), y-down ([1][1]=-F).
		float P[16] = { F/A, 0, 0, 0,  0, -F, 0, 0,  0, 0, Ff/(N-Ff), -1,  0, 0, Ff*N/(N-Ff), 0 };
		std::memcpy(FrameUni.Proj, P, sizeof(P));
		for (int Col = 0; Col < 4; ++Col)
			for (int Row = 0; Row < 4; ++Row)
			{
				float Sum = 0.0f;
				for (int K = 0; K < 4; ++K)
					Sum += FrameUni.Proj[K*4+Row] * FrameUni.View[Col*4+K];
				FrameUni.ViewProj[Col*4+Row] = Sum;
			}
	}

	FRDGBuffer* RDGFrameUBO = GB.RegisterExternalBuffer(S.FrameUniformBuf[Slot], ERHIResourceState::Common, "FrameUBO");
	GB.UploadBuffer(RDGFrameUBO, &FrameUni, sizeof(FrameUni));
}

void FForwardRendererFeature::ExecuteBasePass(const FForwardDrawContext& Context, FFrameContext& FrameCtx, FRDGBuilder& GB)
{
	auto& S = *Ptr;
	if (!S.RenderSystem)
	{
		return;
	}
	if (!EnsureShaderReady())
	{
		return;
	}

	const std::uint32_t Slot = static_cast<std::uint32_t>(FrameCtx.FrameIndex % FrameRingSize);

	const auto& Scene = Context;
	if (Scene.Draws.empty())
	{
		DebugLog("ForwardRenderer: BasePass — scene has NO draws");
		return;
	}

	// ── RDG resources (ring slot) ──
	FRDGBuffer* RDGFrameUBO = GB.RegisterExternalBuffer(S.FrameUniformBuf[Slot], ERHIResourceState::Common, "FrameUBO");
	FRDGTexture* RDGViewport = GB.RegisterExternalTexture(S.ViewportTex, ERHIResourceState::ShaderResource, "ViewportTex");

	// ── Raster pass: conventional draw ──
	{
		auto& Params = GB.AllocateParameters();
		Params.Reads = {
			{ RDGFrameUBO, ERHIResourceState::UniformBuffer },
		};
		Params.Writes = { { RDGViewport, ERHIResourceState::RenderTarget } };
		FRDGPassParameters::FRenderTargetBinding RT{};
		RT.Texture = RDGViewport;
		RT.View = S.ViewportTexView;
		RT.LoadOp = ERHILoadOp::Clear;
		RT.StoreOp = ERHIStoreOp::Store;
		RT.ClearColor[0] = 0.0f;   // TEMP diagnostic: bright green — composite works if visible
		RT.ClearColor[1] = 1.0f;
		RT.ClearColor[2] = 0.0f;
		RT.ClearColor[3] = 1.0f;
		Params.RenderTargets = { RT };

		GB.AddRasterPass("ForwardDraw", Params,
			[&S, Slot](FRHICommandList& Cmd) mutable
			{
				DebugLog("ForwardRenderer: draw lambda executing");
				Cmd.BindGraphicsPipeline(S.DrawPipeline);
				FRHIDescriptorSet* Sets[] = { S.FrameDescSet[Slot], S.DrawDescSet[Slot] };
				Cmd.BindDescriptorSets(0, Sets, 2);
			Cmd.BindVertexBuffer(0, S.CubeVBO);
			Cmd.SetViewport(0.0f, 0.0f, static_cast<float>(S.VpWidth), static_cast<float>(S.VpHeight));
			Cmd.SetScissor(0, 0, S.VpWidth, S.VpHeight);
			Cmd.Draw(3, 1, 0, 0);
			});
	}
}

} // namespace Maho
