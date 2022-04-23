#include "raytracing_shader.h"

RaytracingAccelerationStructure Scene : register(t0, space0);

RWTexture2D<float4> RenderTarget : register(u0);

ConstantBuffer<RayGenConstantBuffer> ray_gen_constants : register(b0);

struct Material {
  float4 ambient_color;
  float4 diffuse_color;
};

cbuffer Materials : register(b1) {
  Material materials[32];
};

cbuffer MaterialIndex : register(b2) {
  uint material_index;
  uint base_ib_index;
};

struct Vertex {
  float3 position;
  float3 normal;
};

ByteAddressBuffer index_buffer : register(t1);
StructuredBuffer<Vertex> vertex_buffer : register(t2);

typedef BuiltInTriangleIntersectionAttributes IntersectAttributes;

struct RayPayload {
  float4 color;
  float is_shadow;
  float shadow_hit_dist;
};

[shader("raygeneration")]
void RaygenShader() {
  float2 lerp_values = (float2)DispatchRaysIndex() / (float2)DispatchRaysDimensions();

  float viewport_x =
      lerp(ray_gen_constants.Viewport.left, ray_gen_constants.Viewport.right, lerp_values.x);
  float viewport_y =
      lerp(ray_gen_constants.Viewport.top, ray_gen_constants.Viewport.bottom, lerp_values.y);

  float3 ray_dir = float3(viewport_x * 0.414f, viewport_y * 0.414f, -1.f);
  float3 origin = float3(0, 1, 4.f);

  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = ray_dir;
  ray.TMin = 0.001;
  ray.TMax = 10000.0;
  RayPayload payload = { float4(0, 0, 0, 0), 0, 0 };
  TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);

  RenderTarget[DispatchRaysIndex().xy] = payload.color;
}

// Taken from Microsoft sample.
uint3 Load3x16BitIndices(uint offsetBytes)
{
  uint3 indices;

  // ByteAdressBuffer loads must be aligned at a 4 byte boundary.
  // Since we need to read three 16 bit indices: { 0, 1, 2 }
  // aligned at a 4 byte boundary as: { 0 1 } { 2 0 } { 1 2 } { 0 1 } ...
  // we will load 8 bytes (~ 4 indices { a b | c d }) to handle two possible index triplet layouts,
  // based on first index's offsetBytes being aligned at the 4 byte boundary or not:
  //  Aligned:     { 0 1 | 2 - }
  //  Not aligned: { - 0 | 1 2 }
  const uint dwordAlignedOffset = offsetBytes & ~3;
  const uint2 four16BitIndices = index_buffer.Load2(dwordAlignedOffset);

  // Aligned: { 0 1 | 2 - } => retrieve first three 16bit indices
  if (dwordAlignedOffset == offsetBytes)
  {
    indices.x = four16BitIndices.x & 0xffff;
    indices.y = (four16BitIndices.x >> 16) & 0xffff;
    indices.z = four16BitIndices.y & 0xffff;
  }
  else // Not aligned: { - 0 | 1 2 } => retrieve last three 16bit indices
  {
    indices.x = (four16BitIndices.x >> 16) & 0xffff;
    indices.y = four16BitIndices.y & 0xffff;
    indices.z = (four16BitIndices.y >> 16) & 0xffff;
  }

  return indices;
}

// Taken from Microsoft sample.
float3 HitAttribute(float3 vertexAttribute[3], IntersectAttributes attr)
{
  return vertexAttribute[0] +
    attr.barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]) +
    attr.barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
}

[shader("closesthit")]
void ClosestHitShader(inout RayPayload payload, IntersectAttributes attr) {
  if (payload.is_shadow > 0.5) {
    payload.shadow_hit_dist = length(RayTCurrent() * WorldRayDirection());
  } else {
    float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x,
      attr.barycentrics.y);

    // Stride of indices in triangle is index size in bytes * indices per triangle => 2 * 3 = 6.
    uint ib_index_bytes = PrimitiveIndex() * 6 + base_ib_index * 2;

    const uint3 indices = Load3x16BitIndices(ib_index_bytes);

    float3 normals[3] = {
      vertex_buffer[indices[0]].normal,
      vertex_buffer[indices[1]].normal,
      vertex_buffer[indices[2]].normal
    };

    float3 normal = normalize(HitAttribute(normals, attr));

    float3 hit_pos = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();

    float3 light_pos = float3(0.0, 1.9, 0.0);
    float3 light_dist_vec = light_pos - hit_pos;
    float3 light_dir = normalize(light_dist_vec);

    Material mtl = materials[material_index];

    float3 ambient = mtl.ambient_color.rgb;

    float diffuse_coeff = clamp(dot(light_dir, normal), 0.0, 1.0);
    float3 diffuse = diffuse_coeff * mtl.diffuse_color.rgb;

    RayDesc shadow_ray;
    shadow_ray.Origin = hit_pos;
    shadow_ray.Direction = light_dir;
    shadow_ray.TMin = 0.001;
    shadow_ray.TMax = 10000.0;
    RayPayload shadow_payload = { float4(0, 0, 0, 0), 1, 50.0 };
    TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, shadow_ray, shadow_payload);

    float is_illuminated = shadow_payload.shadow_hit_dist > length(light_dist_vec) ? 1.0 : 0.0;

    payload.color = float4(0.3 * ambient + is_illuminated * diffuse, 1);
  }
}

[shader("miss")]
void MissShader(inout RayPayload payload) {
  payload.color = float4(0, 0, 0, 1);
}