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
			if (!D.TargetStage.empty())
			{
				Message += " at " + std::string(D.TargetStage);
			}
			if (D.FrameOffset != 0)
			{
				Message += " (frame offset " + std::to_string(D.FrameOffset) + ")";
			}
		}
		Message += " at " + std::string(D.Stage);
		Message += " -- " + std::string(D.Reason);
		ReportError(Message.c_str());
	}
}

} // namespace Detail

// NOTHING GRAPH-SHAPED LIVES HERE ANY MORE. Two earlier shapes are gone: `FFrameGraphHost` (the mixin
// base that owned a graph) and the builder's own `FFrameGraph* InjectedGraph` (a non-owning reference
// handed in once through `SetGraph`). The graph is a member of the frame that DRIVES the sequence
// (`FEngineBase`, `FRender`, `FGameWorld`, `FExampleEditor` all own theirs) and reaches every driving
// call as an `FFrameGraph&` PARAMETER, so "who owns the graph / who fences and releases it" is a
// property of the frame AND "which graph a sequence runs on" is readable at the call site. What is
// left out of line here is only the interface's key function (below), for the same reason: one vtable
// and one deleting destructor in Maho.dll rather than a COMDAT copy per module.

// Out of line on purpose, exactly like FThreadedServer's virtuals: the interface is a base of
// plugin-side objects, so keeping its first out-of-line virtual (the destructor) in the engine's
// own module gives it a KEY FUNCTION -- one vtable and one deleting destructor in Maho.dll rather
// than a COMDAT copy per module, which is also what makes the installer's dynamic_cast to it
// compare against a single shared RTTI record across the DLL boundary.
IFrameCollector::~IFrameCollector() = default;

// ── the engine's metadata registrars (Engine/Frame.h) ───────────────────────────────────────────
//
// Out of line for the same reason as everything else in this file: a declaration macro is expanded in
// plugin modules, so these constructors have to be imported symbols rather than inline bodies each of
// those modules instantiates. Their bodies are the whole ENCODING of the two axes the engine owns;
// what a record MEANS lives in Engine/Frame.h, next to the decoders.

namespace Detail
{

FFrameRegistrar::FFrameRegistrar(std::string_view Frame, std::string_view Collector) noexcept
{
	if (!Frame.empty())
	{
		DeclareFrameMeta(Frame, Collector);
	}
}

FStageProtocolRegistrar::FStageProtocolRegistrar(std::string_view Stage, std::string_view Method,
	std::string_view Context) noexcept
{
	if (!Stage.empty())
	{
		DeclareStageMeta(Stage, Method, Context);
	}
}

} // namespace Detail

} // namespace Maho
