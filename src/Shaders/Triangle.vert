#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 position;
layout(location = 1) in vec3 color;

layout(location = 0) out vec3 fragColor;

layout(binding = 0) uniform MVP
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
	fragColor = color;
	gl_Position = proj * view * model * vec4(position.xy, 0.0, 1.0);
}