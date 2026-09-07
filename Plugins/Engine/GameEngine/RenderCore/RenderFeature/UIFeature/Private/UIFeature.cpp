#include "UIFeature.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include <DrawTriangleFeature.h>
#include <Log.h>
#include <Platform.h>
#include <Scene.h>
#include <UISystem.h>
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
	// The font atlas is the PASS-LEVEL set (bound via FUIParameters every RenderUI):
	// TexID 0 means "no per-batch texture, use the pass-level font set". A NON-zero
	// ImTextureID holds a mirror FName id (FName::GetId()), resolved per-batch below.
	Fonts->TexID = 0;

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

// (The frame's UI orchestration -- the game draws whatever it wants -- now lives in
// the game-side UISystem's UIBuilder: game UI components Submit draw closures there,
// and this feature pulls + runs them (FUIBuilder::Execute) between NewFrame and
// Render below. The feature no longer builds any UI: it only owns the ImGui context
// + the FName->RDG mirror resolution during draw.)

FUIFeature::FUIFeature()
{
	// UI draws + submits LAST -- after every scene render feature's IEndRender
	// (their submits reach the queue first), so the UI composites over the scene.
	MyStage<IRenderUI>().IsWaiting<Scene::FScene>().ForStage<IEndRender>();
	MyStage<IRenderUI>().IsWaiting<FDrawTriangleFeature>().ForStage<IEndRender>();
	// This feature now owns the present: IPresent blits the final composite RT
	// (UIRenderTarget) to the swapchain backbuffer. It self-advances after
	// IRenderUI (same layer), so it always runs after the UI was drawn.
}

