#include "ExampleEditor.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <Core/Profiler.h>
#include <Log.h>
#include <Name.h>
#include <Platform.h>
#include <Scene.h>
#include <UIFeature.h>
#include <UIView.h>
#include <UIViewRegistry.h>
#include <UITranslate.h>
#include <UIClipboard.h>
#include <UIResource.h>
#include <RHI/RHICommandList.h>
#include <RHI/RHIEnums.h>
#include <RHI/RHIResources.h>
#include <ShaderParameterStruct.h>

#include "imgui.h"

#include "ImGuiTheme.h"

// ImTextureID for the live present target (game composite UIRenderTarget). The name is
// never a registered mirror (the target is CreateTexture'd directly, no mirror entry), so
// the host's translate step special-cases this id -> R.GetPresentTarget().
constexpr const char* kPresentTargetTexName = "__EditorPresentTarget__";

#if defined(_WIN32)
#	include <windows.h>
#endif

namespace
{
#if defined(_WIN32)
	// No GLFW/OS clipboard backend: route ImGui's SetClipboardText straight into the
	// system clipboard so Ctrl+C inside a readonly InputText copies to the OS.
	void SetSystemClipboard(const char* Text)
	{
		if (Text == nullptr)
		{
			return;
		}
		if (!::OpenClipboard(nullptr))
		{
			return;
		}
		::EmptyClipboard();
		const std::size_t Len = std::strlen(Text);
		if (HGLOBAL Mem = ::GlobalAlloc(GMEM_MOVEABLE, Len + 1))
		{
			if (void* Ptr = ::GlobalLock(Mem))
			{
				std::memcpy(Ptr, Text, Len + 1);
				::GlobalUnlock(Mem);
				::SetClipboardData(CF_TEXT, Mem);
			}
			else
			{
				::GlobalFree(Mem);
			}
		}
		::CloseClipboard();
	}

	// Read the system clipboard as UTF-8. CF_UNICODETEXT first (what other applications put there),
	// falling back to CF_TEXT for the ANSI form SetSystemClipboard writes. This is the half Ctrl+V
	// needs: without it the editor's ImGui context has no clipboard SOURCE at all, so paste silently
	// does nothing even though copy works.
	std::string GetSystemClipboard()
	{
		std::string Out;
		if (!::OpenClipboard(nullptr))
		{
			return Out;
		}
		if (HANDLE H = ::GetClipboardData(CF_UNICODETEXT))
		{
			if (const wchar_t* W = static_cast<const wchar_t*>(::GlobalLock(H)))
			{
				const int Need = ::WideCharToMultiByte(CP_UTF8, 0, W, -1, nullptr, 0, nullptr, nullptr);
				if (Need > 1)
				{
					// Two sizes, deliberately: the conversion writes Need bytes INCLUDING the
					// terminating NUL, so it needs room for all of them; the string then drops the NUL.
					// Resizing to Need-1 while passing Need wrote one byte past the buffer.
					Out.resize(static_cast<std::size_t>(Need));
					::WideCharToMultiByte(CP_UTF8, 0, W, -1, Out.data(), Need, nullptr, nullptr);
					Out.resize(static_cast<std::size_t>(Need - 1));
				}
				::GlobalUnlock(H);
			}
		}
		else if (HANDLE H = ::GetClipboardData(CF_TEXT))
		{
			if (const char* A = static_cast<const char*>(::GlobalLock(H)))
			{
				Out = A;
				::GlobalUnlock(H);
			}
		}
		::CloseClipboard();
		return Out;
	}
#endif // _WIN32

	// Map a GLFW key code (the index used in MInputContext::KeyDown / MInputEvent::Key) to
	// the ImGui named key. ImGui keys are a dense named enum (ImGuiKey_A..), NOT the raw
	// GLFW code; the named key IS the real input (arrows, letters, shortcuts). ImGui's
	// virtual mods (ImGuiMod_Ctrl...) are fed separately from the snapshot.
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

	// Editor ImGui vertex shader (identical to the game UI shader -- same GLSL).
	constexpr const char* kEditorVertShader = R"(
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

	constexpr const char* kEditorFragShader = R"(
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
	FRHISamplerDesc EditorClampSamplerDesc()
	{
		FRHISamplerDesc S;
		S.AddressU = ERHIAddressMode::ClampToEdge;
		S.AddressV = ERHIAddressMode::ClampToEdge;
		S.AddressW = ERHIAddressMode::ClampToEdge;
		return S;
	}

	std::uint32_t FExampleEditor::PresentTargetTextureId()
	{
		return Name::FName(kPresentTargetTexName).GetId();
	}

	UI::FUIName FExampleEditor::PresentTargetName()
	{
		return UI::FUIName(kPresentTargetTexName);
	}
}

namespace Maho
{
	// Compile-time FParameters for the editor draw pass (identical shape to the game UI):
	// vertex-stage mat4 push constant + set0/binding0 font sampler.
	BEGIN_SHADER_PARAMETER_STRUCT(FEditorParameters)
		SHADER_PARAMETER_TEXTURE_SAMPLER(Texture2D, FontTexture, FontSampler, 0, 0, ERHIShaderStage::Fragment)
		SHADER_PARAMETER_ARRAY(float, OrthoMatrix, 16)
	END_SHADER_PARAMETER_STRUCT()

	// Statics of FEditorShader: expose the feature-private GLSL to the shader type.
	const char* FEditorShader::GetVertexSource()       { return kEditorVertShader; }
	const char* FEditorShader::GetFragmentSource()     { return kEditorFragShader; }
	const char* FEditorShader::GetVertexEntryPoint()   { return "main"; }
	const char* FEditorShader::GetFragmentEntryPoint() { return "main"; }
}

