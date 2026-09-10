#pragma once

#include "ExampleEditorApi.h"
#include <Engine/Layer.h>
#include <Engine/LayerCollector.h>
#include <Render.h>
#include <RDG.h>
#include <RenderDrawList.h>
#include <UITypes.h>

#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include <Platform.h>

// The editor owns its own ImGui context; the header only holds a pointer member,
// so a global forward declaration keeps the header compilable before imgui.h is
// included by the .cpp.
struct ImGuiContext;

namespace Maho
{

class FExampleEditor;

/**
 * Lightweight shared editor state, owned by the host and read/written by the
 * component plugins through FExampleEditor::GetEditorContext(). The host keeps
 * the panel draw order fixed (Viewport -> Outliner -> Inspector -> Controls), so
 * a component can publish a value (e.g. the selected entity) that a later one
 * consumes -- no task graph, no cross-component dependency edge needed.
 */
struct FEditorContext
{
	std::uint32_t SelectedEntityId = 0;   // written by Outliner, read by Inspector
	bool     bSceneReady = false;         // scene feature installed (viewport samples the mirror)
};

/**
 * Editor component stage interfaces. A component plugin derives from any subset
 * of these (via FLayer<...>). Init/Shutdown are driven by the host's own
 * FLayerCollector install/uninstall graph; Update is driven by the host's frame
 * loop BEFORE `ImGui::NewFrame()` (Select<IEditorPanel>() -> for over Update), and
 * the host then translates the registered views. The context is the host
 * FExampleEditor&, so a component reads shared UI state and reaches FRender
 * through it -- it never owns ImGui/RHI resources.
 */
class MAHO_EXAMPLEEDITOR_API IEditorInit
{
public:
	virtual ~IEditorInit() = default;
	virtual void Init(FExampleEditor&) = 0;
};

/**
 * Panel stage. A panel owns a persistent UI tree (`UI::FUIView`) and rebuilds it in
 * `Update`, which the host calls BEFORE `ImGui::NewFrame()` (declaration phase). The
 * host then translates every registered view inside the frame -- a panel never calls
 * ImGui. This is the only panel door: the legacy direct-draw door is gone.
 */
class MAHO_EXAMPLEEDITOR_API IEditorPanel
{
public:
	virtual ~IEditorPanel() = default;
	/** 声明期（宿主 `NewFrame` 之前）：只改自己的 UI 树，不碰后端。 */
	virtual void Update(FExampleEditor&) = 0;
};

class MAHO_EXAMPLEEDITOR_API IEditorShutdown
{
public:
	virtual ~IEditorShutdown() = default;
	virtual void Shutdown(FExampleEditor&) = 0;
};

// Stage dispatch specializations so the host's FLayerCollector install/uninstall
// graph can drive component Init/Shutdown (Invoke<IEditorInit, FExampleEditor>).
MAHO_DECLARE_STAGE_DISPATCH(FExampleEditor, IEditorInit, IEditorInit, Init)
MAHO_DECLARE_STAGE_DISPATCH(FExampleEditor, IEditorShutdown, IEditorShutdown, Shutdown)

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
 * Editor UI host -- BOTH a render feature of FRender AND its own sub-collector.
 *
 * As a render feature it mounts FRender's stages (IOnInstalled / IInitViews /
 * IRenderUI / IPreUnInstall) and drives the whole editor frame: it owns the
 * editor's OWN ImGui context, the EditorRT composite target, the font upload and
 * the final present. It is the ONLY place the editor touches RHI.
 *
	 * As a sub-collector it derives FLayerCollector<FExampleEditor> and installs the
	 * editor COMPONENT plugins (EditorViewport / EditorConsole / ContentBrowser /
	 * EditorTheme) as DLLs.
	 * Each component is an anonymous FLayer mounting the IEditor* stage interfaces.
	 * The host installs them at OnInstalled (next safe point runs their Init graph),
	 * rebuilds their UI trees every frame via Select<IEditorPanel>() -> for over Update
	 * (before `NewFrame`), translates every registered view in-frame, and uninstalls
	 * them at PreUnInstall (their Shutdown graph first). Components only declare their
	 * own tree -- they carry no ImGui/RHI resource ownership.
	 *
	 * The frame shell (DockSpace) stays in the host, and so does the docking identity:
	 * the host applies `SetNextWindowDockID(its own dockspace id)` before it opens any
	 * shelled view, so a component never needs to know a dock id. The plugin is Type=Editor:
	 * a Runtime build drops it (and its components) at codegen, so editor
	 * DLLs/headers/ImGui compile only in
	 * an Editor build. It is installed into FRender by module base name
	 * (Install(ApplyModuleExtension("FExampleEditor"))) from the host when the build
	 * is an editor build (MAHO_EDITOR_BUILD).
	 */
