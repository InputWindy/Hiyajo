#pragma once

#include "RHIApi.h"
#include <Core/Async/ThreadedServer.h>
#include <Engine.h>

namespace Maho
{

/** Render hardware interface extension (GPU device). Engine extension (driven by EEngineStage). */
class MAHO_RHI_API FRHI final
	: public TExtension<EEngineStage, FRHI>
	, public FThreadedServer
{
public:
	[[nodiscard]] bool ExecuteStage(EEngineStage Stage) override;

protected:
	[[nodiscard]] const char* GetThreadName() const override;

private:
	friend TSingleton<FRHI>;
	FRHI() = default;
};

} // namespace Maho
