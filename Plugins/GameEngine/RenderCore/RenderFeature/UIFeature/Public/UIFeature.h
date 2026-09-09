#pragma once

#include "UIFeatureApi.h"
#include <Engine/Layer.h>
#include <Render.h>
#include <RDG.h>
#include <RHI/RHIResources.h>
#include <RenderDrawList.h>

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

#include <Platform.h>

// ImGui declares `struct ImGuiContext` in the global namespace; the header only
// ever mentions it as a pointer member, so a forward declaration is enough and
// keeps Render.cpp (and any consumer) from needing imgui.h just to include this.
struct ImGuiContext;

namespace Maho
{

/**
 * Shader type consumed by FRender::TryGetShader<T>. Pure STATIC source accessors --
 * the compile + bytecode cache + content hashes live in the shared TShaderHandle
 * state, so a feature never owns the bytecode or a native module. The static bodies
 * are defined in the .cpp (the GLSL strings stay private to the feature). Exactly
 * the same contract as FTriangleShader -- the UI shader path is the generic one.
 */
struct FUIShader
{
	static const char* GetVertexSource();
	static const char* GetFragmentSource();
	static const char* GetVertexEntryPoint();
	static const char* GetFragmentEntryPoint();
};

/**
 * ImGui render feature - the OWNER of the UI's CPU-side ImGui context and the
 * whole frame lifecycle. This feature creates/destroys the ImGui context
 * (OnInstalled / PreUnInstall) and drives the frame inside InitViews: frame feed ->
 * NewFrame -> pull the game-side UI commands from the UISystem's UIBuilder and run
 * them (FUIBuilder::Execute -- the game submits draw closures defining the UI; this
 * worker executes every ImGui call) -> Render -> GetDrawData -> translate the draw
 * data into GPU buffers (uploaded in InitViews) + an FDrawList holding the refs.
 * RenderUI draws that list into this feature's OWN off-screen composite target
 * (UIRenderTarget, sized to the swapchain canvas + format) and sets it as FRender's present
 * target -- the frame feature's IPresent blits it to the swapchain. The UI is the final
 * on-screen surface; the scene is sampled INto it via the game's imgui::image SceneColor
 * control. This feature is OFF-SCREEN ONLY: it no longer owns the present point (the frame
 * feature does). FRender is completely UI-agnostic -- it holds no ImGui state, never links
 * or references ImGui.
 *
 * Stateless draw feature: the UI shader goes through FRender::TryGetShader<FUIShader>
 * (async compile + per-type cache, above). The FONT backend holds ONLY the RDG
 * font texture handle. NO raw RHI pointer lives here: the descriptor set layout, the
 * descriptor set and the sampler are each re-resolved from the resource pool
 * (content-addressable get-or-create, keyed by the PassParameter binding value that
 * produced them) on demand. The pool owns every native lifetime, so this feature
 * neither owns nor tears down a resource. Only the translated FDrawList is held
 * (a member, reused across frames).
 */
class MAHO_UIFEATURE_API FUIFeature : public FLayer<IOnInstalled, IInitViews, IRenderUI, IPreUnInstall>
{
MAHO_DECLARE_LAYER(FUIFeature);

	FUIFeature();

public:
	void OnInstalled(FRender& R) override;
	void InitViews(FRender& R) override;
	void RenderUI(FRender& R) override;
	void PreUnInstall(FRender& R) override;

	/** Pass2 composite target (game view). Read by the editor compose (pass3) to embed
	 *  it as the viewport background via imgui::image. Null until InitViews creates it
	 *  on the first frame with a valid canvas. */
	[[nodiscard]] FRDGTextureRef GetUIRenderTarget() const { return UIRenderTarget; }
	/** Editor-build handoff: flip UIRenderTarget from COLOR_ATTACHMENT (where the UI just
	 *  wrote it) to SHADER_READ_ONLY so pass3 can imgui::image it as the viewport present
	 *  target (the RHI never auto-transitions and descriptor writes hardcode SHADER_READ_ONLY).
	 *  Called at the END of RenderUI when the present target is sampled downstream. No-op if
	 *  it is not currently COLOR_ATTACHMENT. */
	void TransitionUIRenderTargetForSampling(FRender& R);

	/** This feature's OWN ImGui context. In an editor build the editor feature (pass0
	 *  IEditorInput) needs the GAME context's IO to feed re-based input into, so it fetches
	 *  this via GetUI() + GetImGuiContext(). May be null before OnInstalled / after
	 *  PreUnInstall. */
	[[nodiscard]] ImGuiContext* GetImGuiContext() const { return m_Context; }

	/** Editor-build input takeover. Called by the editor's pass0 IEditorInput stage, BEFORE
	 *  this feature's InitViews: feeds the cursor (already re-based by the editor into THIS
	 *  context's whole-window DisplaySize coordinates -- the panel-local coords are mapped back
	 *  through the panel->window scale) plus the mouse buttons into THIS context's IO, along with
	 *  the FULL keyboard+character+wheel input the editor already harvested from the platform
	 *  (the editor is the ONE drain/consume consumer this frame -- see FExampleEditor). The
	 *  DisplaySize stays whole-window (game layout never re-scales to the panel). InitViews then
	 *  skips its own OS poll (bEditorInputThisFrame), so the game UI only responds inside the
	 *  viewport panel but still receives keys/chars/wheel. Thread-safe (serialized with InitViews
	 *  behind ImGuiFrameMutex). No-op when the context is not created. */
	void SetEditorInput(
		float X, float Y, bool B0, bool B1, bool B2,
		const Platform::MInputContext& Snap,
		const std::vector<Platform::MInputEvent>& Events,
		float WheelX, float WheelY);

private:
	/** Lazily create the font backend (font texture + staging). Returns whether it is
	 *  ready. Idempotent. */
	bool EnsureUIBackend(FRender& R);
	/** One-time font-atlas upload (a transfer submit, illegal inside a render pass).
	 *  No-op after the first call. */
	void UploadFont(FRender& R);

