#ifndef APP_H_
#define APP_H_

#include <vector>

#include "constants.h"
#include "dx_includes.h"
#include "raytracing_shader.h"

class App {
public:
  App(HWND hwnd);

  void Initialize();

  void Cleanup();

  void RenderFrame();

private:
  void InitDeviceAndSwapChain();

  void CreateCommandObjects();

  void CreatePipeline();

  void CreateDescriptorHeap();

  void InitData();

  void CreateBuffersAndViews();

  void CreateShaderTables();

  void CreateAccelerationStructure();

  void MoveToNextFrame();

  void WaitForGpu();

  HWND hwnd_;

  int frame_index_ = 0;

   D3D_ROOT_SIGNATURE_VERSION root_signature_version_;

   Microsoft::WRL::ComPtr<ID3D12Device> device_;
   Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue_;
   Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain_;

   Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;

   Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
   UINT64 latest_fence_value_ = 0;
   HANDLE fence_event_;

   Microsoft::WRL::ComPtr<ID3D12Device5> dxr_device_;

   Microsoft::WRL::ComPtr<ID3D12RootSignature> global_root_signature_;
   Microsoft::WRL::ComPtr<ID3D12RootSignature> local_root_signature_;

   Microsoft::WRL::ComPtr<ID3D12StateObject> dxr_state_object_;

   Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptor_heap_;
   D3D12_CPU_DESCRIPTOR_HANDLE uav_cpu_handle_;
   D3D12_GPU_DESCRIPTOR_HANDLE uav_gpu_handle_;

   Microsoft::WRL::ComPtr<ID3D12Resource> matrix_buffer_;
   UINT matrix_buffer_size_ = 0;

   Microsoft::WRL::ComPtr<ID3D12Resource> raytracing_output_;

   Microsoft::WRL::ComPtr<ID3D12Resource> ray_gen_shader_table_;
   Microsoft::WRL::ComPtr<ID3D12Resource> hit_group_shader_table_;
   Microsoft::WRL::ComPtr<ID3D12Resource> miss_shader_table_;

   UINT hit_group_shader_record_size_ = 0;

   Microsoft::WRL::ComPtr<ID3D12Resource> bottom_level_acceleration_structure_;
   Microsoft::WRL::ComPtr<ID3D12Resource> top_level_acceleration_structure_;

   Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> dxr_command_list_;

   struct Frame {
     Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator;

     Microsoft::WRL::ComPtr<ID3D12Resource> swap_chain_buffer;

     UINT64 fence_value = 0;
   };

   Frame frames_[kNumFrames];

   RayGenConstantBuffer ray_gen_constants_;

   float camera_yaw_ = 0.f;
   float camera_pitch_ = 0.f;
   float camera_roll_ = 0.f;

   DirectX::XMFLOAT3X4 world_view_mat_;

   std::unique_ptr<DirectX::GraphicsMemory> graphics_memory_;
   std::unique_ptr<DirectX::Model> model_;
};

#endif  // APP_H_