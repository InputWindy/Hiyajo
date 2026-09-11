#pragma once

#include "EditorConsoleApi.h"
#include <Engine/Layer.h>
#include <ExampleEditor.h>
#include <Log.h>
#include <UIView.h>

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Maho
{

/**
 * EditorConsole - an editor component plugin that mirrors the live engine log
 * stream into a host-docked panel. The panel owns a persistent declarative UI
 * tree (`UI::FUIView`): `Update` -- the declaration phase, which the host runs
 * before it begins its UI frame -- rebuilds that tree; the host then translates
 * it in-frame. The plugin never touches the backend.
 *
 * It mounts IEditorInit/IEditorPanel/IEditorShutdown. Init subscribes to the Log
 * layer's listener stream (producer thread pushes into a mutex-guarded deque, so
 * the panel is safe even though LogLine runs on any emitting thread); each frame
 * `Update` drains the deque into one level-colored FUISelectable per visible line,
 * click-to-select / Shift+click-or-drag range select / Ctrl+C to copy the selection
 * (Ctrl+A selects every visible line first), plus a live string-match filter box in the
 * toolbar and a CVar command line at the bottom (Enter submits; a floating completion list
 * follows the box -- `↑`/`↓` walk it, filling the highlighted name back into the box -- and
 * with nothing to complete `↑` re-opens the same popup as the last 10 executed commands,
 * `↑`/`↓` walking those instead).
 * Right-clicking the log area opens a context menu at the pointer whose first entry is Clear
 * (it replaced the old toolbar Clear button).
 * Shutdown unsubscribes before the Log layer goes away. Like every editor component
 * it carries no backend/RHI resource ownership -- the host owns the UI context.
 *
 * Widget values live on the tree nodes: the backend writes them in place during
 * translation, so `Update` reads them back into `FilterBuffer`/`CvarBuffer` before
 * declaring (a suggestion pick or a command run refreshes the buffer first and marks
 * it authoritative for that frame instead).
 */
class FEditorConsole : public FLayer<IEditorInit, IEditorPanel, IEditorShutdown>
{
	MAHO_DECLARE_LAYER(FEditorConsole);

public:
	void Init(FExampleEditor& Editor) override;
	/** 声明期（宿主 `NewFrame` 之前）：只重建本视图的树，不碰后端。 */
	void Update(FExampleEditor& Editor) override;
	/** 解绑日志监听 + 注销视图（注册表只持裸指针，从不删除）。 */
	void Shutdown(FExampleEditor& Editor) override;

private:
	/** One buffered console line. Timestamp is captured locally at enqueue time,
	 *  since FLogMessage itself only carries Level/Category/Message. */
	struct FLogEntry
	{
		ELogLevel   Level;
		std::string Category;
		std::string Message;
	};

	static constexpr std::size_t MaxLines = 4096;

	/** Lazy view creation + registration: the UI plugin may not be up during Init. */
	UI::FUIView* EnsureView(FExampleEditor& Editor);

	/** Run the CVar command line: parse "name [value]" against the ConsoleVariable
	 *  registry, echo the result back into the log list and clear the box. */
	void ExecuteCvarLine();

	/** Write one line back into the command line (shared by a history row and a completion
	 *  row pick, and by the `↑`/`↓` walks): the buffer becomes authoritative for this frame,
	 *  the text is recorded as the last picked name so the completion list does not re-open
	 *  over it, and the box asks for the keyboard back. */
	void FillFromList(const std::string& Text);

	/** Fill the command line from `History[HistoryIndex]` (shared by the `↑`/`↓` keys and a row
	 *  pick). Also closes the completion list: the two lists are mutually exclusive. */
	void FillFromHistory();

	/** Walk the command history by `Step` (-1 = older, +1 = newer). A closed list (-1) is
	 *  opened by an older step and lands on the newest line; the ends clamp. */
	void StepHistory(int Step);

	/** Walk the completion candidates of `Matches` (the pinned needle's, see `SuggestNeedle`)
	 *  by `Step`: entering pins the needle and lands on the last row (`↑`) or the first (`↓`),
	 *  the ends clamp, and every step fills the highlighted name back into the box. The list
	 *  stays open -- walking is not picking. */
	void StepSuggest(int Step, const std::vector<std::string>& Matches);

	/** Persistent UI tree, owned here, registered in the UI view registry. */
	std::unique_ptr<UI::FUIView> View;

	/** Stable per-line node Ids, indexed by line number (built once, reused). */
	std::vector<UI::FUIName>     LineIds;

	// -- line buffer (producer writes, frame thread snapshots) --
	std::mutex                LinesMutex;   // guards Lines against producer (LogLine) races
	std::deque<FLogEntry>     Lines;
	FSubscriptionID           ListenerId = 0;
	/** UE/Unity-style line selection: click a line to select it; Shift+click extends the
	 *  range from the anchor (modifiers arrive with the event -- `GetLastModifiers()`);
	 *  dragging across lines extends it too (a pressed line plus a hovered one).
	 *  Per-line color and click-to-select coexist because every line is its own Selectable. */
	int                       SelAnchor = -1;   // selection start line (anchor), -1 = none
	int                       SelEnd = -1;      // selection end line, -1 = none

	/** Head lines dropped by the MaxLines trim since the last frame's snapshot. The frame
	 *  thread consumes this (under LinesMutex) to shift the scroll view back down, so a
	 *  scrolled-up reader isn't nudged up by each head trim. Written by the producer. */
	std::size_t               DroppedCount = 0;

	/** Live "Filter logs..." box text in the top menu bar. Case-insensitive substring
	 *  match against the rendered line; empty shows everything. Read back from the
	 *  filter node before each declaration. */
	char                      FilterBuffer[256] = { 0 };

	/** CVar command line below the log area. Executing runs "name [value]" against the
	 *  ConsoleVariable registry and echoes the result back into the log panel. Read back
	 *  from the cvar node before each declaration. */
	char                      CvarBuffer[256] = { 0 };

	/** Set when a suggestion is picked: the buffer (not the node) is the truth for this
	 *  frame, so the read-back must not push the node's stale text back, and the node's
	 *  text must be written from the buffer. */
	bool                      CvarAuthoritative = false;

	/** The name last picked from the completion list. While the box holds exactly this
	 *  text, the list must NOT auto-reopen: the box keeps keyboard focus (and the backend's
	 *  active item) across the pick, so "active + non-empty text" alone reopens the list on
	 *  the next frame and the user can never leave it (every row they click re-opens it --
	 *  the editor looks frozen). Editing the text invalidates the guard by itself; picking
	 *  a different row overwrites it. */
	char                      CvarPickedName[256] = { 0 };

	/** Set by the cvar box's Enter submit. The host drains events BEFORE `Update`, so the
	 *  buffer is not yet synced at callback time -- the command runs in `Update`, right
	 *  after the read-back. */
	bool                      CvarRunRequested = false;

	/** Whether the command line currently holds the keyboard (the box's active bit, read back
	 *  from the node each frame). The `↑`/`↓` walks run in `Update` and gate on this, so the
	 *  history list does not open while another box (e.g. the filter) is the one being typed into. */
	bool                      CvarEditing = false;

	/** Set when a suggestion is picked: the cvar node asks the backend for the keyboard
	 *  focus on its next translation (`RequestKeyboardFocus()`), so typing continues. */
	bool                      CvarPendingFocus = false;

	/** Set by the log panel's `Ctrl+C` shortcut (declarative, see `FUIKeyChord`); `Ctrl+A`
	 *  only moves the selection range onto every visible line, so "copy all" is
	 *  `Ctrl+A` then `Ctrl+C`. The work happens in `Update`: only there is the frame's
	 *  visible-line snapshot available. */
	bool                      CopyRequested = false;
	bool                      SelectAllRequested = false;

	/** Whether the name-completion list should be shown. It persists across the frame that
	 *  the click lands on (clicking a row deactivates the box before the click is drained),
	 *  and is only closed when the box clears, an exact name is typed, or it loses focus. */
	bool                      CvarDropdownOpen = false;

	/** Command history behind the command line's `↑` key: the last `MaxHistory` executed
	 *  lines, newest last. `HistoryIndex` is the highlighted row of the `↑` list, -1 = the
	 *  list is closed; the rows reuse the completion popup (they are mutually exclusive), the
	 *  newest row is the bottom one so `↑` walks backwards away from the box. */
	static constexpr std::size_t MaxHistory = 10;
	std::deque<std::string>   History;
	int                       HistoryIndex = -1;

	/** Highlight of the completion list while `↑`/`↓` walks it, -1 = not walking. A step fills
	 *  the highlighted name into the box, so the box holds `Matches[SuggestIndex]`. */
	int                       SuggestIndex = -1;

	/** The filter needle pinned when the walk entered the completion list. Walking writes whole
	 *  names into the box, and following the box's text would narrow the candidates down to that
	 *  one name and then have them dropped by the "the box already holds an exact name" rule --
	 *  the list would collapse mid-walk. Empty = not walking. */
	char                      SuggestNeedle[256] = { 0 };

	/** The `↑`/`↓` step recorded by the shortcut callbacks. They run during the event drain,
	 *  before `Update` has gathered the candidates, so which list the step belongs to
	 *  (completion or history) is decided in `Update`. */
	int                       PendingStep = 0;

	/** Log-area context menu (the old toolbar Clear button's replacement): right-clicking the
	 *  log panel opens it. The backend writes the right-click and the pointer position into the
	 *  panel's runtime state on the frame it happens (`EUIInputFlags::ContextMenu`); `Update`
	 *  reads them one frame later, exactly like the line hit-state reads above. */
	bool                      bContextMenuOpen = false;
	UI::FUIVector2            ContextMenuAnchor{};
};

} // namespace Maho
