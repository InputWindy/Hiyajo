#include "UIFeature.h"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <DrawTriangleFeature.h>
#include <Frame.h>
#include <Log.h>
#include <Scene.h>
#include <RHI/RHICommandList.h>
#include <RHI/RHIEnums.h>
#include <RHI/RHIResources.h>

#include "imgui.h"

namespace
{
	// ImGui vertex shader: aPos (loc0) / aUV (loc1) / aColor (loc2) + a vertex-stage
	// push constant (mat4) for the ortho projection. gl_Position = Proj * aPos.
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

	// ImGui fragment shader: sample the font atlas, multiply by per-vertex color.
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
}

namespace Maho
{

// Statics of FUIShader: expose the feature-private GLSL to the shader type.
const char* FUIShader::GetVertexSource()       { return kImGuiVertShader; }
const char* FUIShader::GetFragmentSource()     { return kImGuiFragShader; }
const char* FUIShader::GetVertexEntryPoint()   { return "main"; }
const char* FUIShader::GetFragmentEntryPoint() { return "main"; }

bool FUIFeature::EnsureUIBackend(FRender& R)
{
	if (bUIInit)
	{
		return true;
	}

	// -- descriptor set layout for the font texture (set 0, binding 0) --
	// Cached (get-or-create) by the pool; kept as a borrowed handle because the
	// font descriptor pool allocates against it and the (cached) pipeline layout
	// references it.
	FRHIDescriptorBinding FontB;
	FontB.Binding = 0;
	FontB.Type = ERHIDescriptorType::CombinedImageSampler;
	FontB.Count = 1;
	FontB.Stages = ERHIShaderStage::Fragment;
	FRHIDescriptorSetLayoutDesc FontSetDesc;
	FontSetDesc.Bindings.push_back(FontB);
	FontSetLayout = R.GetOrCreateDescriptorSetLayout(FontSetDesc);
	if (FontSetLayout == nullptr)
	{
		MAHO_LOG_CORE_ERROR("FUIFeature: UI descriptor set layout failed");
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
	FontTexture = R.CreateTexture(TexDesc, ERDGResourceLifetime::Persistent);
	FRHISamplerDesc SamplerDesc;
	SamplerDesc.AddressU = ERHIAddressMode::ClampToEdge;
	SamplerDesc.AddressV = ERHIAddressMode::ClampToEdge;
	SamplerDesc.AddressW = ERHIAddressMode::ClampToEdge;
	// Sampler is pool-owned (get-or-create by descriptor); the feature holds only the
	// handle, the pool destroys it at Shutdown.
	FontSampler = R.CreateSampler(SamplerDesc);
	if (FontSampler == nullptr)
	{
		MAHO_LOG_CORE_ERROR("FUIFeature: UI sampler failed");
		return false;
	}
	// Pool-owned descriptor set: content-addressable get-or-create keyed by the set
	// layout + referenced resources. The pool writes the descriptor content (font
	// view + sampler) at allocation time -- a device-level op (vkUpdateDescriptorSets),
	// not a recorded vkCmd -- so no write is deferred into a pass.
	FRHIDescriptorWrite FontWrite;
	FontWrite.Binding = 0;
	FontWrite.Type = ERHIDescriptorType::CombinedImageSampler;
	FontWrite.TextureView = FontTexture.GetView();
	FontWrite.Sampler = FontSampler;
	FontDescriptorSet = R.GetOrCreateDescriptorSet(FontSetLayout, FontSetDesc, &FontWrite, 1);
	if (FontDescriptorSet == nullptr)
	{
		MAHO_LOG_CORE_ERROR("FUIFeature: UI descriptor set failed");
		return false;
	}
	// Staging buffer for the font pixels (CPUToGPU, filled via UpdateBuffer).
	FRHIBufferDesc StagingDesc;
	StagingDesc.Size = static_cast<std::uint64_t>(FontW) * FontH * 4;
	StagingDesc.Usage = ERHIBufferUsage::TransferSrc;
	StagingDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
	// Pooled transient staging buffer -- the pool destroys it after the upload's
	// fence wait (one frame), so no native buffer is held across frames.
	FontStaging = R.CreateBuffer(StagingDesc, ERDGResourceLifetime::Transient);
	if (!FontStaging.IsValid() || FontStaging.GetRHI() == nullptr)
	{
		MAHO_LOG_CORE_ERROR("FUIFeature: UI font staging buffer failed");
		return false;
	}

	// The ImGui font texture id (ImTextureID) is the descriptor set handle.
	Fonts->TexID = reinterpret_cast<ImTextureID>(FontDescriptorSet);

	bUIInit = true;
	MAHO_LOG_CORE_INFO("FUIFeature: UI font backend ready (font; pipeline fetched per-frame from cache)");
	return true;
}

void FUIFeature::UploadFont(FRender& R)
{
	if (bFontUploaded)
	{
		return;
	}
	// One-time font-atlas upload. The UI draw pass's lambda runs INSIDE
	// BeginRendering, where transfer commands (vkCmdCopyBufferToImage + barriers)
	// are illegal -- so this genuine transfer submit stays OUTSIDE a render pass. It
	// is pure initialization, executed once.
	R.AddPass(ERHICommandListType::Graphics, [this](FRHICommandList& Cmd)
	{
		ImFontAtlas* Fonts = ImGui::GetIO().Fonts;
		unsigned char* Pixels = nullptr;
		int FontW = 0, FontH = 0, FontBpp = 0;
		Fonts->GetTexDataAsRGBA32(&Pixels, &FontW, &FontH, &FontBpp);
		if (Pixels != nullptr && FontW > 0 && FontH > 0)
		{
			Cmd.UpdateBuffer(FontStaging.GetRHI(), 0,
				static_cast<std::uint64_t>(FontW) * FontH * 4, Pixels);
			Cmd.TransitionTexture(FontTexture.GetRHI(), ERHIResourceState::Common, ERHIResourceState::CopyDst);
			Cmd.CopyBufferToTexture(FontStaging.GetRHI(), FontTexture.GetRHI(), 0);
			Cmd.TransitionTexture(FontTexture.GetRHI(), ERHIResourceState::CopyDst, ERHIResourceState::ShaderResource);
		}
		bFontUploaded = true;
	});
}

FUIFeature::FUIFeature()
{
	// UI draws + submits LAST -- after every scene render feature's IEndRender
	// (their submits reach the queue first), so the UI composites over the scene.
	MyStage<IRenderUI>().IsWaiting<Scene::FScene>().ForStage<IEndRender>();
	MyStage<IRenderUI>().IsWaiting<FDrawTriangleFeature>().ForStage<IEndRender>();
	// (FFrame additionally declares IPresent waits for my IRenderUI.)
}

void FUIFeature::InitViews(FRender& R)
{
	// CPU-side data integration (UE InitViews analogue) now lives in the UI
	// engine layer's ITick, which produces the draw data FScene receives via the
	// sink. This stage stays as a graph no-op so the feature's stage list and the
	// render-graph topology are unchanged.
	(void)R;
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

	ImDrawData* DrawData = R.UIData;
	if (DrawData == nullptr || !DrawData->Valid || DrawData->CmdListsCount <= 0)
	{
		MAHO_LOG_CORE_INFO("FUIFeature: no draw data (dd={} valid={} lists={})",
			DrawData != nullptr, DrawData != nullptr && DrawData->Valid,
			DrawData != nullptr ? DrawData->CmdListsCount : -1);
		return;
	}

	// The UI SHADER is fetched via the shared FRender::TryGetShader<FUIShader> path
	// below (no bytecode cache here). The FONT backend resources (font texture /
	// sampler / descriptor set + layout) are held by THIS feature as borrowed pool
	// handles. Lazy init + the one-time font upload (a transfer submit, illegal
	// inside a render pass) are here too.
	if (!EnsureUIBackend(R))
	{
		return;   // backend init failed this frame
	}
	if (!bFontUploaded)
	{
		UploadFont(R);
		if (!bFontUploaded)
		{
			return;   // font upload failed this frame
		}
	}

	FRHIDescriptorSetLayout* FontSetLayout = this->FontSetLayout;
	FRHIDescriptorSet* FontDescriptorSet = this->FontDescriptorSet;

	// -- Draw pass -- one subpass via the TYPED AddPass. AddPass resolves the PSO from
	//    the pass input layout + the target, starts dynamic rendering, BINDS the
	//    pipeline implicitly, and only then runs this lambda -- so the lambda records
	//    ONLY the draws (viewport / binds / push constant / draw calls). The feature
	//    never queries a pipeline or calls BeginRendering/BindGraphicsPipeline itself.
	FRenderPassDesc Pass;
	FRenderTarget& Target = Pass.Target;
	FRenderTarget::FAttachment Color;
	Color.View = SceneColor;
	Color.LoadOp = ERHILoadOp::Load;
	Color.StoreOp = ERHIStoreOp::Store;
	Target.AddColor(Color);
	Target.SetSize(TargetW, TargetH);

	// Input binding (Pass.Layout): the font descriptor set (set 0) + the vertex-stage
	// push constant. AddPass fills PipelineDesc.Layout from this and resolves the PSO.
	FRHIPushConstantRange PushRange;
	PushRange.Stages = ERHIShaderStage::Vertex;
	PushRange.Offset = 0;
	PushRange.Size = 64;   // mat4
	Pass.Layout.SetLayouts.push_back(FontSetLayout);
	Pass.Layout.PushConstants.push_back(PushRange);

	// Fetch the UI shader modules through the shared per-type async path (compile +
	// sync up front), so the resolved modules, bytecode hashes and entry points can
	// be written straight into the pipeline config. Each frame returns the cached
	// handle; Wait() is the sync-before-use -- identical to the triangle feature.
	TShaderHandle<FUIShader> Shader = R.TryGetShader<FUIShader>();
	FRHIShaderModule* VS = nullptr;
	FRHIShaderModule* FS = nullptr;
	if (Shader.Wait())
	{
		VS = Shader.GetVertex();
		FS = Shader.GetFragment();
	}
	if (VS == nullptr || FS == nullptr)
	{
		MAHO_LOG_CORE_ERROR("FUIFeature: shader not ready");
		return;
	}

	FRHIGraphicsPipelineDesc PipelineDesc;
	PipelineDesc.VertexShader = VS;
	PipelineDesc.FragmentShader = FS;
	PipelineDesc.VertexShaderHash = Shader.GetVertexHash();
	PipelineDesc.FragmentShaderHash = Shader.GetFragmentHash();
	PipelineDesc.VertexEntryPoint = Detail::GetVertexEntryPoint<FUIShader>();
	PipelineDesc.FragmentEntryPoint = Detail::GetFragmentEntryPoint<FUIShader>();
	// NOTE: PipelineDesc.Layout is left unset -- AddPass fills it from Pass.Layout.
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

	R.AddPass(ERHICommandListType::Graphics, PipelineDesc, Pass,
		[&R, DrawData, TargetW, TargetH, FontDescriptorSet](FRHICommandList& Cmd)
		{
			// [格式转换] Concatenate every ImDrawList's vertices/indices into one CPU
			// array, grow pooled transient GPU buffers to fit, and upload. The
			// vertex/index buffers are CPUToGPU (host-visible), so UpdateBuffer is a
			// Map+memcpy (a pure CPU write, no recorded GPU command) -- legal even
			// inside BeginRendering; the draws then read it.
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
			FRHIBufferDesc VDesc;
			VDesc.Size = static_cast<std::uint64_t>(TotalVerts) * sizeof(ImDrawVert);
			VDesc.Usage = ERHIBufferUsage::Vertex;
			VDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
			FRDGBufferRef VB = R.CreateBuffer(VDesc, ERDGResourceLifetime::Transient);
			FRHIBufferDesc IDesc;
			IDesc.Size = static_cast<std::uint64_t>(TotalIndices) * sizeof(ImDrawIdx);
			IDesc.Usage = ERHIBufferUsage::Index;
			IDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
			FRDGBufferRef IB = R.CreateBuffer(IDesc, ERDGResourceLifetime::Transient);
			std::vector<ImDrawVert> Verts;
			std::vector<ImDrawIdx> Idx;
			Verts.reserve(TotalVerts);
			Idx.reserve(TotalIndices);
			for (int I = 0; I < DrawData->CmdListsCount; ++I)
			{
				const ImDrawList* List = DrawData->CmdLists[I];
				Verts.insert(Verts.end(), List->VtxBuffer.Data, List->VtxBuffer.Data + List->VtxBuffer.Size);
				Idx.insert(Idx.end(), List->IdxBuffer.Data, List->IdxBuffer.Data + List->IdxBuffer.Size);
			}
			Cmd.UpdateBuffer(VB.GetRHI(), 0, TotalVerts * sizeof(ImDrawVert), Verts.data());
			Cmd.UpdateBuffer(IB.GetRHI(), 0, TotalIndices * sizeof(ImDrawIdx), Idx.data());

			Cmd.SetViewport(0.0f, 0.0f,
				static_cast<float>(TargetW),
				static_cast<float>(TargetH));
			Cmd.BindVertexBuffer(0, VB.GetRHI(), 0);
			Cmd.BindIndexBuffer(IB.GetRHI(), 0, sizeof(ImDrawIdx) == 4);
			Cmd.BindDescriptorSets(0, &FontDescriptorSet, 1);

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
						// Bind the per-draw texture. ImGui uses the font set (Fonts->TexID)
						// for text and a user-provided ImTextureID for Image() calls; the
						// texture id IS the descriptor set handle. All sets share the same
						// set-0 CombinedImageSampler layout (the font layout), so the bound
						// set is accepted by the pipeline. Fall back to the font set when
						// a cmd carries no texture (drawing with the default font atlas).
						FRHIDescriptorSet* CmdSet = (DrawCmd.TextureId != 0)
							? reinterpret_cast<FRHIDescriptorSet*>(static_cast<std::uintptr_t>(DrawCmd.TextureId))
							: FontDescriptorSet;
						Cmd.BindDescriptorSets(0, &CmdSet, 1);

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
		});
}

} // namespace Maho

// The C export FRender looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_UIFEATURE_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FUIFeature::CreateLayer();
}
