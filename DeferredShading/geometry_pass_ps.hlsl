struct Material {
  float4 ambient_color;
  float4 diffuse_color;
};

struct Materials {
  Material materials[32];
};

ConstantBuffer<Materials> materials : register(b1);

struct MaterialIndex {
  uint index;
};

ConstantBuffer<uint> material_index : register(b2);

struct PSInput {
	float4 position : SV_POSITION;
	float3 view_pos : POSITION;
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

  Material material = materials.materials[material_index.index];

  result.color = float4((material.ambient_color).rgb, 1.f);
  result.position = float4(input.view_pos, 1.f);
  result.diffuse = float4((material.diffuse_color).rgb, 1.f);
  result.normal = float4(input.normal, 0.f);

  return result;
}