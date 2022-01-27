cbuffer MatrixBuffer : register(b0) {
	float4x4 shadow_mat;
};

float4 main(float3 position : POSITION) : SV_POSITION {
	return mul(float4(position, 1.f), shadow_mat);
}