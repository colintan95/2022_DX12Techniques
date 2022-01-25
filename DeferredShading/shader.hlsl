float4 main(float3 position : POSITION) : SV_POSITION {
	return float4(position, 1.f);
}

float4 PSMain() : SV_TARGET {
	return float4(1.f, 0.f, 0.f, 1.f);
}