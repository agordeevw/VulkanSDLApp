float4 main(in float3 fragColor : COLOR) : SV_Target
{
  return float4(fragColor, 1.0f);
}