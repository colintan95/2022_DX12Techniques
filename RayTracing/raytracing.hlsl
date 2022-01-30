#include "raytracing_shader.h"

RaytracingAccelerationStructure Scene : register(t0, space0);

ConstantBuffer<RayGenConstantBuffer> ray_gen_constants : register(b0);
RWTexture2D<float4> RenderTarget : register(u0);
typedef BuiltInTriangleIntersectionAttributes IntersectAttributes;

struct RayPayload {
  float4 color;
};

bool IsInsideViewport(float2 p, Viewport viewport)
{
    return (p.x >= viewport.left && p.x <= viewport.right)
        && (p.y >= viewport.top && p.y <= viewport.bottom);
}

[shader("raygeneration")]
void RaygenShader() {
  float2 lerpValues = (float2)DispatchRaysIndex() / (float2)DispatchRaysDimensions();

  // Orthographic projection since we're raytracing in screen space.
  float3 rayDir = float3(0, 0, 1);
  float3 origin = float3(
      lerp(ray_gen_constants.viewport.left, ray_gen_constants.viewport.right, lerpValues.x),
      lerp(ray_gen_constants.viewport.top, ray_gen_constants.viewport.bottom, lerpValues.y),
      0.0f);

  if (IsInsideViewport(origin.xy, ray_gen_constants.stencil))
  {
      // Trace the ray.
      // Set the ray's extents.
      RayDesc ray;
      ray.Origin = origin;
      ray.Direction = rayDir;
      // Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
      // TMin should be kept small to prevent missing geometry at close contact areas.
      ray.TMin = 0.001;
      ray.TMax = 10000.0;
      RayPayload payload = { float4(0, 0, 0, 0) };
      TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);

      // Write the raytraced color to the output texture.
      RenderTarget[DispatchRaysIndex().xy] = payload.color;
  }
  else
  {
      // Render interpolated DispatchRaysIndex outside the stencil window
      RenderTarget[DispatchRaysIndex().xy] = float4(lerpValues, 0, 1);
  }
}

[shader("closesthit")]
void ClosestHitShader(inout RayPayload payload, IntersectAttributes attr) {
  float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
  payload.color = float4(barycentrics, 1);
}

[shader("miss")]
void MissShader(inout RayPayload payload) {
  payload.color = float4(0, 0, 0, 1);
}