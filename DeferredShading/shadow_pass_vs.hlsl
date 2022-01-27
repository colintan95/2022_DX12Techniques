cbuffer MatrixBuffer : register(b0) {
	float4x4 shadow_mats[6];
};

cbuffer MatrixIndex : register(b1) {
  uint mat_index;
}

float4 main(float3 position : POSITION) : SV_POSITION {
	return mul(float4(position, 1.f), shadow_mats[mat_index]);
}