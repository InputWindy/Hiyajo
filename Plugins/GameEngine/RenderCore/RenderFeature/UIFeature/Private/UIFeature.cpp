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
#include <FrameRenderFeature.h>
#include <Log.h>
#include <Name.h>
#include <Platform.h>
#include <Scene.h>
#include <UITranslate.h>
#include <UIResource.h>
#include <UIViewRegistry.h>
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

	// Map a GLFW key code (MInputContext::KeyDown index / MInputEvent::Key) to the ImGui
	// named key. ImGui keys are a dense named enum, NOT the raw GLFW code; the named key IS
	// the real input (arrows, letters, shortcuts). Virtual mods (ImGuiMod_*) are fed
	// separately from the snapshot.
	ImGuiKey MapGlfwKey(int Key)
	{
		switch (Key)
		{
		case 32:  return ImGuiKey_Space;
		case 39:  return ImGuiKey_Apostrophe;
		case 44:  return ImGuiKey_Comma;
		case 45:  return ImGuiKey_Minus;
		case 46:  return ImGuiKey_Period;
		case 47:  return ImGuiKey_Slash;
		case 59:  return ImGuiKey_Semicolon;
		case 61:  return ImGuiKey_Equal;
		case 91:  return ImGuiKey_LeftBracket;
		case 92:  return ImGuiKey_Backslash;
		case 93:  return ImGuiKey_RightBracket;
		case 96:  return ImGuiKey_GraveAccent;
		case 256: return ImGuiKey_Escape;
		case 257: return ImGuiKey_Enter;
		case 258: return ImGuiKey_Tab;
		case 259: return ImGuiKey_Backspace;
		case 260: return ImGuiKey_Insert;
		case 261: return ImGuiKey_Delete;
		case 262: return ImGuiKey_RightArrow;
		case 263: return ImGuiKey_LeftArrow;
		case 264: return ImGuiKey_DownArrow;
		case 265: return ImGuiKey_UpArrow;
		case 266: return ImGuiKey_PageUp;
		case 267: return ImGuiKey_PageDown;
		case 268: return ImGuiKey_Home;
		case 269: return ImGuiKey_End;
		case 280: return ImGuiKey_CapsLock;
		case 281: return ImGuiKey_ScrollLock;
		case 282: return ImGuiKey_NumLock;
		case 283: return ImGuiKey_PrintScreen;
		case 284: return ImGuiKey_Pause;
		case 340: return ImGuiKey_LeftShift;
		case 341: return ImGuiKey_LeftCtrl;
		case 342: return ImGuiKey_LeftAlt;
		case 343: return ImGuiKey_LeftSuper;
		case 344: return ImGuiKey_RightShift;
		case 345: return ImGuiKey_RightCtrl;
		case 346: return ImGuiKey_RightAlt;
		case 347: return ImGuiKey_RightSuper;
		case 348: return ImGuiKey_Menu;
		case 330: return ImGuiKey_KeypadDecimal;
		case 331: return ImGuiKey_KeypadDivide;
		case 332: return ImGuiKey_KeypadMultiply;
		case 333: return ImGuiKey_KeypadSubtract;
		case 334: return ImGuiKey_KeypadAdd;
		case 335: return ImGuiKey_KeypadEnter;
		case 336: return ImGuiKey_KeypadEqual;
		default: break;
		}
		if (Key >= 48  && Key <= 57)  return (ImGuiKey)(ImGuiKey_0      + (Key - 48));
		if (Key >= 65  && Key <= 90)  return (ImGuiKey)(ImGuiKey_A      + (Key - 65));
		if (Key >= 290 && Key <= 301) return (ImGuiKey)(ImGuiKey_F1     + (Key - 290));
		if (Key >= 320 && Key <= 329) return (ImGuiKey)(ImGuiKey_Keypad0 + (Key - 320));
		return ImGuiKey_None;
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

// (The frame's UI orchestration -- the game declares a persistent view tree in the UI
// view registry, and this feature translates it once per frame between NewFrame and
// Render via UI::TranslateRegisteredViews. The feature owns only the ImGui context +
// the draw-data translation; it issues no UI calls of its own.)

// Cross-DLL global accessor state (mirrors Resource::GetResourceSystem() and
// Log::GetLog()). Set at OnInstalled, cleared at PreUnInstall. The editor feature
// reaches the game-UI context via GetUI()->GetImGuiContext() to feed re-based input.
FUIFeature* GUIFeature = nullptr;

FUIFeature* GetUI()
{
	return GUIFeature;
}

FUIFeature::FUIFeature()
{
	// UI draws + submits LAST -- after every scene render feature's IEndRender
	// (their submits reach the queue first), so the UI composites over the scene.
	MyStage<IRenderUI>().IsWaiting<Scene::FScene>().ForStage<IEndRender>();
	MyStage<IRenderUI>().IsWaiting<FDrawTriangleFeature>().ForStage<IEndRender>();
	// The composite base (InitViews) resolves the SceneColor mirror by name and the
	// compose pass (RenderUI) samples that SAME texture. On a (re)size the scene's
	// IBeginRender -> EnsureTargets destroys + recreates SceneColor. Without a declared
	// edge, IInitViews (a parallel worker) can resolve the STALE pre-resize mirror before
	// the recreate lands; RenderUI then flips the NEW SceneColor to SR but draws the OLD
	// one (still COLOR_ATTACHMENT) -> vkCmdDraw layout-mismatch validation error. Pin the
	// data-flow edge so the reader always sees the current target.
	MyStage<IInitViews>().IsWaiting<Scene::FScene>().ForStage<IBeginRender>();
	// This feature is OFF-SCREEN ONLY now: it draws the ImGui list into its own
	// composite target and sets it as FRender's present target. It no longer owns
	// the present blit -- the frame feature does. Declare the reverse edge so the
	// frame feature's IPresent (the very last frame stage) runs AFTER this
	// feature's IRenderUI, and consumes the target it set. FFrameRenderFeature is
	// always installed, so this edges its IPresent behind us.
	MyStage<IRenderUI>().IsBlocking<FFrameRenderFeature>().OnStage<IPresent>();
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
		m_Context = ImGui::CreateContext();
		ImGuiIO& IO = ImGui::GetIO();
		IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // the editor shell docks later
		ImGui::StyleColorsDark();
		// Startup bake of the theme font x every size step (before the atlas is first
		// fetched below -- the atlas rasterizes on first GetTexDataAsRGBA32, so entries
		// added later would never make it in). Runtime never re-bakes.
		UI::BakeUIThemeFonts();
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

	// Publish the cross-DLL accessor so the editor feature can reach this context.
	GUIFeature = this;
	// Publish the GAME context through the UI plugin, so a game-side view owner (FUISystem)
	// can tag its views for THIS context without a UISystem -> UIFeature build dependency.
	UI::SetUIGameRenderContext(m_Context);
}

void FUIFeature::SetEditorInput(
	float X, float Y, bool B0, bool B1, bool B2,
	const Platform::MInputContext& Snap,
	const std::vector<Platform::MInputEvent>& Events,
	float WheelX, float WheelY)
{
	// Editor-build input takeover: the editor's pass0 IEditorInput stage feeds the game
	// context's IO with the cursor already re-based to THIS context's whole-window DisplaySize
	// coordinates (panel-local mapped back through the panel->window scale) + button state +
	// the FULL keyboard/char/wheel input the editor harvested from the platform this frame.
	// BEFORE this feature's InitViews runs. The DisplaySize is left to InitViews (whole-window);
	// only the input is injected here. Serialized behind the SAME ImGuiFrameMutex
	// InitViews uses, so a frame is never fed while another thread is mid-ImGui-frame. No-op
	// when the context isn't created yet.
	if (!bContextCreated || m_Context == nullptr)
	{
		return;
	}
	std::lock_guard<std::mutex> FrameLock(ImGuiFrameMutex);
	ImGui::SetCurrentContext(m_Context);
	ImGuiIO& IO = ImGui::GetIO();
	IO.AddMousePosEvent(X, Y);
	IO.AddMouseButtonEvent(0, B0);
	IO.AddMouseButtonEvent(1, B1);
	IO.AddMouseButtonEvent(2, B2);

	// Mods from the snapshot, fed explicitly as ImGuiMod_* (the ImGuiMod_Ctrl that
	// Shortcut() checks is derived from ImGuiMod_Ctrl key data, not the named Ctrl keys).
	IO.AddKeyEvent(ImGuiMod_Ctrl,   Snap.KeyDown[341] || Snap.KeyDown[345]);
	IO.AddKeyEvent(ImGuiMod_Shift,  Snap.KeyDown[340] || Snap.KeyDown[344]);
	IO.AddKeyEvent(ImGuiMod_Alt,    Snap.KeyDown[342] || Snap.KeyDown[346]);
	IO.AddKeyEvent(ImGuiMod_Super,  Snap.KeyDown[343] || Snap.KeyDown[347]);

	// Named keys + characters from the platform event batch the editor drained (the editor
	// is the ONE consumer this frame; this context receives a COPY of that batch). Feed
	// down=true on PRESS/REPEAT, down=false on RELEASE.
	for (const auto& Ev : Events)
	{
		if (Ev.Type == Platform::MInputEventType::Key)
		{
			const ImGuiKey K = MapGlfwKey(Ev.Key);
			if (K != ImGuiKey_None)
			{
				IO.AddKeyEvent(K, Ev.Action != 0);   // 0=RELEASE, 1=PRESS, 2=REPEAT
			}
		}
		else if (Ev.Type == Platform::MInputEventType::Char && Ev.Codepoint != 0)
		{
			IO.AddInputCharacter(Ev.Codepoint);      // text input
		}
	}

	// Mouse wheel (the editor already exchange-to-zero'd the platform accumulation).
	if (WheelX != 0.f || WheelY != 0.f)
	{
		IO.AddMouseWheelEvent(WheelX, WheelY);
	}
	bEditorInputThisFrame = true;
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

	// Select THIS feature's own context. The editor feature (an editor build) owns a
	// second ImGui context; without this the GImGui global could be left pointing at the
	// editor's context and this frame would be built against the wrong one.
	ImGui::SetCurrentContext(m_Context);

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
			// Recreated target's native starts UNDEFINED; it is re-transitioned Common ->
			// RenderTarget below, so the stale SR flag must be cleared or RenderUI's head
			// would emit a bogus ShaderResource -> RenderTarget barrier (oldLayout mismatch).
			bUIRenderTargetLayoutSR = false;
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
			// The composite target leaves create as UNDEFINED but is used as a dynamic-
			// rendering color attachment (BeginRendering declares COLOR_ATTACHMENT and
			// never auto-transitions). Bring it to COLOR_ATTACHMENT_OPTIMAL once, now,
			// so the first RenderUI's BeginRendering (and PresentTexture's "is a color
			// attachment" barrier) find it legal.
			{
				FRHITexture* RT = UIRenderTarget.GetRHI();
				R.AddPass(ERHICommandListType::Graphics, [RT](FRHICommandList& Cmd)
				{
					Cmd.TransitionTexture(RT, ERHIResourceState::Common, ERHIResourceState::RenderTarget);
				});
			}
		}
	}

	// -- Frame feed. In an editor build the editor's pass0 IEditorInput stage has ALREADY fed
	//    this context on the same frame (SetEditorInput: the panel-local cursor re-based back
	//    to the WHOLE-window DisplaySize coordinates + buttons), so bEditorInputThisFrame is
	//    set and we skip our own OS poll -- the game UI only responds inside the viewport panel
	//    but its layout stays in whole-window coordinates (the panel just displays the whole
	//    game surface scaled). Otherwise (pure game build, or the panel not yet present) fall
	//    back to the whole-window poll below. The DisplaySize is ALWAYS the whole window (game
	//    layout never re-scales to the panel). The WHOLE ImGui frame lifecycle is driven here:
	//    feed -> NewFrame -> build UI -> Render -> GetDrawData -> translate.
	ImGuiIO& IO = ImGui::GetIO();
	Platform::FPlatform* P = Platform::GetPlatform();
	if (P == nullptr)
	{
		return;
	}
	IO.DisplaySize = ImVec2(
		static_cast<float>(P->GetWindowWidth()),
		static_cast<float>(P->GetWindowHeight()));
	bool bEditorFed = bEditorInputThisFrame;
	bEditorInputThisFrame = false;
	if (!bEditorFed)
	{
		// Whole-window fallback poll (pure game build, or the panel not yet present).
		// Read the canonical client-space input snapshot produced by FPlatform's GLFW
		// callbacks (fired on the window-loop thread during PollEvents). The snapshot's
		// cursor is already relative to the window content-area top-left -- the same
		// space as IO.DisplaySize (whole window) -- so it feeds 1:1. No Win32 global
		// state any more; the IOContext is the single source of truth.
		Platform::MInputContext In;
		P->ReadInput(In);
		IO.AddMousePosEvent(In.MouseX, In.MouseY);
		IO.AddMouseButtonEvent(0, In.MouseButtons[0]);
		IO.AddMouseButtonEvent(1, In.MouseButtons[1]);
		IO.AddMouseButtonEvent(2, In.MouseButtons[2]);

		// Mods from the snapshot, fed explicitly as ImGuiMod_* (the ImGuiMod_Ctrl that
		// Shortcut() checks is derived from ImGuiMod_Ctrl key data, not the named Ctrl keys).
		IO.AddKeyEvent(ImGuiMod_Ctrl,   In.KeyDown[341] || In.KeyDown[345]);
		IO.AddKeyEvent(ImGuiMod_Shift,  In.KeyDown[340] || In.KeyDown[344]);
		IO.AddKeyEvent(ImGuiMod_Alt,    In.KeyDown[342] || In.KeyDown[346]);
		IO.AddKeyEvent(ImGuiMod_Super,  In.KeyDown[343] || In.KeyDown[347]);

		// Named keys + characters from the platform event stream (edges). A pure game
		// build is the only consumer (the editor feature doesn't exist), so the whole
		// batch is ours. Feed down=true on PRESS/REPEAT, down=false on RELEASE.
		{
			std::vector<Platform::MInputEvent> Events;
			P->DrainInputEvents(Events);
			for (const auto& Ev : Events)
			{
				if (Ev.Type == Platform::MInputEventType::Key)
				{
					const ImGuiKey K = MapGlfwKey(Ev.Key);
					if (K != ImGuiKey_None)
					{
						IO.AddKeyEvent(K, Ev.Action != 0);   // 0=RELEASE, 1=PRESS, 2=REPEAT
					}
				}
				else if (Ev.Type == Platform::MInputEventType::Char && Ev.Codepoint != 0)
				{
					IO.AddInputCharacter(Ev.Codepoint);      // text input
				}
			}
		}

		// Mouse wheel: exchange-to-zero (single consumer here in a pure game build).
		if (float Wx = 0.f, Wy = 0.f; (P->ConsumeMouseWheelXY(Wx, Wy), Wx != 0.f || Wy != 0.f))
		{
			IO.AddMouseWheelEvent(Wx, Wy);
		}
	}

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

	// Composite base: draw the scene (the SceneColor mirror) as a fullscreen background
	// image underneath every UI control. The UI surface is the final on-screen layer;
	// the scene is the base it composites over -- if nothing here samples SceneColor,
	// the UI target's black clear is all you see (the scene pass ran, but was never
	// read into the UI). The TextureId is the SceneColor mirror's FName id -- the same
	// key the translate step below resolves through GetMirror, so the scene's offscreen
	// target is sampled by the UI fragment shader. AddImage records on the background
	// layer, so the Game UI window (foreground) draws on top.
	if (Scene::FScene* Scene = Scene::GetScene())
	{
		if (Scene->GetSceneColor().IsValid())
		{
			// Draw the scene as a fixed, non-interactive FULLSCREEN background window so
			// the Composit layer is stable: a window has its own clip rect + coordinates,
			// so it cannot be distorted by another window being dragged (a global
			// background list AddImage gets re-affected by the active window's transform,
			// which made the scene stretch when the Game UI window moved). This window is
			// Begin'd FIRST, so the Game UI window (Begin'd later in the game commands)
			// draws on top of it.
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
			ImGui::SetNextWindowSize(IO.DisplaySize, ImGuiCond_Always);
			if (ImGui::Begin("##SceneBackground", nullptr,
				ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
				| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
				| ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse
				| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs
				| ImGuiWindowFlags_NoBringToFrontOnFocus))
			{
				const ImVec2 P0 = ImGui::GetWindowPos();
				const ImVec2 P1 = ImVec2(P0.x + ImGui::GetWindowWidth(), P0.y + ImGui::GetWindowHeight());
				ImGui::GetWindowDrawList()->AddImage(
					static_cast<ImTextureID>(Name::FName("SceneColor").GetId()),
					P0, P1, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, 255));
			}
			ImGui::End();
		}
	}

	// Translate every view registered for THIS context (the UI plugin owns the walk:
	// shell/overlay handling, docking, size derivation, then the tree traversal). The
	// game side declares a PERSISTENT tree and never issues an ImGui call; the tree is
	// read under the view's shared lock for the whole traversal, so a game-thread
	// `Edit()` (exclusive write) simply waits for this frame's translation to end.
	// Views tagged with another context (or none) are skipped inside the translator --
	// this feature only ever renders its own context. The display rect is the WHOLE
	// window (game layout never re-scales to an editor panel), i.e. the same space the
	// input feed above used.
	UI::FUIViewFrameDesc FrameDesc;
	FrameDesc.ImGuiContext = m_Context;
	FrameDesc.DisplayWidth = IO.DisplaySize.x;
	FrameDesc.DisplayHeight = IO.DisplaySize.y;
	UI::TranslateRegisteredViews(FrameDesc);

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
		// No draws this frame, but the editor's pass3 always samples GetPresentTarget()
		// (the game composite) and NEVER re-flips it -- it relies on pass2 leaving
		// UIRenderTarget in SHADER_READ_ONLY for exactly that sampled read. So even on a
		// no-draw frame we must keep the present slot on UIRenderTarget and (editor build)
		// flip it to SR; otherwise pass3 reads a COLOR_ATTACHMENT texture as a shader
		// resource (descriptors hardcode SHADER_READ_ONLY) and the validator fires. The
		// head flip below undoes this the next time a draw actually happens.
		R.SetPresentTarget(UIRenderTarget);
