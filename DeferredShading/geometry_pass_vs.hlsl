struct Matrices {
	float4x4 world_view;
	float4x4 world_view_proj;
};

ConstantBuffer<Matrices> matrices : register(b0);

struct PSInput {
	float4 position : SV_POSITION;
	float3 view_pos : POSITION;
	float3 normal : NORMAL;
};

PSInput main(float3 position : POSITION, float3 normal : NORMAL) {
	PSInput result;

	result.position = mul(float4(position, 1.f), matrices.world_view_proj);
	result.view_pos = mul(float4(position, 1.f), matrices.world_view).xyz;
	result.normal = mul(float4(normal, 0.f), matrices.world_view).xyz;

	return result;
}