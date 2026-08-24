// Compile + run check for FLayerBase (the polymorphic layer anchor) and the
// pattern: a concrete layer owns an FQueue of value commands and Flushes them.
#include <Core/Queue.h>
#include <Engine/Layer.h>

#include <cstdio>
#include <memory>

using namespace Maho;

namespace
{
	int gInstalls = 0;

	// a value command a layer enqueues: "install a feature of kind N"
	struct FInstallCmd
	{
		int Kind;
		bool operator==(const FInstallCmd& O) const { return Kind == O.Kind; }
		void Execute() { ++gInstalls; }
	};

	// a concrete layer: owns a queue of install commands, flushes at safe point
	struct FRenderer : FLayerBase
	{
		FQueue<FInstallCmd> Pending;
		void Install(int Kind) { Pending.Enqueue(FInstallCmd{ Kind }); }
		void FlushQueued() { Pending.Flush(); }
	};
}

int main()
{
	FRenderer R;
	R.Install(1);
	R.Install(1); // deduped
	R.Install(2);

	if (R.Pending.IsEmpty())
	{
		std::puts("[FAIL] commands should be pending before Flush");
		return 1;
	}

	R.FlushQueued(); // run the (deduped) pending commands

	if (gInstalls != 2 || !R.Pending.IsEmpty())
	{
		std::printf("[FAIL] installs=%d pendingEmpty=%d want=2/true\n",
			gInstalls, R.Pending.IsEmpty());
		return 1;
	}
	std::puts("ok: FLayerBase anchor + value-command queue flush pattern");
	return 0;
}
