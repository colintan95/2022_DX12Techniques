#ifndef APP_H_
#define APP_H_

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <memory>
#include <vector>

#include "d3dx12.h"
#include "DirectXMath.h"

// TODO: See if this can be removed. Currently needed so that "Model.h" doesn't error out.
#include <stdexcept>

#include "GraphicsMemory.h"
#include "Model.h"

constexpr int kNumFrames = 3;

class App {
public:
  App(HWND window_hwnd, int window_width, int window_height);

  void Initialize();

  void Cleanup();

  void RenderFrame();

private:
  void InitDeviceAndSwapChain();

  void InitCommandAllocators();
  void InitFence();

  void InitPipelines();

  void InitDescriptorHeapsAndHandles();
  void InitResources();

  void UploadDataToBuffer(const void* data, UINT64 data_size, ID3D12Resource* dst_buffer);

  void MoveToNextFrame();

  void WaitForGpu();

  D3D_ROOT_SIGNATURE_VERSION root_signature_version_;

  HWND window_hwnd_;
  int window_width_;
  int window_height_;

  int frame_index_ = 0;

  CD3DX12_VIEWPORT viewport_;
  CD3DX12_RECT scissor_rect_;

  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue_;
  Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain_;

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

    struct DrawCallArgs {
      D3D12_PRIMITIVE_TOPOLOGY primitive_type;
      D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view;
      D3D12_INDEX_BUFFER_VIEW index_buffer_view;

      uint32_t index_count;
      uint32_t start_index;
      int32_t vertex_offset;

      uint32_t material_index;
    };

    std::vector<DrawCallArgs> draw_call_args_;
  };

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

  GeometryPass geometry_pass_;

  LightingPass lighting_pass_;

  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;

  Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
  UINT64 latest_fence_value_ = 0;
  HANDLE fence_event_;

  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  UINT rtv_descriptor_size_ = 0;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsv_heap_;
  UINT dsv_descriptor_size_ = 0;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> cbv_srv_heap_;
  UINT cbv_srv_descriptor_size_ = 0;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sampler_heap_;
  UINT sampler_descriptor_size_ = 0;

  Microsoft::WRL::ComPtr<ID3D12Resource> depth_stencil_;

  std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> upload_buffers_;

  struct Frame {
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator;

    Microsoft::WRL::ComPtr<ID3D12Resource> swap_chain_buffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> gbuffer;

    Microsoft::WRL::ComPtr<ID3D12Resource> pos_gbuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> diffuse_gbuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> normal_gbuffer;

    UINT64 fence_value = 0;
  };

  Frame frames_[kNumFrames];

  std::unique_ptr<DirectX::GraphicsMemory> graphics_memory_;
  std::unique_ptr<DirectX::Model> model_;

  DirectX::XMFLOAT4X4 view_mat_;
  DirectX::XMFLOAT4X4 world_view_mat_;
  DirectX::XMFLOAT4X4 world_view_proj_mat_;

  struct Material {
    DirectX::XMFLOAT4 ambient_color;
    DirectX::XMFLOAT4 diffuse_color;
  };

  std::vector<Material> materials_;

  DirectX::XMFLOAT4 light_camera_pos_;
};

#endif  // APP_H_