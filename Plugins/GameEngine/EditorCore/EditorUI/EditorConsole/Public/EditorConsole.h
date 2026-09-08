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
 * Draw drains the deque into a plain, selectable message body. Shutdown unsubscribes
 * before the Log layer goes away. Like every editor component it carries no
 * ImGui/RHI resource ownership -- the host owns the ImGui context.
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

	// The accumulated log body shown by a READ-ONLY InputTextMultiline (selectable /
	// copyable -- TextColored is not), rebuilt each frame from the line snapshot.
	std::string         LogBody;
};

} // namespace Maho
