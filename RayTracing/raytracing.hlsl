#include "raytracing_shader.h"

RaytracingAccelerationStructure Scene : register(t0, space0);

RWTexture2D<float4> RenderTarget : register(u0);

ConstantBuffer<RayGenConstantBuffer> ray_gen_constants : register(b0);

typedef BuiltInTriangleIntersectionAttributes IntersectAttributes;

struct RayPayload {
  float4 color;
};
//
//bool IsInsideViewport(float2 p, Viewport viewport)
//{
//  return (p.x >= viewport.left && p.x <= viewport.right) &&
//         (p.y >= viewport.top && p.y <= viewport.bottom);
//}

[shader("raygeneration")]
void RaygenShader() {
  float2 lerp_values = (float2)DispatchRaysIndex() / (float2)DispatchRaysDimensions();

  float viewport_x =
      lerp(ray_gen_constants.viewport.left, ray_gen_constants.viewport.right, lerp_values.x);
  float viewport_y =
      lerp(ray_gen_constants.viewport.top, ray_gen_constants.viewport.bottom, lerp_values.y);

  float3 ray_dir = float3(viewport_x * 0.414f, viewport_y * 0.414f, 1.f);
  float3 origin = float3(0, 0, 0.f);

  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = ray_dir;
  ray.TMin = 0.001;
  ray.TMax = 10000.0;
  RayPayload payload = { float4(0, 0, 0, 0) };
  TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);

  RenderTarget[DispatchRaysIndex().xy] = payload.color;
}

[shader("closesthit")]
void ClosestHitShader(inout RayPayload payload, IntersectAttributes attr) {
  float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x,
                               attr.barycentrics.y);

  payload.color = float4(barycentrics, 1);
}

[shader("miss")]
void MissShader(inout RayPayload payload) {
  payload.color = float4(0, 0, 0, 1);
}