class MAHO_EXAMPLEEDITOR_API FExampleEditor
	: public FLayer<IOnInstalled, IEditorInput, IEditorCompose, IPreUnInstall>
	, public FLayerCollector<FExampleEditor>
{
	MAHO_DECLARE_LAYER(FExampleEditor);

	FExampleEditor();

public:
	void OnInstalled(FRender&) override;
	/** Pass0: editor input takeover. Runs FIRST, before the game-UI feature's IInitViews
	 *  feeds + NewFrame()s its IO. Tastes the Win32 cursor, re-bases it to the viewport
	 *  panel rect (clamped panel-local) and feeds the game context via FUIFeature::GetUI()
	 *  -> SetEditorInput, so the game UI only responds inside the panel and its layout
	 *  matches the displayed panel. Blocked before FUIFeature's IRenderUI (ctor). */
	void EditorInput(FRender&) override;
	/** Pass3: run the editor's OWN InitViews + Render in a single IEditorCompose stage
	 *  (after the game-UI composite IRenderUI, before the frame's IPresent blit). It takes
	 *  over the frame's present target with its EditorRT. */
	void EditorCompose(FRender&) override;
	void PreUnInstall(FRender&) override;

	/** Called by a viewport component (Update) to publish the on-screen panel rect (client
	 *  pixels) the game UI is presented into. EditorInput re-bases the game cursor to it. */
	void ReportViewportRect(float X, float Y, float W, float H) { VpX = X; VpY = Y; VpW = W; VpH = H; bVpValid = W > 0.f && H > 0.f; }

	/** File paths dropped onto the window THIS frame (physical absolute paths from the OS
	 *  file manager). A batch lives for exactly one frame: EditorInput fills it, a panel
	 *  reads it while drawing, EditorCompose clears the remainder. Consumers decide
	 *  themselves whether the drop belongs to them (e.g. "is my window hovered"). */
	[[nodiscard]] std::span<const std::string> GetDroppedFiles() const { return DroppedFiles; }

	/** Consume this frame's dropped files. First consumer wins -- a later panel sees an
	 *  empty batch, so two drop targets can never import the same file. Returns false
	 *  when nothing was pending. */
	bool ConsumeDroppedFiles(std::vector<std::string>& Out);

	/** Shared component state (selected entity, scene-ready flag). */
	FEditorContext& GetEditorContext() { return EditorContext; }
	/** The FRender the host frame is driven from (set each InitViews). */
	FRender& GetRender() { return *RenderRef; }
	/** The host's main docking-space node id (owner of the frame shell). A component
	 *  calls DockBuilder/SetNextWindowDockID against it to land inside the shared space. */
	std::uint32_t GetEditorDockSpaceId() const { return EditorDockSpaceId; }

	/** 编辑器 ImGui 上下文的 opaque 句柄。面板建视图时用它登记
	 *  `FUIView::SetRenderContext` —— 通用翻译循环按上下文筛选视图，两套上下文互不串扰。 */
	void* GetUIRenderContext() const { return m_Context; }
	/** ImTextureID for the live present target (the game composite UIRenderTarget in an
	 *  editor build, resolved to EditorRT only at the very end of the frame). The host's
	 *  translate step resolves THIS id to R.GetPresentTarget() instead of a name-keyed
	 *  mirror (the target has no mirror/name entry), so a component can imgui::image the
	 *  current on-screen surface with a single stable id. */
	static std::uint32_t PresentTargetTextureId();

	/** The present target as a UI-tree resource REFERENCE (`FUIName`). A declarative panel
	 *  declares `FUIImage` with this name; the resolver the host injects maps the name back
	 *  to `PresentTargetTextureId()`, so the panel never sees an ImTextureID. */
	static UI::FUIName PresentTargetName();

private:
	/** Install the editor component DLLs + run their Init graph (safe point). */
	void InstallEditorComponents();
	/** Uninstall the 4 editor components (run their Shutdown graph) before teardown. */
	void ShutdownEditorComponents();
	/** 声明期：抽干各视图上一帧入队的交互事件，再让每个组件 `Update` 自己的 UI 树。
	 *  在 `ImGui::NewFrame()` 之前调用 —— 后端此刻还没有帧，组件也就不可能碰 ImGui。 */
	void UpdateEditorPanels();
	/** Host-owned frame shell: own context, fullscreen DockSpace, then the generic
	 *  registered-view loop (host supplies context/display rect/dock id; the UI plugin
	 *  opens each window and translates its tree -- the host knows no concrete view). */
	void DrawEditorPanels();
	/** Formerly the IInitViews stage -- now the frame-build half of EditorCompose:
	 *  feed + NewFrame + panels + Render + translate to an FDrawList. Runs first. */
	void InitEditorViews(FRender& R);
	/** Formerly the IRenderUI stage -- now the compose half of EditorCompose: draws the
	 *  editor ImGui list into EditorRT and takes over the present target. Runs second. */
	void RenderEditorUI(FRender& R);
	/** Lazy font-backend init (editor-owned font texture + staging). Idempotent. */
	bool EnsureUIBackend(FRender& R);
	/** One-time editor font-atlas upload (transfer submit, illegal in a render pass). */
	void UploadFont(FRender& R);

	// This feature owns its OWN ImGui context (fully independent of the game's). It is
	// switched in at InitViews (SetCurrentContext) so the game feature's frame is never
	// mixed with this one; DestroyContext at PreUnInstall.
	ImGuiContext* m_Context = nullptr;
	bool bUIInit = false;
	bool bFontUploaded = false;

	// Single-frame input cache. EditorInput (pass0, runs FIRST this frame) is the ONE
	// drain + wheel-consume consumer of the frame. It stores the drained event batch and
	// the exchanged-to-zero wheel delta here so InitEditorViews (pass3, later in the SAME
	// frame) can feed the editor context WITHOUT re-draining the platform -- a second
	// drain/consume would return nothing, since these are single-consumer resources.
	std::vector<Platform::MInputEvent> EditorInputEvents;
	float EditorWheelX = 0.f;
	float EditorWheelY = 0.f;
	/** Whether EditorInput drained the platform THIS frame (the cache above is fresh). Set
	 *  true when EditorInput gets past its guards and drains; InitEditorViews consumes the
	 *  cache when true and otherwise leaves the stream alone (the game-UI context's own
	 *  whole-window fallback is THE drainer that frame, and a second drain would be empty). */
	bool bEditorInputCached = false;

	/** OS drop batch for the current frame (filled by EditorInput, cleared at the end of
	 *  EditorCompose). Same single-frame contract as the input cache above. */
	std::vector<std::string> DroppedFiles;

	std::mutex ImGuiFrameMutex;   // one thread at a time owns the editor's Id: NewFrame

	/** The editor's off-screen composite target (EditorRT). Sized to the swapchain
	 *  canvas + format; RenderUI draws the editor ImGui list (incl. the SceneColor
	 *  viewport image) into it and it becomes the present target. */
	FRDGTextureRef EditorRT;
	FRDGTextureRef FontTexture;   // editor-own font texture (pool-owned persistent)
	/** The translated ImDrawData -> FDrawList for the current frame. */
	FDrawList DrawList;

	FRender* RenderRef = nullptr;        // current frame render (for component use)
	FEditorContext EditorContext;        // shared component state
	std::uint32_t EditorDockSpaceId = 0; // host DockSpace node id, set each DrawEditorPanels
	/** Viewport panel rect (client pixels) the game UI is displayed into, published by the
	 *  viewport component via ReportViewportRect. EditorInput re-bases the game cursor to it.
	 *  bVpValid = false until the panel has been drawn at least once (valid positive size). */
	float VpX = 0.f, VpY = 0.f, VpW = 0.f, VpH = 0.f;
	bool bVpValid = false;
};

} // namespace Maho
