cbuffer SceneConstantBuffer : register(b0) {
	float4x4 world_view_proj_mat;
};

float4 main(float3 position : POSITION) : SV_POSITION {
	return mul(float4(position, 1.f), world_view_proj_mat);
}