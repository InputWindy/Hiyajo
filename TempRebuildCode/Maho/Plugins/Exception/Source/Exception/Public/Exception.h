#pragma once

#include "ExceptionApi.h"
#include <Engine.h>

namespace Maho
{

/** Exception handling extension (non-fatal exception events). Engine extension (driven by EEngineStage). */
class MAHO_EXCEPTION_API FException final : public TExtension<EEngineStage, FException>
{
public:
	[[nodiscard]] bool ExecuteStage(EEngineStage Stage) override;

private:
	friend TSingleton<FException>;
	FException() = default;
};

} // namespace Maho
