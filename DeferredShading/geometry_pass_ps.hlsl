struct Material {
  float4 ambient_color;
  float4 diffuse_color;
};

// TODO: Deal with constant register limit. Setting the number of elements higher (e.g. to 16)
// causes the shader to exceed the register limit.
cbuffer MaterialsBuffer : register(b1) {
  Material materials[12];
};

cbuffer MaterialIndex : register(b2) {
  uint material_index;
}

struct PSInput {
	float4 position : SV_POSITION;
	float3 world_pos : POSITION;
  float3 normal : NORMAL;
};

struct PSOutput {
  float4 color : SV_TARGET0;
  float4 position : SV_TARGET1;
  float4 diffuse : SV_TARGET2;
  float4 normal : SV_TARGET3;
};

PSOutput main(PSInput input) {
  PSOutput result;

  result.color = float4((materials[material_index].ambient_color).rgb, 1.f);
  result.position = float4(input.world_pos, 1.f);
  result.diffuse = float4((materials[material_index].diffuse_color).rgb, 1.f);
  result.normal = float4(input.normal, 0.f);

  return result;
}