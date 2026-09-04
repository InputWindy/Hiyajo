#include "UIFeature.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>
#include <DrawTriangleFeature.h>
#include <Frame.h>
#include <Log.h>
#include <Platform.h>
#include <Scene.h>
#include <RHI/RHICommandList.h>
#include <RHI/RHIEnums.h>
#include <RHI/RHIResources.h>
#include <ShaderParameterStruct.h>

#include "imgui.h"

#if defined(_WIN32)
#	include <windows.h>
#endif

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
	/** Shared clamp-to-edge sampler desc (font + mirrors): one sampler descriptor ->
	 *  one pooled sampler (content-addressable), re-resolved to the same FRHISampler*
	 *  every call. */
	FRHISamplerDesc BuildClampSamplerDesc()
	{
		FRHISamplerDesc S;
		S.AddressU = ERHIAddressMode::ClampToEdge;
		S.AddressV = ERHIAddressMode::ClampToEdge;
		S.AddressW = ERHIAddressMode::ClampToEdge;
		return S;
	}
}

namespace Maho
{

// Compile-time FParameters for the UI draw pass, mirroring the shader's push
// constant block (mat4 Proj, vertex stage) + set0/binding0 font descriptor. The
// pass input layout (descriptor binding + push-constant range) is baked here by
// the macro engine -- the same source of truth the RHI pipeline layout is built
// from. The ortho push DATA is carried by the FDrawList; the macro declares only
// the range for pipeline layout.
BEGIN_SHADER_PARAMETER_STRUCT(FUIParameters)
	SHADER_PARAMETER_TEXTURE_SAMPLER(Texture2D, FontTexture, FontSampler, 0, 0, ERHIShaderStage::Fragment)
	SHADER_PARAMETER_ARRAY(float, OrthoMatrix, 16)
END_SHADER_PARAMETER_STRUCT()

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

	// -- font texture + its ImGui id --
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
	if (!FontTexture.IsValid())
	{
		MAHO_LOG_CORE_ERROR("FUIFeature: UI font texture failed");
		return false;
	}
	// Register the font texture with its shared sampler; ImGui's TexID is this id. The
	// draw path resolves the per-batch descriptor set from the RDG texture + sampler
	// inside AddPass (content-addressable) -- no FRHIDescriptorSet* is produced here.
	FontId = RegisterTexture(R, FontTexture);
	if (FontId == 0)
	{
		MAHO_LOG_CORE_ERROR("FUIFeature: UI font texture registration failed");
		return false;
	}
	Fonts->TexID = (ImTextureID)FontId;

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
	// is pure initialization, executed once. The staging buffer is a ONE-SHOT
	// transient, created + consumed in this single pass, so it is a local -- the pool
	// recycles it at the next BeginFrame, nothing is held across frames.
	R.AddPass(ERHICommandListType::Graphics, [this, &R](FRHICommandList& Cmd)
	{
		ImFontAtlas* Fonts = ImGui::GetIO().Fonts;
		unsigned char* Pixels = nullptr;
		int FontW = 0, FontH = 0, FontBpp = 0;
		Fonts->GetTexDataAsRGBA32(&Pixels, &FontW, &FontH, &FontBpp);
		if (Pixels != nullptr && FontW > 0 && FontH > 0 && FontTexture.IsValid())
		{
			FRHIBufferDesc StagingDesc;
			StagingDesc.Size = static_cast<std::uint64_t>(FontW) * FontH * 4;
			StagingDesc.Usage = ERHIBufferUsage::TransferSrc;
			StagingDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
			FRDGBufferRef Staging = R.CreateBuffer(StagingDesc, ERDGResourceLifetime::Transient);
			if (!Staging.IsValid() || Staging.GetRHI() == nullptr)
			{
				MAHO_LOG_CORE_ERROR("FUIFeature: UI font staging buffer failed");
				return;
			}
			Cmd.UpdateBuffer(Staging.GetRHI(), 0,
				static_cast<std::uint64_t>(FontW) * FontH * 4, Pixels);
			Cmd.TransitionTexture(FontTexture.GetRHI(), ERHIResourceState::Common, ERHIResourceState::CopyDst);
			Cmd.CopyBufferToTexture(Staging.GetRHI(), FontTexture.GetRHI(), 0);
			Cmd.TransitionTexture(FontTexture.GetRHI(), ERHIResourceState::CopyDst, ERHIResourceState::ShaderResource);
		}
		bFontUploaded = true;
	});
}

