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
MAHO_DECLARE_LAYER(FUIFeature, "UIFeature.dll");

	FUIFeature();

public:
	void OnInstalled(FRender& R) override;
	void InitViews(FRender& R) override;
	void RenderUI(FRender& R) override;
	void PreUnInstall(FRender& R) override;

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
	/** The translated ImDrawData->FDrawList for the CURRENT frame. Filled at InitViews
	 *  (the whole ImGui frame lifecycle lives there; GPU buffers are uploaded here),
	 *  drawn at RenderUI (same graph, self-progression). A member so the merged
	 *  primitive + batch vectors reuse their capacity across frames. */
	FDrawList DrawList;
};

} // namespace Maho
