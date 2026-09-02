#include "UIFeature.h"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <DrawTriangleFeature.h>
#include <Frame.h>
#include <Log.h>
#include <Scene.h>
#include <RHI/RHICommandList.h>
#include <RHI/RHIResources.h>

#include "imgui.h"

namespace Maho
{

namespace
{
	constexpr const char* kImGuiVertShader = R"(
#version 460
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(push_constant) uniform Push { mat4 Proj; } PC;
layout(location = 0) out vec2 FragUV;
layout(location = 1) out vec4 FragColor;
void main()
{
	FragUV = aUV;
	FragColor = aColor;
	gl_Position = PC.Proj * vec4(aPos, 0.0, 1.0);
}
)";

	constexpr const char* kImGuiFragShader = R"(
#version 460
layout(location = 0) in vec2 FragUV;
layout(location = 1) in vec4 FragColor;
layout(set = 0, binding = 0) uniform sampler2D FontTex;
layout(location = 0) out vec4 OutColor;
void main()
{
	OutColor = FragColor * texture(FontTex, FragUV);
}
)";

} // namespace

struct FUIFeature::FData
{
	bool bInitialized = false;
	bool bFontUploaded = false;
	IRHI* RHIPtr = nullptr;

	// Font texture + sampling (texture/buffer are pooled FRDG resources; the view
	// comes from FontTexture.GetView(), no separate native view is held).
	FRDGTextureRef FontTexture;
	FRHISampler* FontSampler = nullptr;
	FRHIDescriptorSetLayout* FontSetLayout = nullptr;   // cached by the pool (get-or-create)
	FRHIDescriptorPool* FontDescriptorPool = nullptr;
	FRHIDescriptorSet* FontDescriptorSet = nullptr;
	FRDGBufferRef FontStaging;

	// Shaders compiled ONCE into SPIR-V bytecode (+ content hashes). Each frame a
	// FRESH transient shader module is built from this bytecode so the pipeline and
	// its layout can be fetched from FRender's PSO cache -- this feature holds NO
	// raw RHI handle for the pipeline/layout (the pool owns their lifetime). The
	// descriptor set layout IS kept as a borrowed handle: the font descriptor pool
	// allocates against it, and the (cached) pipeline layout references it.
	std::vector<std::uint32_t> VBytecode;
	std::vector<std::uint32_t> FBytecode;
	std::uint64_t VHash = 0;
	std::uint64_t FHash = 0;

	// Per-frame vertex/index upload -- pooled transient buffers, recreated each
	// frame (the pool destroys them at the next BeginFrame fence wait and recycles
	// the slots, so the UI feature never touches native RHI buffers).
	FRDGBufferRef VertexBuffer;
	FRDGBufferRef IndexBuffer;

	void DestroyResources()
	{
		if (RHIPtr == nullptr)
		{
			return;
		}
		// FontDescriptorPool + FontSampler are feature-owned (the pool's PSO cache
		// owns the pipeline + pipeline layout + descriptor set layout, and the
		// pooled FRDG transient/persistent textures/buffers are released by FRender).
		// Destroy the descriptor pool before the sampler it referenced; the cached
		// set layout is still alive (pool Shutdown destroys it later -- after the
		// pipeline layouts that referenced it).
		if (FontDescriptorPool) { RHIPtr->DestroyDescriptorPool(FontDescriptorPool); FontDescriptorPool = nullptr; }
		if (FontSampler)        { RHIPtr->DestroySampler(FontSampler);               FontSampler = nullptr; }
	}
};

FUIFeature::FUIFeature()
{
	Data = std::make_unique<FData>();

	// UI draws + submits LAST -- after every scene render feature's IEndRender
	// (their submits reach the queue first), so the UI composites over the scene.
	MyStage<IRenderUI>().IsWaiting<Scene::FScene>().ForStage<IEndRender>();
	MyStage<IRenderUI>().IsWaiting<FDrawTriangleFeature>().ForStage<IEndRender>();
	// (FFrame additionally declares IPresent waits for my IRenderUI.)
}

