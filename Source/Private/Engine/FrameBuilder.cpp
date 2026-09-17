#include <Engine/FrameBuilder.h>

#include <Core/Fatal.h>

#include <string>
#include <typeindex>

namespace Maho
{

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

} // namespace Maho
