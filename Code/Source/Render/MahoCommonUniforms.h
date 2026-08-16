#pragma once

#include <Core/Misc/Export.h>

namespace Maho
{

// Must match layout of FrameUniforms in MahoCommon.glsl
// set=0, binding=0, std140 layout
struct FFrameUniforms
{
	float View[16];              // offset 0,   size 64  (mat4)
	float Proj[16];              // offset 64,  size 64  (mat4)
	float ViewProj[16];          // offset 128, size 64  (mat4)
	float CameraWorldPos[3];     // offset 192, size 12  (vec3)
	float Time;                  // offset 204, size 4
};
static_assert(sizeof(FFrameUniforms) == 208, "FFrameUniforms size mismatch with GLSL std140 layout");

// Must match layout of ObjectUniforms in MahoCommon.glsl
// set=1, binding=0, std140 layout
struct FObjectUniforms
{
	float LocalToWorld[16];                 // offset 0,  size 64 (mat4)
	float LocalToWorldInverseTranspose[16]; // offset 64, size 64 (mat4)
};
static_assert(sizeof(FObjectUniforms) == 128, "FObjectUniforms size mismatch with GLSL std140 layout");

} // namespace Maho
