typedef BuiltInTriangleIntersectionAttributes IntersectAttributes;

struct RayPayload {
  float4 color;
};


[shader("raygeneration")]
void RaygenShader() {

}

[shader("closesthit")]
void ClosestHitShader(inout RayPayload payload, IntersectAttributes attr) {

}

[shader("miss")]
void MissShader(inout RayPayload payload) {

}