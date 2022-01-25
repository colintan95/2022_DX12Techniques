#ifndef APP_H_
#define APP_H_

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "d3dx12.h"

constexpr int kNumFrames = 3;

class App {
public:
  App(HWND window_hwnd, int window_width, int window_height);

  void Initialize();

  void Cleanup();

  void RenderFrame();

private:
  void InitDeviceAndSwapChain();

  void InitPipelines();
  void InitGeometryPassPipeline();
  void InitLightingPassPipeline();

  void InitCommandAllocators();
  void InitFence();

  void InitResources();

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

  struct Pass {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
  };

  Pass geometry_pass_;
  Pass lighting_pass_;

  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;

  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  UINT rtv_descriptor_size_ = 0;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsv_heap_;
  UINT dsv_descriptor_size_ = 0;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srv_heap_;
  UINT srv_descriptor_size_ = 0;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sampler_heap_;
  UINT sampler_descriptor_size_ = 0;

  Microsoft::WRL::ComPtr<ID3D12Resource> depth_stencil_;

  Microsoft::WRL::ComPtr<ID3D12Resource> vertex_buffer_;
  D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view_;

  Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
  UINT64 latest_fence_value_ = 0;
  HANDLE fence_event_;

  CD3DX12_CPU_DESCRIPTOR_HANDLE geometry_pass_base_rtv_;
  CD3DX12_CPU_DESCRIPTOR_HANDLE lighting_pass_base_rtv_;

  struct Frame {
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator;
    Microsoft::WRL::ComPtr<ID3D12Resource> render_target;

    Microsoft::WRL::ComPtr<ID3D12Resource> geometry_pass_render_target;

    UINT64 fence_value = 0;
  };

  Frame frames_[kNumFrames];
};

#endif  // APP_H_