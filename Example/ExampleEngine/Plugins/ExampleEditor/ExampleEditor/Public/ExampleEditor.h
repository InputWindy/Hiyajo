#pragma once

#include "ExampleEditorApi.h"
#include <Engine/Layer.h>
#include <Render.h>
#include <RDG.h>
#include <RenderDrawList.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// The editor owns its own ImGui context; the header only holds a pointer member,
// so a global forward declaration keeps the header compilable before imgui.h is
// included by the .cpp.
struct ImGuiContext;

namespace Maho
{

/**
 * The editor's draw shader type (mirrors FUIShader). The GLSL sources are private
 * to the .cpp and exposed to FRender's TryGetShader<FEditorShader>() through these
 * four accessors -- exactly the contract FRender::TryGetShader uses for any feature
 * shader. Stateless: no per-frame state, the ortho push data comes from the FDrawList.
 */
struct FEditorShader
{
	static const char* GetVertexSource();
	static const char* GetFragmentSource();
	static const char* GetVertexEntryPoint();
	static const char* GetFragmentEntryPoint();
};

/**
 * Editor UI render feature -- an INDEPENDENT UI subsystem, fully isolated from the
 * game's UIFeature. It owns its OWN ImGui context (ImGui::CreateContext() at install
 * / DestroyContext at teardown, switched via SetCurrentContext each frame) and its OWN
 * off-screen composite target (EditorRT, sized to the swapchain canvas + format). It
 * takes over the whole data-driven control set (FUIControl / EUIControlType and the
 * DrawControl dispatcher, moved here out of the game's UISystem) and draws a dock /
 * scene viewport (sampling the SceneColor mirror as the viewport background) /
 * outliner / inspector shell into EditorRT, then sets EditorRT as FRender's present
 * target -- the frame feature's IPresent blits it to the swapchain (present decoupled,
 * runtime presents the game UI, editor build presents the editor surface).
 *
 * This plugin is marked Type=Editor: a Runtime build (the default) filters it out at
 * codegen, so the editor DLL, headers and every ImGui/editor dependency compile only
 * in an Editor build. It is installed INTO FRender's feature collector by name
 * (Install("ExampleEditor.dll")) from the host when the build is an editor build.
 *
 * FRender is completely UI-agnostic: it holds no ImGui state. Two UI features (game +
 * editor) each own a context and a target, and the last feature to set its target wins
 * the present. The game and editor contexts are never mixed -- each frame the feature
 * switches to its own context, runs its own NewFrame->Render, and translates its own
 * draw data.
 */
class MAHO_EXAMPLEEDITOR_API FExampleEditor : public FLayer<IOnInstalled, IInitViews, IRenderUI, IPreUnInstall>
{
MAHO_DECLARE_LAYER(FExampleEditor, "ExampleEditor.dll");

	FExampleEditor();

public:
	void OnInstalled(FRender& R) override;
	void InitViews(FRender& R) override;
	void RenderUI(FRender& R) override;
	void PreUnInstall(FRender& R) override;

private:
	/** Lazy font-backend init (editor-owned font texture + staging). Idempotent. */
	bool EnsureUIBackend(FRender& R);
	/** One-time editor font-atlas upload (transfer submit, illegal in a render pass). */
	void UploadFont(FRender& R);
	/** Build this frame's editor UI (dock + viewport + outliner + inspector + the
	 *  relocated FUIControl / DrawControl set) directly through this feature's ImGui
	 *  context. The editor owns what it draws -- no external UIBuilder. */
	void BuildEditorUI(FRender& R, bool bSceneReady);

	// This feature owns its OWN ImGui context (fully independent of the game's). It is
	// switched in at InitViews (SetCurrentContext) so the game feature's frame is never
	// mixed with this one; DestroyContext at PreUnInstall.
	ImGuiContext* m_Context = nullptr;
	bool bUIInit = false;
	bool bFontUploaded = false;

	std::mutex ImGuiFrameMutex;   // one thread at a time owns the editor's Id: NewFrame

	/** The editor's off-screen composite target (EditorRT). Sized to the swapchain
	 *  canvas + format; RenderUI draws the editor ImGui list (incl. the SceneColor
	 *  viewport image) into it and it becomes the present target. */
	FRDGTextureRef EditorRT;
	FRDGTextureRef FontTexture;   // editor-own font texture (pool-owned persistent)
	/** The translated ImDrawData -> FDrawList for the current frame. */
	FDrawList DrawList;
};

} // namespace Maho
