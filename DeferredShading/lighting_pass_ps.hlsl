struct PSInput {
  float4 position : SV_POSITION;
  float2 texcoord : TEXCOORD;
};

Texture2D gbuf_tex : register(t0);
Texture2D pos_gbuf_tex : register(t1);
Texture2D diffuse_gbuf_tex : register(t2);
Texture2D normal_gbuf_tex : register(t3);

TextureCube shadow_cubemap_tex : register(t4);

cbuffer LightPosBuffer : register(b0) {
  float4 light_view_pos;
}

SamplerState gbuf_sampler : register(s0);
SamplerState shadow_cubemap_sampler : register(s1);

float4 main(PSInput input) : SV_TARGET {
  float3 view_pos = pos_gbuf_tex.Sample(gbuf_sampler, input.texcoord).xyz;
   float3 light_vec = light_view_pos.xyz - view_pos;

   float3 normal = normalize(normal_gbuf_tex.Sample(gbuf_sampler, input.texcoord).xyz);

  float diffuse_coeff = clamp(dot(normalize(light_vec), normal), 0.f, 1.f);

  float3 ambient_color = gbuf_tex.Sample(gbuf_sampler, input.texcoord).rgb;
  float3 diffuse_color = diffuse_gbuf_tex.Sample(gbuf_sampler, input.texcoord).rgb;

  float near = 0.05f;
  float far = 10.f;

  float max_component = max(max(abs(light_vec.x), abs(light_vec.y)), abs(light_vec.z));
  float depth = (far / (far - near)) - (far * near / (far - near)) / max_component;
  depth = clamp(depth, 0.f, 1.f);

  // Increases the bias as the cubemap coord gets closer to the edges of the cube and as the depth
  // increases.
  float depth_bias = (1.f - depth) * (0.1f + (1.f - max_component) * 0.1f);
  depth = clamp(depth - depth_bias, 0.f, 1.f);

  float3 cubemap_coord = normalize(view_pos - light_view_pos.xyz);

  float shadow_tex_depth = shadow_cubemap_tex.Sample(shadow_cubemap_sampler, cubemap_coord).r;

  float illuminated = clamp(sign(shadow_tex_depth - depth), 0.f, 1.f);

	return float4(0.3f * ambient_color + illuminated * diffuse_coeff * diffuse_color, 1.f);
}