namespace Maho
{

FExampleEditor::FExampleEditor()
{
	// CONFLICT #1 -- a missing dependency edge: both UI features write PROCESS-GLOBAL
	// ImGui state at install.
	//
	// Both OnInstalled bodies run ImGui::CreateContext / GetIO / the theme-font atlas
	// bake, all of which read and write the ONE global "current context", and
	// ImGuiFrameMutex is a PER-FEATURE member -- it excludes nothing across two features.
	// With no edge between the two install roots they run concurrently in the render
	// feature install graph and corrupt that global: the game feature's InitViews then
	// trips IM_ASSERT(g.Initialized) inside ImGui::NewFrame(), and both install nodes
	// fail to return -- which parks every later frame of FRender's chain and, through
	// FRender::ITick, the host graph too.
	//
	// The per-frame edges below order the two features' FRAMES; this one orders their
	// INSTALLS.
	MyStage<IOnInstalled>().IsWaiting<FUIFeature>().ForStage<IOnInstalled>();
	// Pass0 INPUT takeover: the editor re-bases the Win32 cursor to the viewport panel and
	// feeds the game-UI context (SetEditorInput) BEFORE the game feature's IInitViews feeds
	// + NewFrame()s its IO. Without this edge the game UI could run its feed/NewFrame first
	// and consume last frame's (or a whole-window) cursor, so the input lands one frame late
	// or scaled to the whole window. BlockOn: FUIFeature::IInitViews runs AFTER my EditorInput.
	MyStage<IEditorInput>().IsBlocking<FUIFeature>().OnStage<IInitViews>();
	// The editor is the LAST UI writer: it must run AFTER the game UI feature's
	// IRenderUI so its SetPresentTarget(EditorRT) wins the present slot over the game
	// composite. Without this edge the two UI features could run in any order and the
	// editor surface could be overridden back to the game RT.
	MyStage<IEditorCompose>().IsWaiting<FUIFeature>().ForStage<IRenderUI>();
	// Editor runs its compose LAST among the recorders, after the scene's IEndRender (the scene
	// color mirror the viewport samples is written by then). It sets the EditorRT as the present
	// target, and the frame's submission point + blit is the recorded collector's tail stage
	// (FScene::IPresent) -- declared to run after this stage below, since the graph has no stage
	// barrier and only a declared edge keeps a recorded pass inside its own frame.
	MyStage<IEditorCompose>().IsWaiting<Scene::FScene>().ForStage<IEndRender>();
	MyStage<IEditorCompose>().IsBlocking<Scene::FScene>().OnStage<IPresent>();
	// Pass0 -> pass3 hand-off crosses a FRAME boundary, and the render graph pipelines frames
	// (FRender::Tick's Execute does not drain): InitEditorViews@N reads the input cache
	// (EditorInputEvents / EditorWheelX / bEditorInputCached) while EditorInput@N+1 may already
	// be clearing it. That race silently drops a frame's keys/wheel -- and it is invisible from
	// either stage alone. Pin the intra-layer cross-frame edge; same shape as FRender's own
	// frame-isolation edge (IBeginFrame@N+1 waits for IEndFrame@N).
	MyStage<IEditorInput>().WaitFor<FExampleEditor>().OnLastFrameStage<IEditorCompose>();
	// Teardown serialization, mirroring the frame edges above: FRender::Shutdown runs every
	// feature's IPreUnInstall as one batch, and my body releases the editor target + destroys
	// the editor's own ImGui context while the other two features release theirs. Pin my
	// teardown AFTER the scene's (which the game UI feature's is pinned after) so the batch
	// never executes two teardown bodies at once.
	MyStage<IPreUnInstall>().IsWaiting<Scene::FScene>().ForStage<IPreUnInstall>();
}

void FExampleEditor::EditorInput(FRender& R, FRenderContext& Frame)
{
	// Editor-build input takeover: taste the Win32 cursor, confine it to the viewport panel
	// (clamp panel-local) and map it BACK to the game UI's whole-window coordinate space, then
	// feed the game-UI context BEFORE its InitViews NewFrame()s it (ordered via the ctor
	// BlockOn<IEditorInput, FUIFeature, IInitViews>). The game UI keeps laying out in whole-
	// window coordinates; the viewport merely displays the whole game surface scaled into the
	// panel, so panel_local / panel_size * window_size gives the game-space position -- this is
	// what makes a game widget respond only inside the panel at exactly its displayed location.
	// The viewport component publishes the panel rect (editor display-space) via
	// ReportViewportRect during its Draw; if the panel isn't present yet (or the context isn't
	// created) leave the game UI to its whole-window fallback (no SetEditorInput call this frame).
	(void)R;

	// OS file drop: drained BEFORE the viewport guards below, so a drop still reaches the
	// panels when the game UI has no panel to present into (the batch is per-frame either way).
	if (Platform::FPlatform* DropSource = Platform::GetPlatform())
	{
		DroppedFiles.clear();
		DropSource->DrainDroppedFiles(DroppedFiles);
	}

	if (m_Context == nullptr || !bVpValid)
	{
		return;
	}
	Platform::FPlatform* P = Platform::GetPlatform();
	if (P == nullptr)
	{
		return;
	}
	const float WinW = static_cast<float>(P->GetWindowWidth());
	const float WinH = static_cast<float>(P->GetWindowHeight());

	// Feed the GAME-UI context from the platform's tagged input ring, one frame at a time: this
	// consumer owns GameInputCursor and reads every frame it has not fed yet, in order, exactly
	// once. Nothing is drained, so the editor's own context (pass3) reading the same ring cannot
	// starve it -- a shared drain is exactly how a key RELEASE got lost and ImGui kept the key
	// down, auto-repeating it.
	MAHO_TRACE_SCOPE("FExampleEditor::EditorInput.FeedGameUI");
	FUIFeature* UI = GetUI();
	const std::uint64_t Latest = P->GetInputFrameIndex();
	if (Latest < GameInputCursor)
	{
		GameInputCursor = 0;   // window recreated: the platform reset its ring
	}
	while (GameInputCursor < Latest)
	{
		++GameInputCursor;
		Platform::FInputFrame Frame;
		if (!P->ReadInputFrame(GameInputCursor, Frame))
		{
			continue;   // evicted -- this consumer fell behind the ring
		}
		const Platform::MInputContext& In = Frame.Snapshot;

		// Confine to the viewport panel: clamp panel-local, then scale back to whole-window
		// game-space (panel_local / panel_size * window_size gives the game-space position --
		// what makes a game widget respond only inside the panel at exactly its display spot).
		const float Lx = std::clamp(In.MouseX - VpX, 0.f, VpW);
		const float Ly = std::clamp(In.MouseY - VpY, 0.f, VpH);
		const float Gx = VpW > 0.f ? Lx * (WinW / VpW) : 0.f;
		const float Gy = VpH > 0.f ? Ly * (WinH / VpH) : 0.f;
		if (UI != nullptr)
		{
			// The re-based cursor comes from here (only the editor knows the panel rect); the
			// frame's mods / edge events / wheel the game context reads itself, from this same slot.
			UI->SetEditorInput(GameInputCursor, Gx, Gy,
				In.MouseButtons[0], In.MouseButtons[1], In.MouseButtons[2]);
		}
	}
}

bool FExampleEditor::EnsureUIBackend(FRender& R)
{
	MAHO_TRACE_SCOPE("FExampleEditor::EnsureUIBackend");
	if (bUIInit)
	{
		return true;
	}
	ImFontAtlas* Fonts = ImGui::GetIO().Fonts;
	unsigned char* Pixels = nullptr;
	int FontW = 0, FontH = 0, FontBpp = 0;
	Fonts->GetTexDataAsRGBA32(&Pixels, &FontW, &FontH, &FontBpp);
	if (Pixels == nullptr || FontW <= 0 || FontH <= 0)
	{
		MAHO_LOG_CORE_ERROR("ExampleEditor: no font atlas");
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
	FontTexture = R.CreateTexture(TexDesc, ERDGResourceLifetime::Persistent);
	if (!FontTexture.IsValid())
	{
		MAHO_LOG_CORE_ERROR("ExampleEditor: font texture failed");
		return false;
	}
	Fonts->TexID = 0;
	bUIInit = true;
	MAHO_LOG_CORE_INFO("ExampleEditor: font backend ready");
	return true;
}

void FExampleEditor::UploadFont(FRender& R)
{
	if (bFontUploaded)
	{
		return;
	}
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
			// PERSISTENT, for the same reason as the game UI's font staging: the pass is recorded the
			// first time the editor's UI backend comes up (possibly before the frame head), and its
			// list is submitted at the frame's tail -- a transient would be recycled in between.
			FRDGBufferRef Staging = R.CreateBuffer(StagingDesc, ERDGResourceLifetime::Persistent);
			if (!Staging.IsValid() || Staging.GetRHI() == nullptr)
			{
				MAHO_LOG_CORE_ERROR("ExampleEditor: font staging failed");
				return;
			}
			Cmd.UpdateBuffer(Staging.GetRHI(), 0, static_cast<std::uint64_t>(FontW) * FontH * 4, Pixels);
			Cmd.TransitionTexture(FontTexture.GetRHI(), ERHIResourceState::Common, ERHIResourceState::CopyDst);
			Cmd.CopyBufferToTexture(Staging.GetRHI(), FontTexture.GetRHI(), 0);
			Cmd.TransitionTexture(FontTexture.GetRHI(), ERHIResourceState::CopyDst, ERHIResourceState::ShaderResource);
		}
		bFontUploaded = true;
	});
}