void FUIFeature::TrySubscribeUI()
{
	if (bSubscribedUI)
	{
		return;
	}
	GameWorld::FUISystem* UI = GameWorld::GetUISystem();
	if (UI == nullptr)
	{
		return;   // world system not installed yet; retry on a later frame
	}
	// Handler runs ON THE GAME BROADCAST THREAD, right after the game finished
	// building this frame's closures (Submit done), and receives the UIBuilder by
	// reference (no GetUISystem() re-lookup). Copy the batch to THIS feature's snapshot.
	// The snapshot is replaced, never cleared -- so InitViews always has a complete
	// frame, even if it runs before the next broadcast.
	// Keep the returned subscription id so PreUnInstall unsubscribes ONLY this
	// handler (a subscriber owns its own subscription -- never RemoveAll).
	m_UISubscription = UI->SubscribeUIHandler([this](const GameWorld::FUIBuilder& Builder)
	{
		std::vector<std::function<void()>> Batch = Builder.CopyFrame();
		std::lock_guard Lock(m_UISnapshotMutex);
		m_UICommands = std::move(Batch);
	});
	bSubscribedUI = true;
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

	// Serialize the WHOLE ImGui frame behind one mutex: InitViews may run on any
	// render-pool worker, and different frames can land on different workers. ImGui's
	// GImGui is a non-thread-safe state machine, so one thread at a time must own
	// "current frame" (feed -> NewFrame -> build -> Render -> GetDrawData -> translate).
	// Lock here (before the IO feed) so every ImGui access below is mutually excluded.
	std::lock_guard<std::mutex> FrameLock(ImGuiFrameMutex);

	// -- Final on-screen composite target. This feature owns its own off-screen RT per
	//    the design (the UI is the last surface; the scene is sampled INto it via the
	//    game's imgui::image SceneColor control). Sized to the swapchain canvas + format
	//    so the IPresent blit to the backbuffer is geometry/format-consistent, rebuilt
	//    on resize. LoadOp Clear (fully redrawn each frame) in RenderUI.
	{
		const std::uint32_t CanvasW = R.GetCanvasWidth();
		const std::uint32_t CanvasH = R.GetCanvasHeight();
		if (CanvasW == 0 || CanvasH == 0)
		{
			return;
		}
		if (!UIRenderTarget.IsValid()
			|| UIRenderTarget.GetWidth() != CanvasW
			|| UIRenderTarget.GetHeight() != CanvasH)
		{
			if (UIRenderTarget.IsValid())
			{
				R.ReleaseTexture(UIRenderTarget);
				UIRenderTarget.Reset();
			}
			FRHITextureDesc Desc;
			Desc.Format = R.GetSwapchainFormat();
			Desc.Dimension = ERHITextureDimension::Tex2D;
			Desc.Extent = { CanvasW, CanvasH, 1 };
			Desc.MipLevels = 1;
			Desc.ArrayLayers = 1;
			Desc.Usage = ERHITextureUsage::ColorAttachment
				| ERHITextureUsage::Sampled
				| ERHITextureUsage::TransferSrc;
			Desc.MemoryUsage = ERHIMemoryUsage::GPUOnly;
			UIRenderTarget = R.CreateTexture(Desc, ERDGResourceLifetime::Persistent);
			if (!UIRenderTarget.IsValid())
			{
				MAHO_LOG_CORE_ERROR("FUIFeature: UI composite target creation failed");
				return;
			}
		}
	}

	// -- Frame feed (Platform window + Win32 input + lazy font-atlas build), then
	//    NewFrame. The WHOLE ImGui frame lifecycle is driven here: feed -> NewFrame ->
	//    build UI -> Render -> GetDrawData -> translate.
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

	// Pull the game-side UI commands from the UISystem's UIBuilder and run them. The
	// game UI components Submit draw closures (FUIBuilder::Submit) during their Update;
	// this render worker runs them between NewFrame and Render -- every ImGui call the
	// game makes stays on this single owner thread. The game defines what to draw;
	// this feature only owns the ImGui context and the draw-data translation.
	//
	// EVENT-DRIVEN SNAPSHOT (NOT a racy Execute): the game update is dispatched
	// un-flushed (cross-frame pipelined), so calling Execute() here would race the
	// Submit and sometimes see an EMPTY/partial batch -- ImGui hides windows that were
	// not Begin()'d that frame, which flickered the widgets. Instead this feature
	// subscribes to the UI-built event (TrySubscribeUI): on the GAME thread, right
	// after Submit finished building the frame, the handler copies the UIBuilder batch
	// into m_UICommands. The snapshot is only REPLACED, never cleared, so every frame
	// runs a complete set -- no empty-batch flicker.
	TrySubscribeUI();
	std::vector<std::function<void()>> Commands;
	{
		std::lock_guard Lock(m_UISnapshotMutex);
		Commands = m_UICommands;   // copy of the last complete frame snapshot
	}
	for (auto& Fn : Commands)
	{
		try
		{
			Fn();
		}
		catch (const std::exception& E)
		{
			MAHO_LOG_CORE_ERROR("FUIFeature: game UI command threw: {}", E.what());
		}
		catch (...)
		{
			MAHO_LOG_CORE_ERROR("FUIFeature: game UI command threw an unknown exception");
		}
	}

	// Close the frame + take the draw data. ALWAYS runs -- even if a closure threw
	// above, the frame is still ended so g.FrameCountEnded stays synchronized. The
	// data is valid until the NEXT NewFrame; we translate it below and upload it to
	// a transient GPU buffer.
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
	//    batch vectors reuse capacity across frames). The pass-level vertex/index GPU
	//    buffers are the merged ImDrawData (one vertex/index array), created + uploaded
	//    HERE (InitViews); only the FRDGBufferRefs are stored. RenderUI (later, same
	//    graph) draws them via AddPass.
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
	// Upload the merged ImDrawData vertex/index arrays to GPU NOW (this frame's
	// InitViews), NOT at RenderUI. The target buffers are device-local (GPUOnly), so
	// UpdateBuffer records an async vkCmdCopyBuffer via a host-visible staging --
	// the CPU returns immediately; the copy runs on the GPU, and the staging is
	// deferred-freed at the NEXT frame boundary (after this frame's fence). The
	// draw (RenderUI) submits later on the same queue, so it reads after the copy.
	// Transient buffers are recycled at the next BeginFrame, so RenderUI (same
	// frame, later graph stage) still reads them.
	const std::uint64_t VtxBytes = static_cast<std::uint64_t>(TotalVerts) * sizeof(ImDrawVert);
	const std::uint64_t IdxBytes = static_cast<std::uint64_t>(TotalIndices) * sizeof(ImDrawIdx);

	FRHIBufferDesc VDesc;
	VDesc.Size = VtxBytes;
	VDesc.Usage = ERHIBufferUsage::Vertex;
	VDesc.MemoryUsage = ERHIMemoryUsage::GPUOnly;
	FRDGBufferRef VB = R.CreateBuffer(VDesc, ERDGResourceLifetime::Transient);

	FRHIBufferDesc IDesc;
	IDesc.Size = IdxBytes;
	IDesc.Usage = ERHIBufferUsage::Index;
	IDesc.MemoryUsage = ERHIMemoryUsage::GPUOnly;
	FRDGBufferRef IB = R.CreateBuffer(IDesc, ERDGResourceLifetime::Transient);

	if (!VB.IsValid() || !IB.IsValid())
	{
		MAHO_LOG_CORE_ERROR("FUIFeature: UI vertex/index buffer create failed");
		return;
	}
	R.AddPass(ERHICommandListType::Graphics,
		[&VB, &IB, &Verts, &Idx, VtxBytes, IdxBytes](FRHICommandList& Cmd)
		{
			Cmd.UpdateBuffer(VB.GetRHI(), 0, VtxBytes, Verts.data());
			Cmd.UpdateBuffer(IB.GetRHI(), 0, IdxBytes, Idx.data());
		});
	DrawList.SetVertexBuffer(VB);
	DrawList.SetIndexBuffer(IB);

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
	// The shader-parameter range for the ortho mat4 is declared Vertex|Fragment (the
	// SHADER_PARAMETER_ARRAY macro does not pin a stage), so the push constant must be
	// recorded with the same stage set -- vkCmdPushConstants requires the call's
	// stageFlags to include the layout range's stageFlags (VUID-offset-01796). Vertex
	// alone would under-declare it.
	DrawList.SetPushConstants(ERHIShaderStage::Vertex | ERHIShaderStage::Fragment, static_cast<std::uint32_t>(sizeof(Ortho)), Ortho);

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
				// Non-zero ImTextureID = a mirror FName id. Reconstruct the name and
				// re-resolve the RDG texture from the mirror pool (GpuMirrors, keyed by
				// FName); only a texture mirror (not a buffer) is drawable. The clamp
				// sampler is pooled content-addressable by desc -- no native is held.
				const Name::FName TexName = Name::FName::FromId(static_cast<std::uint32_t>(DrawCmd.TextureId));
				const FRDGResourceRef* Mirror = R.GetMirror(TexName);
				const FRDGTextureRef* Tex = Mirror != nullptr ? std::get_if<FRDGTextureRef>(Mirror) : nullptr;
				if (Tex != nullptr)
				{
					FRDGDescriptorSet DescriptorSet;
					DescriptorSet.SetIndex = 0;
					DescriptorSet.Frequency = EDescriptorSetFrequency::Static;
					FRDGBinding Binding;
					Binding.Type = ERHIDescriptorType::CombinedImageSampler;
					Binding.Stages = ERHIShaderStage::Fragment;
					Binding.Resource = *Tex;
					Binding.SamplerIndex = DescriptorSet.AddSampler(R.CreateSampler(BuildClampSamplerDesc()));
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
	// Draw into THIS feature's composite target -- NOT SceneColor. The UI is the final
	// on-screen surface: the scene is sampled INto it (game imgui::image of the SceneColor
	// mirror), then the UI controls draw over it. The target format/size come from the
	// target's own desc (the off-screen canvas geometry), never the swapchain/RHI.
	if (!UIRenderTarget.IsValid())
	{
		return;
	}
	const ERHIFormat ColorFormat = UIRenderTarget.GetFormat();

	// Already translated from ImDrawData in InitViews (the whole ImGui frame
	// lifecycle lives there, including the GPU-buffer upload). Draw from THAT list --
	// FRender holds no ImGui state, so the list lives here in the feature.
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
	Color.View = UIRenderTarget;
	Color.LoadOp = ERHILoadOp::Clear;
	Color.StoreOp = ERHIStoreOp::Store;
	Target.AddColor(Color);

	// Input binding (FUIParameters, macro-declared): the font descriptor set (set 0:
	// font texture + sampler) + the vertex-stage push-constant range (mat4). AddPass
	// translates the macro metadata (ShaderParameterBuild) into the pass layout, fills
	// PipelineDesc.Layout from it and resolves the PSO; the font set is materialised +
	// bound by AddPass (hidden from the feature). The font sampler + texture come from
	// the registry -- no RHI op, no held descriptor set.
	if (!FontTexture.IsValid())
	{
		MAHO_LOG_CORE_ERROR("FUIFeature: font texture missing");
		return;
	}
	FUIParameters* Params = R.AllocParameters<FUIParameters>();
	Params->FontTexture = FontTexture;
	Params->FontSampler = R.CreateSampler(BuildClampSamplerDesc());

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

void FUIFeature::Present(FRender& R)
{
	// This feature owns the final present point: blit the composite target to the
	// swapchain backbuffer. Runs after IRenderUI (self-advancing), so the UI
	// (incl. its SceneColor sample) is fully drawn first.
	if (UIRenderTarget.IsValid())
	{
		R.PresentTexture(UIRenderTarget);
	}
}

void FUIFeature::PreUnInstall(FRender& R)
{
	(void)R;
	// Release this feature's composite target before shutdown (the pool owns the native
	// lifetime, but the ref must be dropped here, like the font texture).
	if (UIRenderTarget.IsValid())
	{
		R.ReleaseTexture(UIRenderTarget);
		UIRenderTarget.Reset();
	}
	// This feature owns the ImGui context, so it tears it down here. This feature is
	// uninstalled BEFORE UISystem (it depends on UISystem), so the game side is still
	// alive: drop our UI-built subscription so the (still-live) game event no longer
	// captures a dead `this`, then clear the snapshot.
	if (GameWorld::FUISystem* UI = GameWorld::GetUISystem())
	{
		UI->UnsubscribeUIHandler(m_UISubscription);
	}
	m_UISubscription = 0;
	{
		std::lock_guard Lock(m_UISnapshotMutex);
		m_UICommands.clear();
	}
	bSubscribedUI = false;
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
