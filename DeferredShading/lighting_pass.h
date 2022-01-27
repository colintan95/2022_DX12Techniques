#ifndef LIGHTING_PASS_H_
#define LIGHTING_PASS_H_

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "d3dx12.h"

#include "constants.h"

class App;

class LightingPass {
public:
  LightingPass(App* app) : app_(app) {}

  void InitPipeline();
  void CreateBuffersAndUploadData();
  void InitResources();

  void RenderFrame(ID3D12GraphicsCommandList* command_list);

private:
  friend class App;

  App* app_;

  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;

  Microsoft::WRL::ComPtr<ID3D12Resource> light_pos_buffer_;
  UINT light_pos_buffer_size_ = 0;

  CD3DX12_CPU_DESCRIPTOR_HANDLE cbv_cpu_handle_;
  CD3DX12_GPU_DESCRIPTOR_HANDLE cbv_gpu_handle_;

  CD3DX12_CPU_DESCRIPTOR_HANDLE sampler_cpu_handle_;
  CD3DX12_GPU_DESCRIPTOR_HANDLE sampler_gpu_handle_;

  struct CbvStatic {
    struct Index {
      static constexpr int kLightPosBuffer = 0;
      static constexpr int kMax = kLightPosBuffer;
    };
    static constexpr int kNumDescriptors = Index::kMax + 1;
  };

  struct SamplerStatic {
    struct Index {
      static constexpr int kGBufferSampler = 0;
      static constexpr int kMax = kGBufferSampler;
    };
    static constexpr int kNumDescriptors = Index::kMax + 1;
  };

  struct Frame {
    CD3DX12_CPU_DESCRIPTOR_HANDLE base_rtv_handle_;

    CD3DX12_CPU_DESCRIPTOR_HANDLE base_srv_cpu_handle_;
    CD3DX12_GPU_DESCRIPTOR_HANDLE base_srv_gpu_handle_;
  };

  Frame frames_[kNumFrames];

  struct RtvPerFrame {
    struct Index {
      static constexpr int kSwapChainBuffer = 0;
      static constexpr int kMax = kSwapChainBuffer;
    };
    static constexpr int kNumDescriptors = Index::kMax + 1;
  };

  struct SrvPerFrame {
    struct Index {
      static constexpr int kAmbientGbufferTexture = 0;
      static constexpr int kPositionGbufferTexture = 1;
      static constexpr int kDiffuseGbufferTexture = 2;
      static constexpr int kNormalGbufferTexture = 3;
      static constexpr int kMax = kNormalGbufferTexture;
    };
    static constexpr int kNumDescriptors = Index::kMax + 1;
  };

  Microsoft::WRL::ComPtr<ID3D12Resource> vertex_buffer_;
  D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view_;
};

#endif  // LIGHTING_PASS_H_