#ifdef MAHO_EDITOR_BUILD
		TransitionUIRenderTargetForSampling(R);
#endif
		return;
	}

	// -- Draw pass -- one subpass via the TYPED AddPass. AddPass resolves the PSO from
	//    the pass input layout + the target, starts dynamic rendering, BINDS the
	//    pipeline implicitly, and only then runs this lambda -- so the lambda records
	//    ONLY the draws (viewport / binds / push constant / draw calls). The feature
	//    never queries a pipeline or calls BeginRendering/BindGraphicsPipeline itself.
	// UIRenderTarget doubles, in an editor build, as the viewport's sampled present target.
	// If the previous frame's pass3 sampled it (flipped to SHADER_READ_ONLY), bring it back
	// to COLOR_ATTACHMENT here before we clear+redraw -- the RHI never auto-transitions.
	if (bUIRenderTargetLayoutSR)
	{
		FRHITexture* RT = UIRenderTarget.GetRHI();
		R.AddPass(ERHICommandListType::Graphics, [RT](FRHICommandList& Cmd)
		{
			Cmd.TransitionTexture(RT, ERHIResourceState::ShaderResource, ERHIResourceState::RenderTarget);
		});
		bUIRenderTargetLayoutSR = false;
	}
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
	// The UI composites the scene-color mirror (game imgui::image of SceneColor) which
	// the scene left as COLOR_ATTACHMENT, but descriptor writes hardcode SHADER_READ_ONLY.
	// Flip SceneColor to SR before this compose pass samples it, and back afterwards.
	if (Scene::FScene* Scene = Scene::GetScene())
	{
		Scene->TransitionSceneColorForSampling(R);
	}
	R.AddPass(ERHICommandListType::Graphics, PipelineDesc, Target, Params, DrawList);
	if (Scene::FScene* Scene = Scene::GetScene())
	{
		Scene->TransitionSceneColorForRendering(R);
	}

	// This feature is off-screen only: it composites into its own target then sets it
	// as FRender's present target. The frame feature's IPresent (a later graph stage,
	// blocked on this stage) blits it to the swapchain. No present happened here.
	//
	// Editor build: pass3 (editor's IEditorCompose) takes over the final present. But the
	// viewport window in pass3 samples "the current on-screen surface" via GetPresentTarget()
	// -- the game composite UIRenderTarget is that surface (scene + game UI already
	// composited). So pass2 MUST STILL set the present target here; pass3 reads it as its
	// viewport background and only THEN replaces it with EditorRT at the very end. Without
	// this, the viewport would sample a stale/empty present target.
	R.SetPresentTarget(UIRenderTarget);
