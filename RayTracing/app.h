#ifndef APP_H_
#define APP_H_

#include "dx_includes.h"

class App {
public:
  App(HWND hwnd);

  void Initialize();

  void Cleanup();

  void RenderFrame();

private:
  void InitDeviceAndSwapChain();

  HWND hwnd_;

  int frame_index_ = 0;

   D3D_ROOT_SIGNATURE_VERSION root_signature_version_;

   Microsoft::WRL::ComPtr<ID3D12Device> device_;
   Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue_;
   Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain_;

   Microsoft::WRL::ComPtr<ID3D12Device5> dxr_device_;

   Microsoft::WRL::ComPtr<ID3D12RootSignature> global_root_signature_;
   Microsoft::WRL::ComPtr<ID3D12RootSignature> local_root_signature_;

   Microsoft::WRL::ComPtr<ID3D12StateObject> dxr_state_object_;
};

#endif  // APP_H_