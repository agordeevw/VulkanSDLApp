#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

layout(set = 0, binding = 0) uniform MVP
{
	mat4x4 model;
	mat4x4 view;
	mat4x4 proj;
};

layout(push_constant) uniform PushConstants {
	float time;
};

void main()
{
	fragColor = mix(
		inColor,
		vec3(1.0f, 1.0f, 1.0f),
		0.5f * (sin(5.0f * time) + 1.0f));
	gl_Position = proj * view * model * vec4(inPosition.xy, 0.0, 1.0);
}