FUIFeature::FUIRegistryId FUIFeature::RegisterTexture(FRender& R, const FRDGTextureRef& Tex)
{
	if (!Tex.IsValid())
	{
		return 0;
	}
	// Reuse the existing id for the same GPU texture (content-addressable by RHI
	// handle) so repeated registration across frames does not grow the registry.
	for (const auto& [Id, Entry] : TextureRegistry)
	{
		if (Entry.Texture.GetRHI() == Tex.GetRHI())
		{
			return Id;
		}
	}
	// Pool-owned shared sampler: content-addressable get-or-create by desc, so every
	// call returns the same sampler (never held as a member).
	FRHISampler* Sampler = R.CreateSampler(BuildClampSamplerDesc());
	if (Sampler == nullptr)
	{
		MAHO_LOG_CORE_ERROR("FUIFeature: texture sampler failed");
		return 0;
	}
	const FUIRegistryId Id = NextTextureId++;
	TextureRegistry[Id] = { Tex, Sampler };
	return Id;
}

const FUIFeature::FUIRegistryEntry* FUIFeature::FindTexture(FUIRegistryId Id) const
{
	const auto It = TextureRegistry.find(Id);
	return It == TextureRegistry.end() ? nullptr : &It->second;
}

void FUIFeature::DisplayMirrorImGui(FRender& R)
{
	const auto& Mirrors = R.GetMirrors();
	if (Mirrors.empty())
	{
		return;
	}
	for (const auto& [AssetName, MirrorRef] : Mirrors)
	{
		const FRDGTextureRef* Tex = std::get_if<FRDGTextureRef>(&MirrorRef);
		if (Tex == nullptr)
		{
			continue;
		}
		// Register (or re-resolve) the mirror texture + shared sampler; ImGui's texture
		// id maps to an RDG texture + sampler, resolved by content inside AddPass. No
		// FRHIDescriptorSet* is created or held here.
		const FUIRegistryId Id = RegisterTexture(R, *Tex);
		if (Id == 0)
		{
			continue;
		}

		const std::string Title = "texture mirror: " + std::string(AssetName.ToString());
		// The mirror window tracks the application frame size every frame, so it
		// grows/shrinks with the OS window. NoResize: its size is driven by the app
		// window, not a manual drag handle.
		const ImVec2& Disp = ImGui::GetIO().DisplaySize;
		ImGui::SetNextWindowSize(
			ImVec2(Disp.x * 0.9f, Disp.y * 0.9f),
			ImGuiCond_Always);
		if (ImGui::Begin(Title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse))
		{
			// Fit the image into the window content region, preserving aspect ratio, so
			// it scales up/down with the window (and thus the OS window). Reserve a
			// line below for the caption text.
			const ImVec2 Avail = ImGui::GetContentRegionAvail();
			const float IW = static_cast<float>(Tex->GetWidth());
			const float IH = static_cast<float>(Tex->GetHeight());
			constexpr float CaptionH = 20.0f;
			const float Scale = (std::min)(Avail.x / IW, (Avail.y - CaptionH) / IH);
			ImGui::Image((ImTextureID)Id, ImVec2(IW * Scale, IH * Scale));
			ImGui::Text("asset=%s  %ux%u", AssetName.ToString().data(), Tex->GetWidth(), Tex->GetHeight());
		}
		ImGui::End();
	}
}

FUIFeature::FUIFeature()
{
	// UI draws + submits LAST -- after every scene render feature's IEndRender
	// (their submits reach the queue first), so the UI composites over the scene.
	MyStage<IRenderUI>().IsWaiting<Scene::FScene>().ForStage<IEndRender>();
	MyStage<IRenderUI>().IsWaiting<FDrawTriangleFeature>().ForStage<IEndRender>();
	// (FFrame additionally declares IPresent waits for my IRenderUI.)
}

