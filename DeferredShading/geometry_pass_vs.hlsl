cbuffer SceneConstantBuffer : register(b0) {
	float4x4 world_view_mat;
	float4x4 world_view_proj_mat;
};

struct PSInput {
	float4 position : SV_POSITION;
	float3 world_view_pos : POSITION;
};

PSInput main(float3 position : POSITION) {
	PSInput result;

	result.position = mul(float4(position, 1.f), world_view_proj_mat);
	result.world_view_pos = mul(float4(position, 1.f), world_view_mat);

	return result;
}