FUIFeature::~FUIFeature()
{
	if (Data != nullptr)
	{
		Data->DestroyResources();
	}
}

void FUIFeature::InitViews(FRender& R)
{
	// CPU-side data integration (UE InitViews analogue) now lives in the UI
	// engine layer's ITick, which produces the draw data FScene receives via the
	// sink. This stage stays as a graph no-op so the feature's stage list and the
	// render-graph topology are unchanged.
	(void)R;
}

bool FUIFeature::EnsureBackend(FRender& R, FRHICommandList& Cmd)
{
	if (Data->bInitialized)
	{
		return true;
	}
	IRHI* RHIPtr = R.GetRHI();
	if (RHIPtr == nullptr)
	{
		return false;
	}
	Data->RHIPtr = RHIPtr;

	// -- shaders (the engine's GLSL -> SPIR-V compiler) -- compiled once into
	// bytecode + content hash. The shader MODULE itself is transient (rebuilt fresh
	// each frame so the PSO cache matches on shader CONTENT); only bytecode/hash
	// are cached here.
	FShaderCompileDesc VDesc;
	VDesc.Source = kImGuiVertShader;
	VDesc.Stage = ERHIShaderStage::Vertex;
	VDesc.EntryPoint = "main";
	FShaderCompileResult VResult = FShaderCompilerServer::CompileStage(VDesc);
	if (!VResult.bSuccess)
	{
		MAHO_LOG_CORE_ERROR("FUIFeature: VS compile failed: {}", VResult.ErrorLog);
		return false;
	}
	Data->VBytecode = VResult.Bytecode;
	Data->VHash = HashShaderWords(Data->VBytecode.data(), Data->VBytecode.size());

	FShaderCompileDesc FDesc;
	FDesc.Source = kImGuiFragShader;
	FDesc.Stage = ERHIShaderStage::Fragment;
	FDesc.EntryPoint = "main";
	FShaderCompileResult FResult = FShaderCompilerServer::CompileStage(FDesc);
	if (!FResult.bSuccess)
	{
		MAHO_LOG_CORE_ERROR("FUIFeature: FS compile failed: {}", FResult.ErrorLog);
		return false;
	}
	Data->FBytecode = FResult.Bytecode;
	Data->FHash = HashShaderWords(Data->FBytecode.data(), Data->FBytecode.size());

	// -- descriptor set layout for the font texture (set 0, binding 0) --
	// Cached (get-or-create) by the pool; kept as a borrowed handle because the
	// font descriptor pool allocates against it and the (cached) pipeline layout
	// references it.
	FRHIDescriptorBinding FontBinding;
	FontBinding.Binding = 0;
	FontBinding.Type = ERHIDescriptorType::CombinedImageSampler;
	FontBinding.Count = 1;
	FontBinding.Stages = ERHIShaderStage::Fragment;
	FRHIDescriptorSetLayoutDesc FontSetDesc;
	FontSetDesc.Bindings.push_back(FontBinding);
	Data->FontSetLayout = R.GetOrCreateDescriptorSetLayout(FontSetDesc);
	if (Data->FontSetLayout == nullptr)
	{
		MAHO_LOG_CORE_ERROR("FUIFeature: descriptor set layout failed");
		return false;
	}

	// -- font texture + sampler + descriptor set --
	ImFontAtlas* Fonts = ImGui::GetIO().Fonts;
	unsigned char* Pixels = nullptr;
	int FontW = 0, FontH = 0, FontBpp = 0;
	Fonts->GetTexDataAsRGBA32(&Pixels, &FontW, &FontH, &FontBpp);
	if (Pixels == nullptr || FontW <= 0 || FontH <= 0)
	{
		MAHO_LOG_CORE_ERROR("FUIFeature: no font atlas");
		return false;
	}
	FRHITextureDesc TexDesc;
	TexDesc.Format = ERHIFormat::R8G8B8A8_UNORM;
	TexDesc.Dimension = ERHITextureDimension::Tex2D;
	TexDesc.Extent = { static_cast<std::uint32_t>(FontW), static_cast<std::uint32_t>(FontH), 1 };
	TexDesc.MipLevels = 1;
	TexDesc.ArrayLayers = 1;
	TexDesc.Usage = ERHITextureUsage::Sampled | ERHITextureUsage::TransferDst;
	TexDesc.MemoryUsage = ERHIMemoryUsage::GPUOnly;
	// Pooled persistent font texture (the pool owns the native + view lifetime).
	Data->FontTexture = R.CreateTexture(TexDesc, ERDGResourceLifetime::Persistent);
	FRHISamplerDesc SamplerDesc;
	SamplerDesc.AddressU = ERHIAddressMode::ClampToEdge;
	SamplerDesc.AddressV = ERHIAddressMode::ClampToEdge;
	SamplerDesc.AddressW = ERHIAddressMode::ClampToEdge;
	Data->FontSampler = RHIPtr->CreateSampler(SamplerDesc);
	FRHIDescriptorPoolDesc PoolDesc;
	PoolDesc.MaxSets = 1;
	PoolDesc.PoolSizes = { { ERHIDescriptorType::CombinedImageSampler, 1 } };
	Data->FontDescriptorPool = RHIPtr->CreateDescriptorPool(PoolDesc);
	Data->FontDescriptorSet = RHIPtr->AllocateDescriptorSet(Data->FontDescriptorPool, Data->FontSetLayout);
	FRHIDescriptorWrite FontWrite;
	FontWrite.Set = Data->FontDescriptorSet;
	FontWrite.Binding = 0;
	FontWrite.Type = ERHIDescriptorType::CombinedImageSampler;
	FontWrite.TextureView = Data->FontTexture.GetView();
	FontWrite.Sampler = Data->FontSampler;
	// Immediate CPU op (vkUpdateDescriptorSets) -- record it on the pass command list.
	Cmd.UpdateDescriptorSets(&FontWrite, 1);
	// Staging buffer for the font pixels (CPUToGPU, filled via UpdateBuffer).
	FRHIBufferDesc StagingDesc;
	StagingDesc.Size = static_cast<std::uint64_t>(FontW) * FontH * 4;
	StagingDesc.Usage = ERHIBufferUsage::TransferSrc;
	StagingDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
	// Pooled transient staging buffer -- the pool destroys it after the upload's
	// fence wait (one frame), so no native buffer is held by the feature.
	Data->FontStaging = R.CreateBuffer(StagingDesc, ERDGResourceLifetime::Transient);

	// The ImGui font texture id (ImTextureID) is the descriptor set handle.
	Fonts->TexID = reinterpret_cast<ImTextureID>(Data->FontDescriptorSet);

	// The pipeline + its layout are NOT built here -- each frame a fresh transient
	// shader module is created from the cached bytecode and the pipeline is fetched
	// from the PSO cache (see RenderUI). This feature never holds a raw
	// pipeline/layout handle.

	Data->bInitialized = true;
	MAHO_LOG_CORE_INFO("FUIFeature: custom FRHI backend ready (font; pipeline fetched per-frame from cache)");
	return true;
}

