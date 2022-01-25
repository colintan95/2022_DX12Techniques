struct PSInput {
  float4 position : SV_POSITION;
  float2 texcoord : TEXCOORD;
};

//Texture2D gbuf_tex : register(t0);
//SamplerState gbuf_sampler : register(s0);

float4 main(PSInput input) : SV_TARGET {
  return float4(1.f, 0.f, 0.f, 1.f);
	//return gbuf_tex.Sample(gbuf_sampler, input.texcoord);
}