#ifdef MAHO_EDITOR_BUILD
	// Editor build: pass3 samples this present target as its viewport background (the
	// present-target ImTextureID resolves to GetPresentTarget() == this composite). Flip it
	// to SHADER_READ_ONLY so that sampled read is legal; pass3's RenderEditorUI flips it back
	// to COLOR_ATTACHMENT afterwards, and RenderUI's head flips it back before the next write.
	TransitionUIRenderTargetForSampling(R);
#endif
}

void FUIFeature::TransitionUIRenderTargetForSampling(FRender& R)
{
	// Flip UIRenderTarget from COLOR_ATTACHMENT (where the composite just wrote it) to
	// SHADER_READ_ONLY so a downstream sampled use (pass3's viewport imgui::image of the
	// present target) can bind it legally. The RHI never auto-transitions and descriptor
	// writes hardcode SHADER_READ_ONLY, so sampling a target still in COLOR_ATTACHMENT
	// trips the validation layer. RenderUI's head flips it back before any write.
	if (UIRenderTarget.IsValid() && !bUIRenderTargetLayoutSR)
	{
		FRHITexture* RT = UIRenderTarget.GetRHI();
		R.AddPass(ERHICommandListType::Graphics, [RT](FRHICommandList& Cmd)
		{
			Cmd.TransitionTexture(RT, ERHIResourceState::RenderTarget, ERHIResourceState::ShaderResource);
		});
		bUIRenderTargetLayoutSR = true;
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
	// This feature owns the ImGui context, so it tears it down here. Unpublish first:
	// a view owner that registers between now and its own teardown must not adopt a dead
	// context (`GetUIGameRenderContext()` returning null makes it retry, never crash).
	UI::SetUIGameRenderContext(nullptr);
	if (bContextCreated)
	{
		// Drop this context's baked font entries BEFORE the atlas dies: the registry stores
		// raw ImFont* (the editor's context keeps its own, keyed separately).
		UI::ClearUIFonts(static_cast<void*>(m_Context));
		ImGui::SetCurrentContext(nullptr);
		ImGui::DestroyContext(m_Context);
		m_Context = nullptr;
		bContextCreated = false;
	}
}

} // namespace Maho

// The C export FRender looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_UIFEATURE_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FUIFeature::CreateLayer();
}