void FUIFeature::OnInstalled(FRender& R)
{
	// THIS feature owns the UI's CPU-side ImGui context (FRender is now completely
	// UI-agnostic). Create it here, on install, BEFORE anything touches ImGui::GetIO()
	// (EnsureUIBackend below reads GetIO().Fonts). The window gate mirrors the old
	// FRender::Initialize check: no window -> UI disabled, context not created, and
	// InitViews bails each frame.
	if (!bContextCreated)
	{
		Platform::FPlatform* P = Platform::GetPlatform();
		if (P == nullptr || P->GetWindowWidth() == 0 || P->GetToolkitWindowHandle() == nullptr)
		{
			MAHO_LOG_CORE_ERROR("FUIFeature::OnInstalled: no window; UI disabled");
			return;
		}
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& IO = ImGui::GetIO();
		IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // the editor shell docks later
		ImGui::StyleColorsDark();
		bContextCreated = true;
		MAHO_LOG_CORE_INFO("FUIFeature: ImGui context created (CPU side; FRender untouched)");
	}

	// The UI SHADER is fetched via the shared FRender::TryGetShader<FUIShader> path
	// below (no bytecode cache here). The FONT backend resources (font texture /
	// sampler / descriptor set + layout) are re-resolved from the resource pool ON
	// DEMAND -- no raw RHI handle is held by this feature. Lazy init + the one-time
	// font upload (a transfer submit, illegal inside a render pass) are here too.
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
}

