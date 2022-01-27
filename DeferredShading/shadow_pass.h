#ifndef SHADOW_PASS_H_
#define SHADOW_PASS_H_

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "d3dx12.h"

#include "constants.h"

class App;

class ShadowPass {
public:
  ShadowPass(App* app) : app_(app) {}
};

#endif  // SHADOW_PASS_H_