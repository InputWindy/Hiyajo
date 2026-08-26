// Compile + run check for FLayerBase (the polymorphic layer anchor) and the
// pattern: commands flow through FQueue's catalog lanes (Install/Uninstall
// command shapes) and are applied by the consumer at a safe point
// (FLayer::FlushCommands drains those lanes). Also exercises Is<T>().
#include <Core/Queue.h>
#include <Engine/Layer.h>

#include <cstdio>
#include <cstdint>
#include <memory>

using namespace Maho;

namespace
{
	struct FX : FLayer<> {};
	struct FY : FLayer<> {};
}

int main()
{
	// FLayerBase anchor + Is<T>() runtime type test
	FX X;
	FLayerBase& Base = X;
	if (!Base.Is<FX>() || Base.Is<FY>())
	{
		std::puts("[FAIL] FLayerBase::Is<T>() mismatch");
		return 1;
	}

	// catalog lanes: enqueue one Install + one Uninstall command; the queue only
	// holds them (consumer applies at a safe point, e.g. FlushCommands).
	FQueue Q;
	if (!Q.IsEmpty())
	{
		std::puts("[FAIL] queue should start empty");
		return 1;
	}
	Q.Enqueue(std::make_unique<FInstallCommand>());
	Q.Enqueue(std::make_unique<FUninstallCommand>());
	if (Q.Size() != 2)
	{
		std::printf("[FAIL] size=%zu want=2 (one per lane)\n", Q.Size());
		return 1;
	}

	// drain each lane FIFO, exactly as FLayer::FlushCommands does
	auto Inst = Q.Dequeue(static_cast<std::uint64_t>(ELayerCommand::Install));
	auto Un = Q.Dequeue(static_cast<std::uint64_t>(ELayerCommand::Uninstall));
	if (!Inst || !Un || !Q.IsEmpty())
	{
		std::printf("[FAIL] drain install/uninstall lanes: size=%zu\n", Q.Size());
		return 1;
	}

	std::puts("ok: FLayerBase anchor + Is<T>() + Install/Uninstall catalog lanes");
	return 0;
}
