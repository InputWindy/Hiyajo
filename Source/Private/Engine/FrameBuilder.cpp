#include <Engine/FrameBuilder.h>

#include <Core/Fatal.h>

#include <string>
#include <typeindex>

namespace Maho
{

namespace
{
	/** The stage call this thread is currently inside, published by MAHO_DECLARE_STAGE_DISPATCH.
	 *  thread_local, not a collector member: frames pipeline, so two frames' stage bodies run at the
	 *  same time and "the frame I belong to" is a property of the call.
	 *
	 *  It carries BOTH halves of the call (see FFrameCall): the per-phase SLOT, and the ROUND the node
	 *  was submitted with. The round travels by value from the node's own closure, so a node that runs
	 *  after the slot was refilled by a later submission still sees its own round. */
	thread_local FFrameCall GCurrentFrameCall{};
}

void* GetCurrentFrameContext() noexcept
{
	return GCurrentFrameCall.Slot;
}

std::uint64_t GetCurrentStageFrame() noexcept
{
	return GCurrentFrameCall.Frame;
}

FScopedFrameContext::FScopedFrameContext(FFrameCall InCall) noexcept
	: Previous(GCurrentFrameCall)
{
	GCurrentFrameCall = InCall;
}

FScopedFrameContext::~FScopedFrameContext()
{
	// Restore, don't clear: a stage body may run a one-shot batch (an install/uninstall flush), whose
	// own stage calls publish and restore their contexts -- the outer frame must still be visible
	// after they return.
	GCurrentFrameCall = Previous;
}

namespace Detail
{

void ReportBridgeDiagnostics(const std::vector<FFrameBridge::FDiagnostic>& Diagnostics)
{
	for (const FFrameBridge::FDiagnostic& D : Diagnostics)
	{
		std::string Message = "frame declaration: '" + std::string(D.Frame) + "'";
		if (!D.Target.empty())
		{
			Message += D.bReverse ? " blocks '" : " waits for '";
			Message += std::string(D.Target) + "'";
			if (D.TargetStage != std::type_index(typeid(void)))
			{
				Message += " at " + std::string(D.TargetStage.name());
			}
			if (D.FrameOffset != 0)
			{
				Message += " (frame offset " + std::to_string(D.FrameOffset) + ")";
			}
		}
		Message += " at " + std::string(D.Stage.name());
		Message += " -- " + std::string(D.Reason);
		ReportError(Message.c_str());
	}
}

} // namespace Detail

// Out of line on purpose, exactly like FThreadedServer's virtuals: the interface is a base of
// plugin-side objects, so keeping its first out-of-line virtual (the destructor) in the engine's
// own module gives it a KEY FUNCTION -- one vtable and one deleting destructor in Maho.dll rather
// than a COMDAT copy per module, which is also what makes the installer's dynamic_cast to it
// compare against a single shared RTTI record across the DLL boundary.
IFrameCollector::~IFrameCollector() = default;

} // namespace Maho