void FUIFeature::InitViews(FRender& R)
{
	if (!bContextCreated || !bUIInit)
	{
		return;   // ImGui context or font backend not ready
	}

	// -- Frame feed (Platform window + Win32 input + lazy font-atlas build), then
	//    NewFrame. The WHOLE ImGui frame lifecycle is driven here on this worker
	//    thread: feed -> NewFrame -> build UI -> Render -> GetDrawData -> translate.
	//    No other thread touches ImGui in a frame, so the single-thread contract holds.
	ImGuiIO& IO = ImGui::GetIO();
	Platform::FPlatform* P = Platform::GetPlatform();
	if (P == nullptr)
	{
		return;
	}
	// Display size must match the render target (SceneColor = swapchain extent), not
	// the window's logical size -- ImGui lays out in DisplaySize coordinates and the
	// render feature clips against it.
	IO.DisplaySize = ImVec2(
		static_cast<float>(P->GetWindowWidth()),
		static_cast<float>(P->GetWindowHeight()));
#if defined(_WIN32)
	// Input bypasses GLFW's message-driven cursor state (the window is created on a
	// pool worker and polled on another, so WM_MOUSEMOVE never reaches
	// glfwGetCursorPos). Win32 global state works from any thread.
	if (HWND Hwnd = static_cast<HWND>(P->GetNativeWindow()))
	{
		POINT Pt{};
		if (::GetCursorPos(&Pt) && ::ScreenToClient(Hwnd, &Pt))
		{
			RECT Client{};
			::GetClientRect(Hwnd, &Client);
			const float ScaleX = Client.right > 0 ? IO.DisplaySize.x / static_cast<float>(Client.right) : 1.f;
			const float ScaleY = Client.bottom > 0 ? IO.DisplaySize.y / static_cast<float>(Client.bottom) : 1.f;
			IO.AddMousePosEvent(static_cast<float>(Pt.x) * ScaleX, static_cast<float>(Pt.y) * ScaleY);
		}
		IO.AddMouseButtonEvent(0, (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
		IO.AddMouseButtonEvent(1, (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
		IO.AddMouseButtonEvent(2, (::GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
	}
#endif

	// Renderer-backend NewFrame duty (mirrors the imgui_impl_* backends, which must
	// be called before ImGui::NewFrame()): the font atlas is built lazily by
	// ImFontAtlas::Build() and ImGui::NewFrame() asserts IsBuilt(). Calling
	// GetTexDataAsRGBA32() triggers that build on the first frame and returns the
	// existing pixels afterwards (the GPU upload already happened in OnInstalled).
	unsigned char* FontPixels = nullptr;
	int FontW = 0, FontH = 0, FontBpp = 0;
	IO.Fonts->GetTexDataAsRGBA32(&FontPixels, &FontW, &FontH, &FontBpp);
	if (FontPixels == nullptr || FontW <= 0 || FontH <= 0)
	{
		MAHO_LOG_CORE_ERROR("FUIFeature: font atlas not built");
		return;
	}
	ImGui::NewFrame();

	// Build the frame UI: the feature's own test-harness windows (texture mirrors).
	DisplayMirrorImGui(R);

	// Close the frame + take the draw data. The data is valid until the NEXT
	// NewFrame; we translate + copy it below (SetPrimitiveData owns the copy).
	ImGui::Render();
	ImDrawData* DrawData = ImGui::GetDrawData();
	if (DrawData == nullptr || !DrawData->Valid || DrawData->CmdListsCount <= 0)
	{
		MAHO_LOG_CORE_INFO("FUIFeature: no draw data (valid={} lists={})",
			DrawData != nullptr && DrawData->Valid,
			DrawData != nullptr ? DrawData->CmdListsCount : -1);
		return;
	}

	// -- Translate ImDrawData -> FDrawList (a feature member so the merged buffer +
	//    batch vectors reuse capacity across frames). The pass-level CPU primitive
	//    data is the merged ImDrawData (one vertex/index array); SetPrimitiveData
	//    COPIES it, so ImGui's buffers need no lifetime beyond here. RenderUI (later,
	//    same graph) draws from the owned copy via AddPass.
	FDrawList& DrawList = this->DrawList;
	DrawList.Reset();

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
	std::vector<ImDrawVert> Verts(TotalVerts);
	std::vector<ImDrawIdx> Idx(TotalIndices);
	{
		std::size_t OffV = 0, OffI = 0;
		for (int I = 0; I < DrawData->CmdListsCount; ++I)
		{
			const ImDrawList* List = DrawData->CmdLists[I];
			std::memcpy(Verts.data() + OffV, List->VtxBuffer.Data, List->VtxBuffer.Size * sizeof(ImDrawVert));
			std::memcpy(Idx.data() + OffI, List->IdxBuffer.Data, List->IdxBuffer.Size * sizeof(ImDrawIdx));
			OffV += List->VtxBuffer.Size;
			OffI += List->IdxBuffer.Size;
		}
	}
	DrawList.SetPrimitiveData(
		Verts.data(), static_cast<std::uint64_t>(TotalVerts) * sizeof(ImDrawVert),
		static_cast<std::uint32_t>(sizeof(ImDrawVert)), static_cast<std::uint32_t>(TotalVerts),
		Idx.data(), static_cast<std::uint64_t>(TotalIndices) * sizeof(ImDrawIdx),
		sizeof(ImDrawIdx) == 4, static_cast<std::uint32_t>(TotalIndices));

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
	DrawList.SetPushConstants(ERHIShaderStage::Vertex, static_cast<std::uint32_t>(sizeof(Ortho)), Ortho);

	const float DisplayW = DrawData->DisplaySize.x;
	const float DisplayH = DrawData->DisplaySize.y;
	std::size_t VtxBase = 0, IdxBase = 0;
	for (int I = 0; I < DrawData->CmdListsCount; ++I)
	{
		const ImDrawList* List = DrawData->CmdLists[I];
		for (const ImDrawCmd& DrawCmd : List->CmdBuffer)
		{
			FDrawBatch Batch;
			Batch.VertexCount = DrawCmd.ElemCount;
			Batch.IndexCount = DrawCmd.ElemCount;
			Batch.bIndex32 = sizeof(ImDrawIdx) == 4;
			Batch.VertexOffset = static_cast<std::uint32_t>((VtxBase + DrawCmd.VtxOffset) * sizeof(ImDrawVert));
			Batch.IndexOffset = static_cast<std::uint32_t>((IdxBase + DrawCmd.IdxOffset) * sizeof(ImDrawIdx));

			// Scissor = clip rect clamped to the framebuffer (DisplaySize == it).
			const ImVec4 Clip = DrawCmd.ClipRect;
			Batch.ScissorX = static_cast<std::int32_t>(Clip.x < 0.0f ? 0.0f : Clip.x);
			Batch.ScissorY = static_cast<std::int32_t>(Clip.y < 0.0f ? 0.0f : Clip.y);
			Batch.ScissorW = static_cast<std::uint32_t>(static_cast<std::int32_t>(Clip.z > DisplayW ? DisplayW : Clip.z) - Batch.ScissorX);
			Batch.ScissorH = static_cast<std::uint32_t>(static_cast<std::int32_t>(Clip.w > DisplayH ? DisplayH : Clip.w) - Batch.ScissorY);
			Batch.bHasScissor = true;

			// Per-batch texture. ImGui uses the font set (no id, so this per-batch set is
			// skipped and AddPass falls back to the pass-level font set) for text, and a
			// registered texture id for Image(); the id maps to an RDG texture + shared
			// sampler, resolved by content inside AddPass.
			if (DrawCmd.TextureId != 0)
			{
				const FUIRegistryEntry* Entry = FindTexture((FUIRegistryId)DrawCmd.TextureId);
				if (Entry != nullptr)
				{
					FRDGDescriptorSet DescriptorSet;
					DescriptorSet.SetIndex = 0;
					DescriptorSet.Frequency = EDescriptorSetFrequency::Static;
					FRDGBinding Binding;
					Binding.Type = ERHIDescriptorType::CombinedImageSampler;
					Binding.Stages = ERHIShaderStage::Fragment;
					Binding.Resource = Entry->Texture;
					Binding.SamplerIndex = DescriptorSet.AddSampler(Entry->Sampler);
					DescriptorSet.Bindings.push_back({ 0, Binding });
					Batch.Sets.push_back(std::move(DescriptorSet));
				}
			}

			DrawList.Add(std::move(Batch));
		}
		VtxBase += List->VtxBuffer.Size;
		IdxBase += List->IdxBuffer.Size;
	}
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

	// Already translated from ImDrawData in InitViews (the whole ImGui frame
	// lifecycle lives there, including the owned-copy SetPrimitiveData). Draw from
	// THAT list -- FRender holds no ImGui state, so the list lives here in the feature.
	FDrawList& DrawList = this->DrawList;
	if (!DrawList.HasPrimitiveData())
	{
		return;
	}

	// -- Draw pass -- one subpass via the TYPED AddPass. AddPass resolves the PSO from
	//    the pass input layout + the target, starts dynamic rendering, BINDS the
	//    pipeline implicitly, and only then runs this lambda -- so the lambda records
	//    ONLY the draws (viewport / binds / push constant / draw calls). The feature
	//    never queries a pipeline or calls BeginRendering/BindGraphicsPipeline itself.
	FRenderTarget Target;
	FRenderTarget::FAttachment Color;
	Color.View = SceneColor;
	Color.LoadOp = ERHILoadOp::Load;
	Color.StoreOp = ERHIStoreOp::Store;
	Target.AddColor(Color);
	Target.SetSize(TargetW, TargetH);

	// Input binding (FUIParameters, macro-declared): the font descriptor set (set 0:
	// font texture + sampler) + the vertex-stage push-constant range (mat4). AddPass
	// translates the macro metadata (ShaderParameterBuild) into the pass layout, fills
	// PipelineDesc.Layout from it and resolves the PSO; the font set is materialised +
	// bound by AddPass (hidden from the feature). The font sampler + texture come from
	// the registry -- no RHI op, no held descriptor set.
	const FUIFeature::FUIRegistryEntry* FontEntry = FindTexture(FontId);
	if (FontEntry == nullptr)
	{
		MAHO_LOG_CORE_ERROR("FUIFeature: font registry entry missing");
		return;
	}
	FUIParameters* Params = R.AllocParameters<FUIParameters>();
	Params->FontTexture = FontEntry->Texture;
	Params->FontSampler = FontEntry->Sampler;

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

	// One AddPass == one subpass. AddPass uploads the CPU primitive data once, resolves
	// the pass-level font set + each per-batch set by content, binds the pipeline, and
	// records every batch's draw. Nothing below is a raw RHI operation.
	R.AddPass(ERHICommandListType::Graphics, PipelineDesc, Target, Params, DrawList);
}

void FUIFeature::PreUnInstall(FRender& R)
{
	(void)R;
	// THIS feature owns the ImGui context, so it tears it down here. Ordered after
	// every render feature teardown that may log / touch UI, and before the DLL unload.
	if (bContextCreated)
	{
		ImGui::DestroyContext();
		bContextCreated = false;
	}
}

} // namespace Maho

// The C export FRender looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_UIFEATURE_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FUIFeature::CreateLayer();
}
