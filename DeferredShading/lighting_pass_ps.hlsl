struct PSInput {
  float4 position : SV_POSITION;
  float2 texcoord : TEXCOORD;
};

Texture2D gbuf_tex : register(t0);
Texture2D pos_gbuf_tex : register(t1);
Texture2D diffuse_gbuf_tex : register(t2);
Texture2D normal_gbuf_tex : register(t3);

cbuffer LightPosBuffer : register(b0) {
  float4 light_pos;
}

SamplerState gbuf_sampler : register(s0);

float4 main(PSInput input) : SV_TARGET {
  float3 pos = pos_gbuf_tex.Sample(gbuf_sampler, input.texcoord).xyz;
  float3 light_vec = normalize(light_pos.xyz - pos);

  float3 normal = normalize(normal_gbuf_tex.Sample(gbuf_sampler, input.texcoord).xyz);

  float diffuse_coeff = clamp(dot(light_vec, normal), 0.f, 1.f);

  float3 ambient_color = gbuf_tex.Sample(gbuf_sampler, input.texcoord).rgb;
  float3 diffuse_color = diffuse_gbuf_tex.Sample(gbuf_sampler, input.texcoord).rgb;

	return float4(0.3f * ambient_color + diffuse_coeff * diffuse_color, 1.f);
}