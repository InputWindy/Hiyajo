#version 450
#extension GL_ARB_separate_shader_objects : enable

// Conventional forward vertex shader: per-object uniform matrix.

layout(set = 0, binding = 0, std140) uniform FrameUniforms
{
	mat4 View;
	mat4 Proj;
	mat4 ViewProj;
	vec3 CameraWorldPos;
	float Time;
} u_Frame;

layout(set = 1, binding = 0, std140) uniform ObjectUniforms
{
	mat4 LocalToWorld;
	mat4 LocalToWorldInverseTranspose;
} u_Object;

layout(location = 0) in vec3 inPosition;
layout(location = 5) in vec3 inColor;

layout(location = 0) out vec3 vColor;

void main()
{
	gl_Position = u_Frame.ViewProj * u_Object.LocalToWorld * vec4(inPosition, 1.0);
	vColor = inColor;
}
