#pragma once

#include "ExampleEditorApi.h"
#include <Engine/Frame.h>
#include <Engine/FrameBuilder.h>
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

/** Per-frame state for the EDITOR's own stages (one instance per ring slot; reach it as
 *  `FExampleEditor::FContext`).
 *
 *  DEFINED HERE, at namespace scope, and not inside the class: the stage interfaces below must
 *  name it in their signatures, and they are declared before `FExampleEditor` exists.
 *  `FExampleEditor` carries a nested alias so stages and the dispatch macro can still write the
 *  nested name. */
struct FExampleEditorContext
{
};

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
 * of these (via FFrameExtension). Init/Shutdown are driven by the host's own
 * FFrameBuilder install/uninstall graph; Update is driven by the host's frame
 * loop BEFORE `ImGui::NewFrame()` (Select<IEditorPanel>() -> for over Update), and
 * the host then translates the registered views. The context is the host
 * FExampleEditor&, so a component reads shared UI state and reaches FRender
 * through it -- it never owns ImGui/RHI resources.
 */
class MAHO_EXAMPLEEDITOR_API IEditorInit
{
public:
	virtual ~IEditorInit() = default;
	virtual void Init(FExampleEditor&, FExampleEditorContext&) = 0;
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
	virtual void Update(FExampleEditor&, FExampleEditorContext&) = 0;
};

class MAHO_EXAMPLEEDITOR_API IEditorShutdown
{
public:
	virtual ~IEditorShutdown() = default;
	virtual void Shutdown(FExampleEditor&, FExampleEditorContext&) = 0;
};

