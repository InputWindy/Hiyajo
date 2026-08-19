#pragma once

#include "MathApi.h"
#include <Maho.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Maho
{

namespace Math
{

// GLM type aliases.
using FVector2 = glm::vec2;
using FVector3 = glm::vec3;
using FVector4 = glm::vec4;
using FMatrix4 = glm::mat4;
using FQuaternion = glm::quat;

// Pure math helpers (header-only).
template <typename T> [[nodiscard]] constexpr T Lerp(const T& A, const T& B, float Alpha) { return A + (B - A) * Alpha; }
template <typename T> [[nodiscard]] constexpr T Clamp(const T& Value, const T& Min, const T& Max) { return Value < Min ? Min : (Value > Max ? Max : Value); }
[[nodiscard]] inline constexpr float DegreesToRadians(float Deg) { return Deg * 0.017453292519943295f; }
[[nodiscard]] inline constexpr float RadiansToDegrees(float Rad) { return Rad * 57.29577951308232f; }

/** Math library extension (GLM + helpers). A plain singleton type provider, header-only. */
class MAHO_MATH_API FMath : public Maho::TExtensionList<FMath>
{
public:
	// Pure function library — no lifecycle, no stage.
};

} // namespace Math

} // namespace Maho