void FExampleEditor::OnInstalled(FRender& R, FRenderContext& Frame)
{
	// THIS feature owns the editor's OWN ImGui context -- fully isolated from the game's
	// UIFeature context. Created at install, before anything touches GetIO(); it becomes
	// the current context for the rest of frame setup (the game feature switches back to
	// its own context at its InitViews). Docking is enabled so the editor shell can dock.
	// NO GLFW backend: input + display size are fed by hand in InitEditorViews (same model
	// as FUIFeature), so there is no process-wide ImGui_ImplGlfw single-instance to clash
	// with anything.
	if (m_Context == nullptr)
	{
		MAHO_TRACE_SCOPE("FExampleEditor::OnInstalled.SetupContext");
		Platform::FPlatform* P = Platform::GetPlatform();
		if (P == nullptr || P->GetWindowWidth() == 0 || P->GetToolkitWindowHandle() == nullptr)
		{
			MAHO_LOG_CORE_ERROR("ExampleEditor::OnInstalled: no window; editor disabled");
			return;
		}
		IMGUI_CHECKVERSION();
		m_Context = ImGui::CreateContext();
		ImGui::SetCurrentContext(m_Context);
		ImGuiIO& IO = ImGui::GetIO();
		IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		IO.IniFilename = "EditorLayout.ini";
		// The editor's OWN startup bake: same theme font x size steps, but into THIS
		// context's atlas (font entries are keyed per context, so the game's baked fonts
		// are never handed to the editor). Must precede the first atlas fetch.
		UI::BakeUIThemeFonts();
#if defined(_WIN32)
		// 1.91 InputText copy/paste gates on g.PlatformIO (not io.SetClipboardTextFn): the copy side
		// so Ctrl+C in a readonly InputText writes the system clipboard, and the PASTE side so Ctrl+V
		// has a source -- without it paste silently does nothing. The returned pointer must stay valid
		// until ImGui copies it, hence the thread-local buffer.
		ImGuiPlatformIO& PIO = ImGui::GetPlatformIO();
		PIO.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* Text) { SetSystemClipboard(Text); };
		PIO.Platform_GetClipboardTextFn = [](ImGuiContext*)
		{
			static thread_local std::string Buffer;
			Buffer = GetSystemClipboard();
			return Buffer.c_str();
		};
#endif
		ApplyMahoNightTheme();
		MAHO_LOG_CORE_INFO("ExampleEditor: ImGui context created (editor-own, no GLFW backend)");

		// Resource resolution for the declarative tree. v1 resolves exactly ONE name -- the
		// present target -- so a panel can `FUIImage` the live surface; every other reference
		// (icons, thumbnails) stays unresolved and the backend draws its placeholder. The UI
		// plugin never links Render, so the mapping lives here (the side that owns the RHI).
		// This is a REGISTRATION, not a bare set: the slot lives on the UI frame while the
		// closure's code lives in THIS module, so the token is handed back in PreUnInstall.
		ResolverToken = UI::BindUIResourceResolver(GetName(), [](const UI::FUIName& Resource, bool bIsFont)
		{
			UI::FUIResolvedResource Out;
			Out.Name = Resource;
			if (bIsFont) { return Out; }
			if (Resource == FExampleEditor::PresentTargetName())
			{
				Out.bValid = true;
				Out.NativeHandle = FExampleEditor::PresentTargetTextureId();
			}
			return Out;
		});

#if defined(_WIN32)
		// Text clipboard: the host owns the window, so it injects the platform capability
		// into the UI plugin (which has no Platform dependency). Both directions: the UI plugin's
		// own copy/paste (log area, context menus) reads through the getter, and Ctrl+V in a text
		// box needs the same source (see the ImGui platform IO above).
		ClipboardToken = UI::BindUIClipboardHandlers(GetName(),
			nullptr,   // DISABLED FOR A TEST: the Win32 clipboard read path is suspected in the startup hang
			[](std::string_view Text)
			{
				const std::string Owned(Text);   // 立刻拷贝进系统剪贴板，不留悬垂指针
				SetSystemClipboard(Owned.c_str());
			});
#endif
	}

	ImGui::SetCurrentContext(m_Context);
	if (!EnsureUIBackend(R))
	{
		return;
	}
	if (!bFontUploaded)
	{
		UploadFont(R);
		if (!bFontUploaded)
		{
			return;
		}
	}
	// Install the editor component plugins (viewport) + run their Init graph at the next
	// safe point, now that this feature's own ImGui context + font backend are ready.
	InstallEditorComponents();
}

