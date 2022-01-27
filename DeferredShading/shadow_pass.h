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

  void InitPipeline();
  void CreateBuffersAndUploadData();

private:
  friend class App;

  App* app_;

  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;

  Microsoft::WRL::ComPtr<ID3D12Resource> matrix_buffer_;
  UINT matrix_buffer_size_ = 0;
};

#endif  // SHADOW_PASS_H_