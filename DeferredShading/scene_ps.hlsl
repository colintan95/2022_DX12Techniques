struct PSOutput {
	float4 out1 : SV_TARGET0;
	float4 out2 : SV_TARGET1;
};

PSOutput main() {
	PSOutput result;

  result.out1 = float4(1.f, 0.f, 0.f, 1.f);
	result.out2 = float4(0.f, 1.f, 0.f, 1.f);

	return result;

	// return float4(1.f, 0.f, 0.f, 1.f);
}