void FExampleEditor::InstallEditorComponents()
{
	MAHO_TRACE_SCOPE("FExampleEditor::InstallEditorComponents");
	// Editor components (viewport, console, theme) are declaratively listed in
	// ExampleEditor.cplugin Plugins and installed into this host's collector by
	// module base name at the next safe point (their IEditorInit graph runs on
	// FlushPendingUpdates). A component with children of its own would
	// install them itself, into this same collector.
	InstallChildrenOf(GetName());
	FlushPendingUpdates<TTypeList<IEditorInit>, TTypeList<IEditorShutdown>>();
}

void FExampleEditor::InitEditorViews(FRender& R)
{
	if (m_Context == nullptr || !bUIInit)
	{
		return;
	}

	// Switch to the editor's OWN context for the whole frame. Two UI features share the
	// process-wide ImGui DLL; each must select its own context so a frame is never built
	// against the wrong one. UIFeature does the same for its own context.
	ImGui::SetCurrentContext(m_Context);

	// Same reasoning as FUIFeature's InitViews: our input is a per-frame SNAPSHOT (one MousePos
	// + one button state), not an event stream, so event trickling only defers state to the next
	// frame -- which reads as stickiness when a button is released mid-motion.
	{
		ImGuiIO& FrameIO = ImGui::GetIO();
		FrameIO.ConfigInputTrickleEventQueue = false;
	}

	std::lock_guard<std::mutex> FrameLock(ImGuiFrameMutex);

	// Record the frame render target the components reach through this host.
	EditorContext.bSceneReady = (Scene::GetScene() != nullptr);

	// Rebuild the editor composite target (EditorRT) to the current canvas.
	{
		MAHO_TRACE_SCOPE("FExampleEditor::InitEditorViews.RebuildTarget");
		const std::uint32_t CanvasW = R.GetCanvasWidth();
		const std::uint32_t CanvasH = R.GetCanvasHeight();
		if (CanvasW == 0 || CanvasH == 0)
		{
			return;
		}
		if (!EditorRT.IsValid() || EditorRT.GetWidth() != CanvasW || EditorRT.GetHeight() != CanvasH)
		{
			if (EditorRT.IsValid())
			{
				R.ReleaseTexture(EditorRT);
				EditorRT.Reset();
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
			EditorRT = R.CreateTexture(Desc, ERDGResourceLifetime::Persistent);
			if (!EditorRT.IsValid())
			{
				MAHO_LOG_CORE_ERROR("ExampleEditor: EditorRT creation failed");
				return;
			}
			// EditorRT leaves create as UNDEFINED but is used as a dynamic-rendering color
			// attachment (BeginRendering declares COLOR_ATTACHMENT and never auto-transitions).
			// Bring it to COLOR_ATTACHMENT_OPTIMAL once, now, so RenderUI's BeginRendering and
			// PresentTexture's "is a color attachment" barrier find it legal.
			{
				FRHITexture* RT = EditorRT.GetRHI();
				R.AddPass(ERHICommandListType::Graphics, [RT](FRHICommandList& Cmd)
				{
					Cmd.TransitionTexture(RT, ERHIResourceState::Common, ERHIResourceState::RenderTarget);
				});
			}
		}
	}

	// -- Frame feed (manual Win32 input, same model as FUIFeature) -- then NewFrame.
	// No GLFW backend: we set the display size from the platform window and poll cursor
	// + mouse-button state directly via Win32. The editor is the only input-consuming
	// ImGui context in an editor build; the game UI feature stays cold.
	ImGuiIO& IO = ImGui::GetIO();
	Platform::FPlatform* P = Platform::GetPlatform();
	if (P == nullptr)
	{
		return;
	}
	IO.DisplaySize = ImVec2(
		static_cast<float>(P->GetWindowWidth()),
		static_cast<float>(P->GetWindowHeight()));
#if defined(_WIN32)
	// Feed THIS context from the platform's tagged ring, with its own cursor. Mouse/buttons/mods
	// are per-frame STATE and ride the frame that carries them; the edge events (keys, characters,
	// wheel) are applied in the order those frames were pumped, so a key RELEASE cannot be skipped
	// -- which is why this reads per frame instead of draining a shared stream (a stolen release
	// left ImGui believing the key was still down, auto-repeating it).
	MAHO_TRACE_SCOPE("FExampleEditor::InitEditorViews.FeedInput");
	{
		const std::uint64_t Latest = P->GetInputFrameIndex();
		if (Latest < EditorInputCursor)
		{
			EditorInputCursor = 0;   // window recreated: the platform reset its ring
		}
		while (EditorInputCursor < Latest)
		{
			++EditorInputCursor;
			Platform::FInputFrame Frame;
			if (!P->ReadInputFrame(EditorInputCursor, Frame))
			{
				continue;   // evicted -- this consumer fell behind the ring
			}
			const Platform::MInputContext& In = Frame.Snapshot;
			IO.AddMousePosEvent(In.MouseX, In.MouseY);
			IO.AddMouseButtonEvent(0, In.MouseButtons[0]);
			IO.AddMouseButtonEvent(1, In.MouseButtons[1]);
			IO.AddMouseButtonEvent(2, In.MouseButtons[2]);

			// Mods from the frame's snapshot, fed explicitly as ImGuiMod_* (the ImGuiMod_Ctrl that
			// Shortcut() checks is derived from the ImGuiMod_Ctrl key data -- NOT from the
			// LeftCtrl/RightCtrl named keys, per the Win32 backend).
			IO.AddKeyEvent(ImGuiMod_Ctrl,   In.KeyDown[341] || In.KeyDown[345]);
			IO.AddKeyEvent(ImGuiMod_Shift,  In.KeyDown[340] || In.KeyDown[344]);
			IO.AddKeyEvent(ImGuiMod_Alt,    In.KeyDown[342] || In.KeyDown[346]);
			IO.AddKeyEvent(ImGuiMod_Super,  In.KeyDown[343] || In.KeyDown[347]);

			// First, release whatever the PREVIOUS feed had to hold back (see DeferredKeyReleases):
			// the key has now been down for a whole ImGui frame, so ImGui has seen the press.
			for (const int KeyCode : DeferredKeyReleases)
			{
				const ImGuiKey K = MapGlfwKey(KeyCode);
				if (K != ImGuiKey_None)
				{
					IO.AddKeyEvent(K, false);
				}
			}
			DeferredKeyReleases.clear();
			for (bool& bPressed : KeyPressedThisFeed)
			{
				bPressed = false;
			}

			for (const auto& Ev : Frame.Events)
			{
				if (Ev.Type == Platform::MInputEventType::Key)
				{
					const ImGuiKey K = MapGlfwKey(Ev.Key);
					if (K != ImGuiKey_None)
					{
						const bool bReleasedInRange = Ev.Key >= 0 && Ev.Key < Platform::MInputContext::KeyCount;
						if (Ev.Action != 0)   // 0=RELEASE, 1=PRESS, 2=REPEAT
						{
							IO.AddKeyEvent(K, true);
							if (bReleasedInRange)
							{
								KeyPressedThisFeed[Ev.Key] = true;
							}
						}
						else if (bReleasedInRange && KeyPressedThisFeed[Ev.Key])
						{
							// A tap delivered entirely inside this feed: hold the release so the press
							// occupies one ImGui frame instead of cancelling itself out.
							DeferredKeyReleases.push_back(Ev.Key);
						}
						else
						{
							IO.AddKeyEvent(K, false);
						}
					}
				}
				else if (Ev.Type == Platform::MInputEventType::Char && Ev.Codepoint != 0)
				{
					IO.AddInputCharacter(Ev.Codepoint);      // text input (console / input boxes)
				}
			}

			// The wheel rides the frame it was accumulated in (no exchange-to-zero, so a second
			// consumer -- the game context -- reads the same delta instead of an empty one).
			if (In.MouseWheelX != 0.f || In.MouseWheelY != 0.f)
			{
				IO.AddMouseWheelEvent(In.MouseWheelX, In.MouseWheelY);
			}
		}
	}
#endif

	unsigned char* FontPixels = nullptr;
	int FontW = 0, FontH = 0, FontBpp = 0;
	IO.Fonts->GetTexDataAsRGBA32(&FontPixels, &FontW, &FontH, &FontBpp);
	if (FontPixels == nullptr || FontW <= 0 || FontH <= 0)
	{
		MAHO_LOG_CORE_ERROR("ExampleEditor: font atlas not built");
		return;
	}
	// 声明期先于后端帧：组件只改自己的 UI 树，绝不碰 ImGui（此顺序是 §10.7 的硬约束）。
	UpdateEditorPanels();

	ImGui::NewFrame();

	// Dock frame shell (host-owned) + each editor component draws its own window.
	DrawEditorPanels();

	ImGui::Render();
	ImDrawData* DrawData = ImGui::GetDrawData();
	if (DrawData == nullptr || !DrawData->Valid || DrawData->CmdListsCount <= 0)
	{
		return;
	}

	// Translate ImDrawData -> FDrawList (merged vertex/index buffers uploaded here).
	MAHO_TRACE_SCOPE("FExampleEditor::InitEditorViews.Translate");
	FDrawList& Out = this->DrawList;
	Out.Reset();
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
		MAHO_LOG_CORE_ERROR("ExampleEditor: vertex/index buffer create failed");
		return;
	}
	R.AddPass(ERHICommandListType::Graphics,
		[&VB, &IB, &Verts, &Idx, VtxBytes, IdxBytes](FRHICommandList& Cmd)
		{
			Cmd.UpdateBuffer(VB.GetRHI(), 0, VtxBytes, Verts.data());
			Cmd.UpdateBuffer(IB.GetRHI(), 0, IdxBytes, Idx.data());
		});
	Out.SetVertexBuffer(VB);
	Out.SetIndexBuffer(IB);

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
	Out.SetPushConstants(ERHIShaderStage::Vertex | ERHIShaderStage::Fragment, static_cast<std::uint32_t>(sizeof(Ortho)), Ortho);

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
			// ImGui collapses a clip rect that lies entirely OUTSIDE the display by pulling
			// its max onto its min (content scrolled below the fold, or a docked window
			// placed off-screen by a stale layout ini). Subtracting a min which is already
			// past the display edge from that collapsed max leaves a NEGATIVE extent, and
			// the uint32 cast turns it into ~4.29e9 -- vkCmdSetScissor then trips
			// VUID-vkCmdSetScissor-offset-00597 (int32 overflow). Clamp BOTH edges first,
			// then take the non-negative difference (0 = empty scissor, draws nothing).
			const ImVec4 Clip = DrawCmd.ClipRect;
			const auto ClampEdge = [](float V, float Max)
			{ return static_cast<std::int32_t>(V < 0.0f ? 0.0f : (V > Max ? Max : V)); };
			Batch.ScissorX = ClampEdge(Clip.x, DisplayW);
			Batch.ScissorY = ClampEdge(Clip.y, DisplayH);
			const std::int32_t ScissorR = ClampEdge(Clip.z, DisplayW);
			const std::int32_t ScissorB = ClampEdge(Clip.w, DisplayH);
			Batch.ScissorW = static_cast<std::uint32_t>(ScissorR > Batch.ScissorX ? ScissorR - Batch.ScissorX : 0);
			Batch.ScissorH = static_cast<std::uint32_t>(ScissorB > Batch.ScissorY ? ScissorB - Batch.ScissorY : 0);
			Batch.bHasScissor = true;
			if (DrawCmd.TextureId != 0)
			{
				// Two resolution paths: a NON-zero ImTextureID is normally a mirror FName id
				// (a registered texture, resolved via GetMirror). But the live present target
				// (game composite UIRenderTarget) has NO mirror entry -- it is CreateTexture'd
				// directly -- so the special present-target id is resolved straight from
				// R.GetPresentTarget() instead. The target is y-down: flip V when displayed.
				const Name::FName TexName = Name::FName::FromId(static_cast<std::uint32_t>(DrawCmd.TextureId));
				if (TexName == Name::FName(kPresentTargetTexName))
				{
					const FRDGTextureRef Present = R.GetPresentTarget();
					if (Present.IsValid())
					{
						FRDGDescriptorSet DescriptorSet;
						DescriptorSet.SetIndex = 0;
						DescriptorSet.Frequency = EDescriptorSetFrequency::Static;
						FRDGBinding Binding;
						Binding.Type = ERHIDescriptorType::CombinedImageSampler;
						Binding.Stages = ERHIShaderStage::Fragment;
						Binding.Resource = Present;
						Binding.SamplerIndex = DescriptorSet.AddSampler(R.CreateSampler(EditorClampSamplerDesc()));
						DescriptorSet.Bindings.push_back({ 0, Binding });
						Batch.Sets.push_back(std::move(DescriptorSet));
					}
				}
				else
				{
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
						Binding.SamplerIndex = DescriptorSet.AddSampler(R.CreateSampler(EditorClampSamplerDesc()));
						DescriptorSet.Bindings.push_back({ 0, Binding });
						Batch.Sets.push_back(std::move(DescriptorSet));
					}
				}
			}
			Out.Add(std::move(Batch));
		}
		VtxBase += List->VtxBuffer.Size;
		IdxBase += List->IdxBuffer.Size;
	}
}

