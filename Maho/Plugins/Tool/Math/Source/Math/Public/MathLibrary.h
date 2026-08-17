#pragma once

#include "MathApi.h"
#include <Toolkit.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Maho
{

namespace Math
{

using FVector2 = glm::vec2;
using FVector3 = glm::vec3;
using FVector4 = glm::vec4;
using FMatrix4 = glm::mat4;
using FQuaternion = glm::quat;

template <typename T> [[nodiscard]] constexpr T Lerp(const T& A, const T& B, float Alpha) { return A + (B - A) * Alpha; }
template <typename T> [[nodiscard]] constexpr T Clamp(const T& Value, const T& Min, const T& Max) { return Value < Min ? Min : (Value > Max ? Max : Value); }
[[nodiscard]] inline constexpr float DegreesToRadians(float Deg) { return Deg * 0.017453292519943295f; }
[[nodiscard]] inline constexpr float RadiansToDegrees(float Rad) { return Rad * 57.29577951308232f; }

/** Math library extension (GLM + math helpers). Pre-app toolkit (driven by EToolStage). */
class MAHO_MATH_API FMath : public TExtension<EToolStage, FMath>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

protected:
	friend TSingleton<FMath>;
	FMath() = default;
};

} // namespace Math

} // namespace Maho
