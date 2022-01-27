struct PSInput {
  float4 position : SV_POSITION;
  float2 texcoord : TEXCOORD;
};

Texture2D gbuf_tex : register(t0);
Texture2D pos_gbuf_tex : register(t1);
Texture2D diffuse_gbuf_tex : register(t2);

SamplerState gbuf_sampler : register(s0);

float4 main(PSInput input) : SV_TARGET {
	return gbuf_tex.Sample(gbuf_sampler, input.texcoord);
  // return pos_gbuf_tex.Sample(gbuf_sampler, input.texcoord);
}