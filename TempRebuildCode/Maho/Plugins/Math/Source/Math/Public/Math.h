#pragma once

#include "MathApi.h"
#include <Engine.h>

namespace Maho
{

namespace Math
{

/** Math library extension (GLM + math helpers). Pre-app singleton (driven by ESingletonStage). */
class MAHO_MATH_API FMath final : public TExtension<ESingletonStage, FMath>
{
public:
	[[nodiscard]] bool ExecuteStage(ESingletonStage Stage) override;

private:
	friend TSingleton<FMath>;
	FMath() = default;
};

} // namespace Math

} // namespace Maho
