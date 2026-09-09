#pragma once

#include "EditorConsoleApi.h"
#include <Engine/Layer.h>
#include <ExampleEditor.h>
#include <Log.h>

#include <deque>
#include <mutex>
#include <string>

namespace Maho
{

/**
 * EditorConsole - an editor component plugin that mirrors the live engine log
 * stream into a host-docked ImGui panel. This is the log-ONLY build: it draws just
 * the accumulated message list (level-colored text, selectable/copyable), with no
 * filter toolbar and no command input box.
 *
 * It mounts IEditorInit/IEditorPanel/IEditorShutdown. Init subscribes to the Log
 * layer's listener stream (producer thread pushes into a mutex-guarded deque, so
 * the panel is safe even though LogLine runs on any emitting thread); each frame
 * Draw drains the deque into level-colored Selectable lines (per-level color, like
 * UE/Unity -- a line's severity is encoded by its color), click-to-select a line and
 * Ctrl+C to copy it, with a Copy All button in the header. Shutdown unsubscribes before
 * the Log layer goes away. Like every editor component it carries no ImGui/RHI
 * resource ownership -- the host owns the ImGui context.
 */
class FEditorConsole : public FLayer<IEditorInit, IEditorPanel, IEditorShutdown>
{
	MAHO_DECLARE_LAYER(FEditorConsole, "EditorConsole.dll");

public:
	void Init(FExampleEditor& Editor) override;
	void Draw(FExampleEditor& Editor) override;
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

	// -- line buffer (producer writes, frame thread snapshots) --
	std::mutex                LinesMutex;   // guards Lines against producer (LogLine) races
	std::deque<FLogEntry>     Lines;
	FSubscriptionID           ListenerId = 0;
	/** UE/Unity-style multi-line selection (indices into the current Snapshot):
	 *  click a line to seed the anchor, drag (or Shift+click) to extend the range,
	 *  Ctrl+C copies the whole range. Per-line color and click-to-select coexist
	 *  because every line is its own Selectable, not one shared InputTextMultiline. */
	int                       SelAnchor = -1;   // selection start line (anchor), -1 = none
	int                       SelEnd = -1;      // selection end line, -1 = none

	/** Head lines dropped by the MaxLines trim since the last frame's snapshot. The frame
	 *  thread consumes this (under LinesMutex) to shift the scroll view back down, so a
	 *  scrolled-up reader isn't nudged up by each head trim. Written by the producer. */
	std::size_t               DroppedCount = 0;

	/** Live "Filter logs..." box text in the top menu bar. Case-insensitive substring
	 *  match against the rendered line; empty shows everything. Fixed buffer so ImGui's
	 *  InputText edits in place without std::string reallocation quirks. */
	char                      FilterBuffer[256] = { 0 };
};

} // namespace Maho
