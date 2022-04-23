#ifndef RAYTRACING_SHADER_H_
#define RAYTRACING_SHADER_H_

struct Viewport {
  float Left;
  float Top;
  float Right;
  float Bottom;
};

struct RayGenConstantBuffer {
  Viewport Viewport;
};

#endif  // RAYTRACING_SHADER_H_