struct PSInput {
  float4 position : SV_POSITION;
  float2 texcoord : TEXCOORD;
};

PSInput main(float2 screen_pos : POSITION, float2 texcoord : TEXCOORD) { 
  PSInput result;

  result.position = float4(screen_pos, 0.f, 1.f);
  result.texcoord = texcoord;

  return result;
}