// Stage dispatch specializations so the host's FFrameBuilder install/uninstall
// graph can drive component Init/Shutdown (Invoke<IEditorInit, FExampleEditor>).
// The per-stage dispatch specializations live AT THE BOTTOM of this header, after `class FExampleEditor`:
// their body names `FExampleEditor::FContext`, which is only declared inside the class.

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
	 * As a sub-collector it derives FFrameBuilder<FExampleEditor> and installs the
	 * editor COMPONENT plugins (EditorViewport / EditorConsole / ContentBrowser /
	 * EditorTheme) as DLLs.
	 * Each component is an anonymous frame extension mounting the IEditor* stage interfaces.
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
	: public FFrameExtension, public IPipeline<IOnInstalled, IEditorInput, IEditorCompose, IPreUnInstall>
	, public FFrameBuilder<FExampleEditor>
{
public:
	/** Per-frame state for the editor's stages -- one instance per ring slot. (Same shape as
	 *  FEngineBase::FContext; the reasoning lives there.) EMPTY FOR NOW.
	 *
	 *  An ALIAS, not the definition: the stage interfaces must name this type before FExampleEditor
	 *  exists, so the definition lives at namespace scope (see FExampleEditorContext). Stages and
	 *  the dispatch macro write the nested name; it is the same type either way. */
	using FContext = FExampleEditorContext;

protected:
	std::array<FContext, MAHO_FRAMES_IN_FLIGHT> Slots;

	void* GetContext(int Slot) override
	{
		return &Slots[Slot];
	}

private:
	MAHO_DECLARE_FRAME_UNDER(FExampleEditor, FRender);

	FExampleEditor();

public:
	void OnInstalled(FRender&, FRenderContext&) override;
	/** Pass0: editor input takeover. Runs FIRST, before the game-UI feature's IInitViews
	 *  feeds + NewFrame()s its IO. Tastes the Win32 cursor, re-bases it to the viewport
	 *  panel rect (clamped panel-local) and feeds the game context via FUIFeature::GetUI()
	 *  -> SetEditorInput, so the game UI only responds inside the panel and its layout
	 *  matches the displayed panel. Blocked before FUIFeature's IRenderUI (ctor). */
	void EditorInput(FRender&, FRenderContext&) override;
	/** Pass3: run the editor's OWN InitViews + Render in a single IEditorCompose stage
	 *  (after the game-UI composite IRenderUI, before the frame's IPresent blit). It takes
	 *  over the frame's present target with its EditorRT. */
	void EditorCompose(FRender&, FRenderContext&) override;
	void PreUnInstall(FRender&, FRenderContext&) override;

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
	/** The host's main docking-space node id (owner of the frame shell). A component
	 *  calls DockBuilder/SetNextWindowDockID against it to land inside the shared space. */
	std::uint32_t GetEditorDockSpaceId() const { return EditorDockSpaceId; }

	/** 编辑器视图的翻译作用域名（`UI.Scope.Editor`）。**集中一处**：4 个面板建视图时用它声明
	 *  `FUIView::SetRenderScope`，本宿主的翻译入口与抽干循环声明同一个 —— 「同名字 = 同作用域」
	 *  是这套筛选的全部依据，5 份字面量各自漂移就会静默丢视图。 */
	static UI::FUIName EditorRenderScope();
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

	/** UI 能力注册凭据（`BindUIResourceResolver` / `BindUIClipboardHandlers` 的返回值），
	 *  在 PreUnInstall 里交还。槽挂在 UI 层（FUIViewRegistry）而不是文件级 static，所以槽的
	 *  析构时刻由 FrameGraph 驱动；本模块一旦先卸载，UI 层的 IShutdown 会清掉槽并记名报错。 */
	FSubscriptionID ResolverToken = 0;
	FSubscriptionID ClipboardToken = 0;

	// Input cursors -- one per consumer, over the platform's TAGGED input ring.
	//
	// The platform publishes a frame's input (snapshot + its edge events) under a frame INDEX. A
	// consumer reads every frame it has not read yet, in order, EXACTLY ONCE. Two cursors, not one
	// shared batch: the editor's own ImGui context (pass3) and the game-UI context (fed from pass0
	// through FUIFeature::SetEditorInput) are independent readers, and neither may skip a frame the
	// other happened to take -- the previous design drained one shared stream, so a consumer that
	// missed the frame carrying a key RELEASE never learned the key came up, and ImGui kept it down
	// and auto-repeated it (one Backspace erasing a whole line, one arrow walking to the far left).
	std::uint64_t GameInputCursor = 0;     // fed to the game-UI context from EditorInput (pass0)
	std::uint64_t EditorInputCursor = 0;   // feeds THIS editor context (pass3)

	/** Deferred key RELEASES. ImGui derives "a key was pressed" from the key state at FRAME
	 *  boundaries, so a press and its release that both land inside ONE feed cancel each other out
	 *  and the tap never existed (measured: five Backspace taps delivered together, every one of
	 *  them swallowed -- only a LONG press worked, because it spans a frame). A release arriving in
	 *  the same feed as its press is therefore held back one feed, which leaves the key down for
	 *  exactly one ImGui frame -- enough for ImGui to observe the press. */
	std::vector<int> DeferredKeyReleases;
	bool KeyPressedThisFeed[Platform::MInputContext::KeyCount] = {};

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

	FEditorContext EditorContext;        // shared component state
	std::uint32_t EditorDockSpaceId = 0; // host DockSpace node id, set each DrawEditorPanels
	/** Viewport panel rect (client pixels) the game UI is displayed into, published by the
	 *  viewport component via ReportViewportRect. EditorInput re-bases the game cursor to it.
	 *  bVpValid = false until the panel has been drawn at least once (valid positive size). */
	float VpX = 0.f, VpY = 0.f, VpW = 0.f, VpH = 0.f;
	bool bVpValid = false;
};

// Stage dispatch for the editor's own stages. Placed AFTER the class on purpose: each expansion names
// `FExampleEditor::FContext`, and a full specialization's body is compiled where it is written.
MAHO_DECLARE_STAGE_DISPATCH(FExampleEditor, IEditorInit, IEditorInit, Init)
MAHO_DECLARE_STAGE_DISPATCH(FExampleEditor, IEditorShutdown, IEditorShutdown, Shutdown)

} // namespace Maho
