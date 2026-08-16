#pragma once

/**
 * Animation keyframe (project asset type). Shared by FAnimationTrack and
 * the render-side FAnimationTrackSnapshot.
 */

namespace Maho
{

struct FAnimationKey
{
	float Time = 0.f;
	float Translation[3] = {0, 0, 0};
	float Rotation[4] = {0, 0, 0, 1}; // xyzw
	float Scale[3] = {1, 1, 1};
};

} // namespace Maho
