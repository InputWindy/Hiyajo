#pragma once

#include "UISystemApi.h"

#include <functional>
#include <mutex>
#include <vector>

namespace Maho
{
namespace GameWorld
{

/**
 * Game->render UI command broker (producer/consumer).
 *
 * GAME SIDE (any thread, usually the game/main thread): a UI component arranges what
 * to draw by submitting a draw CLOSURE -- a std::function<void()> whose body calls
 * ImGui (Begin/Image/End). ImGui is immediate-mode: there is NO command record/replay,
 * so the closure is not recorded -- it is collected here and EXECUTED LATER on the
 * render worker, inside the ImGui frame (between NewFrame and Render). Submitting is
 * cheap and thread-safe (a mutex guards the per-frame list).
 *
 * RENDER SIDE (FUIFeature::InitViews): calls Execute() once per ImGui frame. The
 * pending closures are swapped out atomically first (concurrent game-side submits land
 * in the NEXT frame's list) and then run in order, so the ImGui frame is never
 * touched by the game thread -- only by the worker that executes the closures.
 */
class MAHO_UISYSTEM_API FUIBuilder
{
public:
	/** Register one draw closure to run in the current (or next) ImGui frame. */
	void Submit(std::function<void()> Closure);

	/** Run every pending closure in order (called once per ImGui frame, between
	 *  NewFrame and Render). Pending closures are swapped out atomically first, so
	 *  concurrent game-side submits go to the next frame's list. Exceptions from a
	 *  closure propagate to the caller (FUIFeature contains them so the frame is
	 *  still closed). Returns whether any closure ran. */
	bool Execute();

	/** Snapshot the pending closures (a copy), WITHOUT consuming them. Called from
	 *  the UI-built event handler on the GAME thread, just after Submit() finished
	 *  building this frame -- so the copy captures a complete frame, and the render
	 *  side can run its own snapshot between NewFrame and Render regardless of the
	 *  async game update. */
	std::vector<std::function<void()>> CopyFrame() const;

	[[nodiscard]] bool HasCommands() const;

private:
	mutable std::mutex Mutex;
	std::vector<std::function<void()>> Frame;
};

} // namespace GameWorld
} // namespace Maho
