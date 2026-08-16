#pragma once

/**
 * Forward-renderer scene data gathered from the ECS world (game thread).
 * Feature-specific — lives in the project, not the engine core.
 */

#include <cstdint>
#include <vector>

namespace Maho
{

enum class EScenePrimitiveType : std::uint8_t
{
	ColoredTriangle = 0,
};

struct FSceneDrawItem
{
	EScenePrimitiveType Type = EScenePrimitiveType::ColoredTriangle;
	/** Row-major 4x4 LocalToWorld. */
	float LocalToWorld[16] = {
			1.f, 0.f, 0.f, 0.f,
			0.f, 1.f, 0.f, 0.f,
			0.f, 0.f, 1.f, 0.f,
			0.f, 0.f, 0.f, 1.f,
	};
};

struct FCameraFrameData
{
	float View[16] = {
			1.f, 0.f, 0.f, 0.f,
			0.f, 1.f, 0.f, 0.f,
			0.f, 0.f, 1.f, 0.f,
			0.f, 0.f, 0.f, 1.f,
	};
	float FOV = 60.0f;
	float NearPlane = 0.1f;
	float FarPlane = 1000.0f;
	float AspectRatio = 16.0f / 9.0f;
	bool bOrthographic = false;
	float OrthoSize = 10.0f;
};

} // namespace Maho