	/** Subscribe to the UISystem's "UI built" event (thread-safe, idempotent). The
	 *  handler runs on the GAME broadcast thread AFTER Submit finished, so it copies
	 *  the UIBuilder batch into m_UICommands -- a COMPLETE frame snapshot, never a
	 *  partial/empty one. Called lazily from InitViews: the world system installs
	 *  after this render feature, so the subscription is registered on first frame. */
	void TrySubscribeUI();

	// ImGui texture-id semantics: ImTextureID == 0 selects the PASS-LEVEL font set
	// (FontTexture + a pooled clamp sampler, bound via FUIParameters). A NON-zero
	// ImTextureID holds a mirror FName id (FName::GetId()) -- the per-batch set is
	// resolved by FName::FromId(id) -> FRender::GetMirror -> FRDGTextureRef (a pooled
	// clamp sampler is re-resolved by desc). No descriptor set or sampler is ever held
	// here; the pool owns every native lifetime.
	//
	// THREAD SAFETY: ImGui's GImGui is a process-wide NON-thread-safe state machine
	// (CurrentWindow stack, FrameCount/FrameCountEnded, DrawData...). InitViews runs on
	// an arbitrary render-pool worker, and different frames may land on different
	// workers -- so the WHOLE frame (feed -> NewFrame -> build -> Render -> GetDrawData
	// -> translate) is serialized behind ImGuiFrameMutex. One thread at a time owns
	// "current frame", which satisfies ImGui's single-owner contract without forcing a
	// dedicated thread (other render features stay parallel).
	std::mutex ImGuiFrameMutex;
	bool bUIInit = false;
	bool bFontUploaded = false;
	/** Whether ImGui::CreateContext() has run (this feature owns the CPU-side ImGui
	 *  context; created at OnInstalled, destroyed at PreUnInstall). Guards every
	 *  frame-feed / InitViews entry. */
	bool bContextCreated = false;
	/** This feature's OWN ImGui context. Two UI features (game + editor) share the
	 *  process-wide imgui DLL but each owns a distinct context; InitViews selects this
	 *  one (SetCurrentContext) so a frame is never built against the other context. */
	ImGuiContext* m_Context = nullptr;
	/** Whether this feature has subscribed to the UISystem's UI-built event. */
	bool bSubscribedUI = false;
	/** Editor-build input takeover flag. Set by SetEditorInput (the editor's pass0 stage);
	 *  InitViews reads it to decide whether to skip its own OS poll. Reset to false after
	 *  InitViews consumes it each frame. */
	bool bEditorInputThisFrame = false;
	/** The UI-built subscription id (0 = not subscribed). Retained so PreUnInstall
	 *  unsubscribes ONLY this feature's handler -- not any other subscriber's. */
	uint64_t m_UISubscription = 0;
	/** Render-side snapshot of the game's UI closures. REPLACED (never cleared) by
	 *  the UI-built event handler on the game thread, so InitViews always has a
	 *  complete frame to run -- no empty/partial batch, no flicker. Guarded by
	 *  m_UISnapshotMutex (game handler writes it, render InitViews reads + runs a copy). */
	std::mutex m_UISnapshotMutex;
	std::vector<std::function<void()>> m_UICommands;
	/** The pass-level font texture (pool-owned persistent). Bound via FUIParameters
	 *  every RenderUI; the sampler is a pooled clamp sampler (content-addressable). */
	FRDGTextureRef FontTexture;
	/** This feature's own final on-screen composite target. Sized to the swapchain canvas
	 *  + format (rebuilt on resize) and created from the canvas geometry, so the IPresent
	 *  blit to the backbuffer is format/geometry-consistent. RenderUI draws the ImGui list
	 *  (incl. the game's imgui::image SceneColor sample) into it (LoadOp Clear -- this
	 *  surface is fully redrawn each frame); Present() blits it to the swapchain. Pool-owned
	 *  persistent, released at PreUnInstall. */
	FRDGTextureRef UIRenderTarget;
	/** Layout tracker for UIRenderTarget. It doubles as both a dynamic-rendering color
	 *  attachment (RenderUI draws it) AND, in an editor build, a sampled mirror (pass3's
	 *  viewport imgui::image of the present target). The two uses need opposite layouts and
	 *  the RHI never auto-transitions, so the layout is flipped each frame: RenderUI leaves
	 *  it in COLOR_ATTACHMENT, then TransitionUIRenderTargetForSampling() flips it to
	 *  SHADER_READ_ONLY so pass3 can sample it; RenderUI's head flips it back before any
	 *  write. A fresh target (never transitioned) is Common and is brought up once. */
	bool bUIRenderTargetLayoutSR = false;
	/** The translated ImDrawData->FDrawList for the CURRENT frame. Filled at InitViews
	 *  (the whole ImGui frame lifecycle lives there; GPU buffers are uploaded here),
	 *  drawn at RenderUI (same graph, self-progression). A member so the merged
	 *  primitive + batch vectors reuse their capacity across frames. */
	FDrawList DrawList;
};

/** Global accessor to the UI render feature (cross-DLL, mirrors Resource::GetResourceSystem()
 *  and Log::GetLog()). nullptr until the feature is installed by FRender; set at
 *  OnInstalled, cleared at PreUnInstall. The editor feature uses this (pass0) to reach the
 *  game-UI context and feed re-based input into it. */
MAHO_UIFEATURE_API FUIFeature* GetUI();

} // namespace Maho
