#ifndef APP_H_
#define APP_H_

#include "dx_includes.h"

#include <vector>

class App {
public:
  App(HWND hwnd);

  void Initialize();

  void Cleanup();

  void RenderFrame();

private:
  void InitDeviceAndSwapChain();

  void UploadDataToBuffer(const void* data, UINT64 data_size, ID3D12Resource* dst_buffer);

  void WaitForGpu();

  HWND hwnd_;

  int frame_index_ = 0;

   D3D_ROOT_SIGNATURE_VERSION root_signature_version_;

   Microsoft::WRL::ComPtr<ID3D12Device> device_;
   Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue_;
   Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain_;

   Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator_;
   Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;

   Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    UINT64 latest_fence_value_ = 0;
    HANDLE fence_event_;

   Microsoft::WRL::ComPtr<ID3D12Device5> dxr_device_;

   Microsoft::WRL::ComPtr<ID3D12RootSignature> global_root_signature_;
   Microsoft::WRL::ComPtr<ID3D12RootSignature> local_root_signature_;

   Microsoft::WRL::ComPtr<ID3D12StateObject> dxr_state_object_;

   Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptor_heap_;

   Microsoft::WRL::ComPtr<ID3D12Resource> vertex_buffer_;
   Microsoft::WRL::ComPtr<ID3D12Resource> index_buffer_;

   std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> upload_buffers_;

   Microsoft::WRL::ComPtr<ID3D12Resource> bottom_level_acceleration_structure_;
   Microsoft::WRL::ComPtr<ID3D12Resource> top_level_acceleration_structure_;
};

#endif  // APP_H_