#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec3 fragColor;

layout(push_constant) uniform PushConstants {
	float time;
};

vec2 positions[3] = vec2[](
  vec2(0.0, -0.5),
  vec2(0.5, 0.5),
  vec2(-0.5, 0.5)
);

vec3 colors[3] = vec3[](
  vec3(1.0, 0.0, 0.0),
  vec3(0.0, 1.0, 0.0),
  vec3(0.0, 0.0, 1.0)
);

void main()
{
  float c = cos(time);
  float s = sin(time);
  
  uint vid = gl_VertexIndex;
  vec2 position = positions[vid];
  vec2 rotmatRow1 = vec2(c, s);
  vec2 rotmatRow2 = vec2(-s, c);
  vec2 rotatedPosition = rotmatRow1 * position.xx + rotmatRow2 * position.yy;
  
  fragColor = colors[vid];
  gl_Position = vec4(rotatedPosition, 0.0, 1.0);
}