void FExampleEditor::DrawEditorPanels()
{
	MAHO_TRACE_SCOPE("FExampleEditor::DrawEditorPanels");
	// Docking host: a fullscreen dockspace behind the editor windows (host-owned frame
	// shell). Each component plugin draws its own window inside it.
	ImGuiViewport* VP = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(VP->WorkPos);
	ImGui::SetNextWindowSize(VP->WorkSize);
	ImGui::SetNextWindowViewport(VP->ID);
	ImGuiWindowFlags HostFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("EditorDock", nullptr, HostFlags);
	ImGui::PopStyleVar(3);
	EditorDockSpaceId = static_cast<std::uint32_t>(ImGui::GetID("EditorDockSpace"));
	ImGui::DockSpace(EditorDockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
	ImGui::End();

	// 声明式视图：通用循环 —— 宿主只提供上下文/作用域/显示区/停靠 id，开窗与翻译由视图库统一做。
	UI::FUIViewFrameDesc Desc;
	Desc.ImGuiContext = m_Context;
	// 只翻编辑器作用域的视图（与面板登记处同一个名字 —— 见 `EditorRenderScope()`）。
	Desc.RenderScope = EditorRenderScope();
	Desc.DisplayWidth = ImGui::GetIO().DisplaySize.x;
	Desc.DisplayHeight = ImGui::GetIO().DisplaySize.y;
	Desc.DockSpaceId = EditorDockSpaceId;
	UI::TranslateRegisteredViews(Desc);
}

UI::FUIName FExampleEditor::EditorRenderScope()
{
	// 名字只解析一次：名字池的 intern 带锁，不值得每次比较都做。
	static const UI::FUIName Scope("UI.Scope.Editor");
	return Scope;
}

void FExampleEditor::UpdateEditorPanels()
{
	MAHO_TRACE_SCOPE("FExampleEditor::UpdateEditorPanels");
	// 先抽干：上一帧翻译线程入队的交互事件交回各自所有者线程（回调内可再次 Edit()）。
	// 只抽本编辑器作用域的视图：注册表是全进程共享的，游戏侧视图归游戏自己的翻译循环，由它的
	// 所有者（UISystem）在自己的更新期抽干 —— 越作用域抽干会把事件投递到别的所有者线程上。
	const UI::FUIName EditorScope = EditorRenderScope();
	if (UI::FUIViewRegistry* Registry = UI::GetUIViewRegistry())
	{
		for (UI::FUIView* View : Registry->SnapshotViews())
		{
			if (View->GetRenderScope() != EditorScope)
			{
				continue;
			}
			View->DrainEvents();
		}
	}

	for (IEditorPanel* P : Cast<IEditorPanel>())
	{
		// Slot 0, same as FlushPendingUpdates: a panel's Update is the host-driven sibling of
		// its Init/Shutdown batch, which that one-shot path already runs in slot 0.
		P->Update(*this, Slots[0]);
	}
}

void FExampleEditor::RenderEditorUI(FRender& R)
{
	if (!EditorRT.IsValid())
	{
		return;
	}
	const ERHIFormat ColorFormat = EditorRT.GetFormat();
	FDrawList& DrawList = this->DrawList;
	// Always clear + present even with no visible panels, so the swapchain shows the
	// editor's black backplate instead of an undefined (grey) surface.

	FRenderTarget Target;
	FRenderTarget::FAttachment Color;
	Color.View = EditorRT;
	Color.LoadOp = ERHILoadOp::Clear;
	Color.StoreOp = ERHIStoreOp::Store;
	const ImVec4& BgColor = GetEditorBgColor();
	Color.ClearColor[0] = BgColor.x;
	Color.ClearColor[1] = BgColor.y;
	Color.ClearColor[2] = BgColor.z;
	Color.ClearColor[3] = 1.0f;
	Target.AddColor(Color);

	if (!FontTexture.IsValid())
	{
		MAHO_LOG_CORE_ERROR("ExampleEditor: font texture missing");
		return;
	}
	FEditorParameters* Params = R.AllocParameters<FEditorParameters>();
	Params->FontTexture = FontTexture;
	Params->FontSampler = R.CreateSampler(EditorClampSamplerDesc());

	TShaderHandle<FEditorShader> Shader = R.TryGetShader<FEditorShader>();
	FRHIShaderModule* VS = nullptr;
	FRHIShaderModule* FS = nullptr;
	if (Shader.Wait())
	{
		VS = Shader.GetVertex();
		FS = Shader.GetFragment();
	}
	if (VS == nullptr || FS == nullptr)
	{
		MAHO_LOG_CORE_ERROR("ExampleEditor: shader not ready");
		return;
	}

	FRHIGraphicsPipelineDesc PipelineDesc;
	PipelineDesc.VertexShader = VS;
	PipelineDesc.FragmentShader = FS;
	PipelineDesc.VertexShaderHash = Shader.GetVertexHash();
	PipelineDesc.FragmentShaderHash = Shader.GetFragmentHash();
	PipelineDesc.VertexEntryPoint = FEditorShader::GetVertexEntryPoint();
	PipelineDesc.FragmentEntryPoint = FEditorShader::GetFragmentEntryPoint();
	PipelineDesc.RenderPass = nullptr;   // dynamic rendering
	PipelineDesc.Topology = ERHIPrimitiveTopology::TriangleList;
	PipelineDesc.VertexStride = sizeof(ImDrawVert);
	PipelineDesc.Attributes = {
		{ 0, ERHIFormat::R32G32_SFLOAT,  offsetof(ImDrawVert, pos) },
		{ 1, ERHIFormat::R32G32_SFLOAT,  offsetof(ImDrawVert, uv) },
		{ 2, ERHIFormat::R8G8B8A8_UNORM, offsetof(ImDrawVert, col) },
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

	// The editor draws only its own ImGui content into EditorRT, and its viewport window
	// samples the live present target (the game composite UIRenderTarget, flipped to
	// SHADER_READ_ONLY by pass2 for exactly this sampled read). Sample it here. No layout
	// re-flip is needed: this frame presents EditorRT (below), so UIRenderTarget stays in
	// SHADER_READ_ONLY until the game UI's next RenderUI head flips it back to
	// COLOR_ATTACHMENT before it writes again -- the RHI never auto-transitions, so the
	// editor must NOT try to reuse it as a render target itself.
	R.AddPass(ERHICommandListType::Graphics, PipelineDesc, Target, Params, DrawList);

	// Done with the list's per-frame GPU refs (the merged VB/IB) the moment the draw pass above has
	// recorded. Dropping them HERE rather than at the next frame's rebuild keeps a transient slot from
	// being held across the frame boundary: a held slot is not recyclable (the pool must not hand it
	// to another request while a ref points at it), so the next frame would allocate fresh buffers
	// every frame -- a vkCreateBuffer + vkAllocateMemory per frame, i.e. visible drag jank.
	DrawList.Reset();

	// The editor owns the final on-screen surface in an editor build: set its EditorRT
	// as the present target (last writer wins over the game UI, which may have set the
	// game composite). The frame feature's IPresent blits it to the swapchain.
	R.SetPresentTarget(EditorRT);
}

void FExampleEditor::EditorCompose(FRender& R, FRenderContext& Frame)
{
	// Pass3 -- the editor's whole frame in a single graph stage (after the game-UI
	// composite IRenderUI, before the frame's IPresent). Split into two private steps:
	// InitViews builds the ImGui frame (feed + NewFrame + panels + Render + translate),
	// RenderUI composes it into EditorRT and takes over the present target. Panels /
	// component draws (DrawEditorPanels) are the content you fill.
	InitEditorViews(R);
	RenderEditorUI(R);

	// A drop batch is one frame's worth of news: the panels have had their chance.
	DroppedFiles.clear();
}

bool FExampleEditor::ConsumeDroppedFiles(std::vector<std::string>& Out)
{
	if (DroppedFiles.empty())
	{
		return false;
	}
	Out.insert(Out.end(), DroppedFiles.begin(), DroppedFiles.end());
	DroppedFiles.clear();
	return true;
}

void FExampleEditor::PreUnInstall(FRender& R, FRenderContext& Frame)
{
	// Hand back the two capability tokens this module registered into the UI layer. The slots
	// live on FUIViewRegistry -- a frame, not a file-scope static -- so their destruction is
	// driven by the graph; but the FUNCTIONS they hold are lambdas whose code lives in THIS
	// module. Left registered, those would be torn down after this DLL is unloaded. Symmetric
	// with the Bind calls in InitUI; and if this were forgotten, the registry's own IShutdown
	// now clears the slot AND names the owner in the log instead of crashing at process exit.
	UI::UnbindUIResourceResolver(ResolverToken);
	UI::UnbindUIClipboardHandlers(ClipboardToken);
	ResolverToken = 0;
	ClipboardToken = 0;

	// Uninstall the editor components first -- their Shutdown graph runs (and the layer
	// instances + DLLs are freed) before the host tears down the surface/font/context it
	// owns. The editor as a whole is a self-consistent module: it drains its components.
	ShutdownEditorComponents();

	if (EditorRT.IsValid())
	{
		R.ReleaseTexture(EditorRT);
		EditorRT.Reset();
	}
	if (FontTexture.IsValid())
	{
		R.ReleaseTexture(FontTexture);
		FontTexture.Reset();
	}
	// Destroy the ImGui context (no GLFW backend to shut down -- input is hand-fed).
	if (m_Context != nullptr)
	{
		// Drop THIS context's baked font entries before its atlas dies (the game context
		// keeps its own, keyed separately).
		UI::ClearUIFonts(static_cast<void*>(m_Context));
		ImGui::SetCurrentContext(nullptr);
		ImGui::DestroyContext(m_Context);
		m_Context = nullptr;
	}
	bUIInit = false;
	bFontUploaded = false;
	bVpValid = false;
}

void FExampleEditor::ShutdownEditorComponents()
{
	MAHO_TRACE_SCOPE("FExampleEditor::ShutdownEditorComponents");
	// Uninstall by module base name + suffix -- symmetric with Install(...). The
	// layer's GetName() is "FEditorConsole", but TryUninstall resolves either form,
	// so this guarantees the component's IEditorShutdown (EditorConsole unbinding
	// its OnLog subscription) runs BEFORE FLog::Shutdown -- no name/module
	// asymmetry to trip on. The same holds for the UI view registry: every panel
	// unregisters its view in IEditorShutdown here, and FRender (the layer that drives
	// THIS teardown) declares the registry's IShutdown behind its own -- a panel is a
	// sub-plugin, so an edge it declared itself would never bind.
	TryUninstall(Maho::ApplyModuleExtension("FEditorConsole"));
	TryUninstall(Maho::ApplyModuleExtension("FContentBrowser"));
	TryUninstall(Maho::ApplyModuleExtension("FEditorViewport"));
	TryUninstall(Maho::ApplyModuleExtension("FEditorTheme"));
	FlushPendingUpdates<TTypeList<IEditorInit>, TTypeList<IEditorShutdown>>();
}

} // namespace Maho

// C export -- the host (FRender collector) loads this DLL and calls CreateFrame() by symbol name.
extern "C" MAHO_EXAMPLEEDITOR_API Maho::FFrameExtension* CreateFrame()
{
	return Maho::FExampleEditor::CreateFrame();
}
