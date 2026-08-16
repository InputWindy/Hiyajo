#pragma once

#include "MathApi.h"
#include <Engine.h>

namespace Maho
{

namespace Math
{

/** Math library extension (GLM + math helpers). Pre-app toolkit (driven by EToolStage). */
class MAHO_MATH_API FMath final : public TExtension<EToolStage, FMath>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

private:
	friend TSingleton<FMath>;
	FMath() = default;
};

} // namespace Math

} // namespace Maho
