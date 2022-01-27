#ifndef SHADOW_PASS_H_
#define SHADOW_PASS_H_

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "d3dx12.h"
#include "DirectXMath.h"

#include "constants.h"

class App;

class ShadowPass {
public:
  ShadowPass(App* app);

  void InitPipeline();
  void CreateBuffersAndUploadData();
  void CreateResourceViews();

  void RenderFrame(ID3D12GraphicsCommandList* command_list);

private:
  friend class App;

  App* app_;

  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;

  CD3DX12_VIEWPORT viewport_;
  CD3DX12_RECT scissor_rect_;

  Microsoft::WRL::ComPtr<ID3D12Resource> matrix_buffer_;
  UINT matrix_buffer_size_ = 0;

  DirectX::XMFLOAT4X4 shadow_mat_;

  CD3DX12_CPU_DESCRIPTOR_HANDLE cbv_cpu_handle_;
  CD3DX12_GPU_DESCRIPTOR_HANDLE cbv_gpu_handle_;

  struct CbvStatic {
    struct Index {
      static constexpr int kMatrixBuffer = 0;
      static constexpr int kMax = kMatrixBuffer;
    };
    static constexpr int kNumDescriptors = Index::kMax + 1;
  };

  struct Frame {
    CD3DX12_CPU_DESCRIPTOR_HANDLE base_dsv_handle;
  };

  Frame frames_[kNumFrames];

  struct DsvPerFrame {
    struct Index {
      static constexpr int kDepthCubemapBase = 0;
      static constexpr int kMax = 5;  // Cubemap takes six faces - index 0 to 5.
    };
    static constexpr int kNumDescriptors = Index::kMax + 1;
  };
};

#endif  // SHADOW_PASS_H_