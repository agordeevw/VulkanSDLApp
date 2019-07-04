#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 position;
layout(location = 1) in vec3 color;

layout(location = 0) out vec3 fragColor;

layout(push_constant) uniform PushConstants {
	float time;
};

void main()
{
  float c = cos(time);
  float s = sin(time);
  
  vec2 rotmatRow1 = vec2(c, s);
  vec2 rotmatRow2 = vec2(-s, c);
  vec2 rotatedPosition = rotmatRow1 * position.xx + rotmatRow2 * position.yy;
  
  fragColor = color;
  gl_Position = vec4(rotatedPosition, 0.0, 1.0);
}