void FUIFeature::RenderUI(FRender& R)
{
	Scene::FScene* Scene = Scene::GetScene();
	if (Scene == nullptr || !Scene->GetSceneColor().IsValid())
	{
		return;
	}
	// The UI composites over SceneColor; its color format + size come FROM THE
	// TARGET (never the swapchain / RHI), matching the off-screen target's desc.
	const FRDGTextureRef SceneColor = Scene->GetSceneColor();
	const std::uint32_t TargetW = SceneColor.GetWidth();
	const std::uint32_t TargetH = SceneColor.GetHeight();
	const ERHIFormat ColorFormat = SceneColor.GetFormat();

	ImDrawData* DrawData = static_cast<ImDrawData*>(Scene->GetImGuiDrawData());
	if (DrawData == nullptr || !DrawData->Valid || DrawData->CmdListsCount <= 0)
	{
		MAHO_LOG_CORE_INFO("FUIFeature: no draw data (dd={} valid={} lists={})",
			DrawData != nullptr, DrawData != nullptr && DrawData->Valid,
			DrawData != nullptr ? DrawData->CmdListsCount : -1);
		return;
	}

	// Record + submit the UI draw in one pass. AddPass acquires the list, Begin/End
	// it, and submits at this call site (so the UI is one "draw the UI" IRenderUI
	// step on the graph). EnsureBackend + font upload + buffer fill + draw all run
	// inside the lambda, which receives the list mid-pass.
	R.AddPass(ERHICommandListType::Graphics, [&](FRHICommandList& Cmd)
	{
		if (!EnsureBackend(R, Cmd))
		{
			return;
		}

		// Each frame: fetch the vertex/fragment shader modules from the PSO cache
		// (the pool owns the natives and matches them by bytecode CONTENT, so every
		// pass built from these bytes shares one module). This feature holds no raw
		// RHI handle for the module, layout, or pipeline -- the pool owns their
		// lifetimes.
		FRHIShaderModuleDesc VSDesc;
		VSDesc.Stage = ERHIShaderStage::Vertex;
		VSDesc.Bytecode = Data->VBytecode.data();
		VSDesc.BytecodeSize = Data->VBytecode.size() * sizeof(std::uint32_t);
		VSDesc.EntryPoint = "main";
		FRHIShaderModule* VS = R.GetOrCreateShaderModule(VSDesc);
		FRHIShaderModuleDesc FSDesc;
		FSDesc.Stage = ERHIShaderStage::Fragment;
		FSDesc.Bytecode = Data->FBytecode.data();
		FSDesc.BytecodeSize = Data->FBytecode.size() * sizeof(std::uint32_t);
		FSDesc.EntryPoint = "main";
		FRHIShaderModule* FS = R.GetOrCreateShaderModule(FSDesc);
		if (VS == nullptr || FS == nullptr)
		{
			MAHO_LOG_CORE_ERROR("FUIFeature: GetOrCreateShaderModule failed");
			return;
		}

		FRHIPushConstantRange PushRange;
		PushRange.Stages = ERHIShaderStage::Vertex;
		PushRange.Offset = 0;
		PushRange.Size = 64;   // mat4
		FRHIPipelineLayoutDesc LayoutDesc;
		LayoutDesc.SetLayouts.push_back(Data->FontSetLayout);
		LayoutDesc.PushConstants.push_back(PushRange);
		FRHIPipelineLayout* PipelineLayout = R.GetOrCreatePipelineLayout(LayoutDesc);

		FRHIGraphicsPipelineDesc PipelineDesc;
		PipelineDesc.VertexShader = VS;
		PipelineDesc.FragmentShader = FS;
		PipelineDesc.VertexShaderHash = Data->VHash;
		PipelineDesc.FragmentShaderHash = Data->FHash;
		PipelineDesc.VertexEntryPoint = "main";
		PipelineDesc.FragmentEntryPoint = "main";
		PipelineDesc.Layout = PipelineLayout;
		PipelineDesc.RenderPass = nullptr;   // dynamic rendering
		PipelineDesc.Topology = ERHIPrimitiveTopology::TriangleList;
		PipelineDesc.VertexStride = sizeof(ImDrawVert);
		PipelineDesc.Attributes = {
			{ 0, ERHIFormat::R32G32_SFLOAT,  offsetof(ImDrawVert, pos) },   // aPos
			{ 1, ERHIFormat::R32G32_SFLOAT,  offsetof(ImDrawVert, uv) },    // aUV
			{ 2, ERHIFormat::R8G8B8A8_UNORM, offsetof(ImDrawVert, col) },   // aColor
		};
		PipelineDesc.CullMode = ERHICullMode::None;
		PipelineDesc.FillMode = ERHIFillMode::Solid;
		PipelineDesc.ColorFormat = ColorFormat;
		PipelineDesc.DepthFormat = ERHIFormat::Unknown;
		FRHIAttachmentBlend Blend;
		Blend.bBlend = true;
		Blend.SrcColorFactor = ERHIBlendFactor::SrcAlpha;
		Blend.DstColorFactor = ERHIBlendFactor::OneMinusSrcAlpha;
		Blend.SrcAlphaFactor = ERHIBlendFactor::One;
		Blend.DstAlphaFactor = ERHIBlendFactor::OneMinusSrcAlpha;
		PipelineDesc.AttachmentBlends = { Blend };
		FRHIGraphicsPipeline* Pipeline = R.GetOrCreateGraphicsPipeline(PipelineDesc);
		if (PipelineLayout == nullptr || Pipeline == nullptr)
		{
			MAHO_LOG_CORE_ERROR("FUIFeature: GetOrCreate layout/pipeline failed");
			return;
		}
		// Shader modules + layout + pipeline are pool-owned (no manual destroy here).

		// One-time font atlas upload (staging buffer -> texture, then to shader resource).
		if (!Data->bFontUploaded)
		{
			ImFontAtlas* Fonts = ImGui::GetIO().Fonts;
			unsigned char* Pixels = nullptr;
			int FontW = 0, FontH = 0, FontBpp = 0;
			Fonts->GetTexDataAsRGBA32(&Pixels, &FontW, &FontH, &FontBpp);
			Cmd.UpdateBuffer(Data->FontStaging.GetRHI(), 0, static_cast<std::uint64_t>(FontW) * FontH * 4, Pixels);
			Cmd.TransitionTexture(Data->FontTexture.GetRHI(), ERHIResourceState::Common, ERHIResourceState::CopyDst);
			Cmd.CopyBufferToTexture(Data->FontStaging.GetRHI(), Data->FontTexture.GetRHI(), 0);
			Cmd.TransitionTexture(Data->FontTexture.GetRHI(), ERHIResourceState::CopyDst, ERHIResourceState::ShaderResource);
			Data->bFontUploaded = true;
		}

		// Concatenate all ImDrawLists' vertices/indices into CPU arrays, grow the GPU
		// buffers on demand, and upload (CPUToGPU memcpy).
		std::size_t TotalVerts = 0, TotalIndices = 0;
		for (int I = 0; I < DrawData->CmdListsCount; ++I)
		{
			TotalVerts += DrawData->CmdLists[I]->VtxBuffer.Size;
			TotalIndices += DrawData->CmdLists[I]->IdxBuffer.Size;
		}
		if (TotalVerts == 0 || TotalIndices == 0)
		{
			return;
		}
		// Pooled transient vertex/index buffers -- recreated each frame; the pool
		// destroys last frame's natives after the fence wait and recycles the slots.
		FRHIBufferDesc VDesc;
		VDesc.Size = static_cast<std::uint64_t>(TotalVerts) * sizeof(ImDrawVert);
		VDesc.Usage = ERHIBufferUsage::Vertex;
		VDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
		Data->VertexBuffer = R.CreateBuffer(VDesc, ERDGResourceLifetime::Transient);
		FRHIBufferDesc IDesc;
		IDesc.Size = static_cast<std::uint64_t>(TotalIndices) * sizeof(ImDrawIdx);
		IDesc.Usage = ERHIBufferUsage::Index;
		IDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
		Data->IndexBuffer = R.CreateBuffer(IDesc, ERDGResourceLifetime::Transient);
		std::vector<ImDrawVert> CombinedVerts;
		std::vector<ImDrawIdx> CombinedIndices;
		CombinedVerts.reserve(TotalVerts);
		CombinedIndices.reserve(TotalIndices);
		for (int I = 0; I < DrawData->CmdListsCount; ++I)
		{
			ImDrawList* List = DrawData->CmdLists[I];
			CombinedVerts.insert(CombinedVerts.end(), List->VtxBuffer.Data, List->VtxBuffer.Data + List->VtxBuffer.Size);
			CombinedIndices.insert(CombinedIndices.end(), List->IdxBuffer.Data, List->IdxBuffer.Data + List->IdxBuffer.Size);
		}
		Cmd.UpdateBuffer(Data->VertexBuffer.GetRHI(), 0, TotalVerts * sizeof(ImDrawVert), CombinedVerts.data());
		Cmd.UpdateBuffer(Data->IndexBuffer.GetRHI(), 0, TotalIndices * sizeof(ImDrawIdx), CombinedIndices.data());

		// Dynamic rendering over SceneColor (Load) + the ImGui draw loop.
		FRHIRenderingAttachmentInfo Color;
		Color.View = SceneColor.GetView();
		Color.LoadOp = ERHILoadOp::Load;
		Color.StoreOp = ERHIStoreOp::Store;
		Cmd.BeginRendering(&Color, 1, nullptr, TargetW, TargetH);

		Cmd.BindGraphicsPipeline(Pipeline);
		// The pipeline uses a dynamic viewport; set it (full framebuffer) before
		// any draw -- vkCmdDrawIndexed with an unset dynamic viewport is invalid
		// (VUID-vkCmdDrawIndexed-None-07831).
		Cmd.SetViewport(0.0f, 0.0f,
			static_cast<float>(TargetW),
			static_cast<float>(TargetH));
		Cmd.BindVertexBuffer(0, Data->VertexBuffer.GetRHI(), 0);
		Cmd.BindIndexBuffer(Data->IndexBuffer.GetRHI(), 0, sizeof(ImDrawIdx) == 4);
		FRHIDescriptorSet* FontSet = Data->FontDescriptorSet;
		Cmd.BindDescriptorSets(0, &FontSet, 1);

		// Ortho projection (DisplaySize coords -> NDC), column-major. Vulkan NDC y is
		// DOWN (top = -1): the y row uses (B-T), so ImGui's top maps to the screen top;
		// 2/(T-B) would flip the UI vertically.
		const float OrthoL = DrawData->DisplayPos.x;
		const float OrthoT = DrawData->DisplayPos.y;
		const float OrthoRt = DrawData->DisplayPos.x + DrawData->DisplaySize.x;
		const float OrthoB = DrawData->DisplayPos.y + DrawData->DisplaySize.y;
		const float Ortho[16] = {
			2.0f / (OrthoRt - OrthoL), 0.0f, 0.0f, 0.0f,
			0.0f, 2.0f / (OrthoB - OrthoT), 0.0f, 0.0f,
			0.0f, 0.0f, -1.0f, 0.0f,
			-(OrthoRt + OrthoL) / (OrthoRt - OrthoL), -(OrthoT + OrthoB) / (OrthoB - OrthoT), 0.0f, 1.0f,
		};
		Cmd.PushConstants(ERHIShaderStage::Vertex, 0, sizeof(Ortho), Ortho);

		std::size_t VtxBase = 0, IdxBase = 0;
		const float DisplayW = DrawData->DisplaySize.x;
		const float DisplayH = DrawData->DisplaySize.y;
		for (int I = 0; I < DrawData->CmdListsCount; ++I)
		{
			const ImDrawList* List = DrawData->CmdLists[I];
			for (const ImDrawCmd& DrawCmd : List->CmdBuffer)
			{
				// Scissor = clip rect clamped to the framebuffer (DisplaySize == it).
				const ImVec4 Clip = DrawCmd.ClipRect;
				const std::int32_t Sx = static_cast<std::int32_t>(Clip.x < 0.0f ? 0.0f : Clip.x);
				const std::int32_t Sy = static_cast<std::int32_t>(Clip.y < 0.0f ? 0.0f : Clip.y);
				const std::int32_t Sw = static_cast<std::int32_t>(Clip.z > DisplayW ? DisplayW : Clip.z) - Sx;
				const std::int32_t Sh = static_cast<std::int32_t>(Clip.w > DisplayH ? DisplayH : Clip.w) - Sy;
				Cmd.SetScissor(Sx, Sy, Sw, Sh);
				Cmd.DrawIndexed(
					DrawCmd.ElemCount, 1,
					static_cast<std::uint32_t>(IdxBase + DrawCmd.IdxOffset),
					static_cast<std::int32_t>(VtxBase + DrawCmd.VtxOffset),
					0);
			}
			VtxBase += List->VtxBuffer.Size;
			IdxBase += List->IdxBuffer.Size;
		}

		Cmd.EndRendering();
	});
}

} // namespace Maho

// The C export FRender looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_UIFEATURE_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FUIFeature::CreateLayer();
}
