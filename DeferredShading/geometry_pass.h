#ifndef GEOMETRY_PASS_H_
#define GEOMETRY_PASS_H_

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "d3dx12.h"

#include "constants.h"

class App;

class GeometryPass {
public:
  GeometryPass(App* app) : app_(app) {}

  void InitPipeline();
  void CreateBuffersAndUploadData();
  void InitResources();

  void RenderFrame(ID3D12GraphicsCommandList* command_list);

private:
  friend class App;

  App* app_;

  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;

  Microsoft::WRL::ComPtr<ID3D12Resource> matrix_buffer_;
  UINT matrix_buffer_size_ = 0;

  Microsoft::WRL::ComPtr<ID3D12Resource> materials_buffer_;
  UINT materials_buffer_size_ = 0;

  CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle_;

  CD3DX12_CPU_DESCRIPTOR_HANDLE base_cbv_cpu_handle_;
  CD3DX12_GPU_DESCRIPTOR_HANDLE base_cbv_gpu_handle_;

  struct DsvStatic {
    struct Index {
      static constexpr int kDepthBuffer = 0;
      static constexpr int kMax = kDepthBuffer;
    };
    static constexpr int kNumDescriptors = Index::kMax + 1;
  };

  struct CbvStatic {
    struct Index {
      static constexpr int kMatrixBuffer = 0;
      static constexpr int kMaterialsBuffer = 1;
      static constexpr int kMax = kMaterialsBuffer;
    };
    static constexpr int kNumDescriptors = Index::kMax + 1;
  };

  struct Frame {
    CD3DX12_CPU_DESCRIPTOR_HANDLE base_rtv_handle_;
  };

  Frame frames_[kNumFrames];

  struct RtvPerFrame {
    struct Index {
      static constexpr int kAmbientGbufferTexture = 0;
      static constexpr int kPositionGbufferTexture = 1;
      static constexpr int kDiffuseGbufferTexture = 2;
      static constexpr int kNormalGbufferTexture = 3;
      static constexpr int kMax = kNormalGbufferTexture;
    };
    static constexpr int kNumDescriptors = Index::kMax + 1;
  };
};

#endif  // GEOMETRY_PASS_H_