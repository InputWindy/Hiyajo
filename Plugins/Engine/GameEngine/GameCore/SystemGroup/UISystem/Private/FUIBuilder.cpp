#include "FUIBuilder.h"

namespace Maho
{
namespace GameWorld
{

void FUIBuilder::Submit(std::function<void()> Closure)
{
	if (!Closure)
	{
		return;
	}
	std::lock_guard Lock(Mutex);
	Frame.push_back(std::move(Closure));
}

bool FUIBuilder::Execute()
{
	std::vector<std::function<void()>> Batch;
	{
		std::lock_guard Lock(Mutex);
		if (Frame.empty())
		{
			return false;
		}
		Batch.swap(Frame);
	}
	// Run on the caller (render worker), OUTSIDE the builder lock: concurrent game-side
	// submits only ever target the (now swapped-out) next frame's list, so closures can
	// freely call ImGui without a lock held here. An exception propagates to FUIFeature's
	// frame try/catch -- the ImGui frame is still closed (Render always runs).
	for (auto& Fn : Batch)
	{
		Fn();
	}
	return true;
}

std::vector<std::function<void()>> FUIBuilder::CopyFrame() const
{
	std::lock_guard Lock(Mutex);
	return Frame;   // copy of the pending closures (Frame is not consumed)
}

bool FUIBuilder::HasCommands() const
{
	std::lock_guard Lock(Mutex);
	return !Frame.empty();
}

} // namespace GameWorld
} // namespace Maho
