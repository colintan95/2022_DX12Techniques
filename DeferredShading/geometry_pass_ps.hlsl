struct Material {
  float4 ambient_color;
  float4 diffuse_color;
};

cbuffer MaterialsBuffer : register(b1) {
  Material materials[16];
};

cbuffer MaterialIndex : register(b2) {
  uint material_index;
}

struct PSInput {
	float4 position : SV_POSITION;
	float3 world_view_pos : POSITION;
};

struct PSOutput {
  float4 color : SV_TARGET0;
  float4 position : SV_TARGET1;
};

PSOutput main(PSInput input) {
  PSOutput result;

  result.color = float4((materials[material_index].ambient_color).rgb, 1.f);
  result.position = float4(input.world_view_pos, 1.f);

  return result;
}