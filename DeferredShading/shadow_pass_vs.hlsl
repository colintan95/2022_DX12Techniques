struct Matrices {
	float4x4 world_view;
	float4x4 shadow[6];
};

ConstantBuffer<Matrices> matrices : register(b0);

struct MatrixIndex {
	uint index;
};

ConstantBuffer<MatrixIndex> matrix_index : register(b1);

float4 main(float3 position : POSITION) : SV_POSITION {
	return mul(mul(float4(position, 1.f), matrices.world_view), matrices.shadow[matrix_index.index]);
}