#ifndef RAYTRACING_SHADER_H_
#define RAYTRACING_SHADER_H_

struct Viewport {
  float left;
  float top;
  float right;
  float bottom;
};

struct RayGenConstantBuffer {
  Viewport viewport;
  Viewport stencil;
};

#endif  // RAYTRACING_SHADER_H_