const float2 positions[] = {
  float2(0.0f, -0.5f),
  float2(0.5f, 0.5f),
  float2(-0.5f, 0.5f)
};

const float3 colors[] = {
  float3(1.0f, 0.0f, 0.0f),
  float3(0.0f, 1.0f, 0.0f),
  float3(0.0f, 0.0f, 1.0f)
};

float4 main(in uint vertexId : SV_VertexId, out float3 fragColor : COLOR) : SV_POSITION
{
  fragColor = colors[vertexId];
  return float4(positions[vertexId], 0.0f, 1.0f);
}