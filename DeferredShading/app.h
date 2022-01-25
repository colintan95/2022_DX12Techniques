#ifndef APP_H_
#define APP_H_

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "d3dx12.h"

class App {
public:
  App(HWND window_hwnd, int window_width, int window_height);

  void Initialize();

  void Cleanup();

  void RenderFrame();

private:
  void InitDeviceAndSwapChain();
  void InitPipeline();

  HWND window_hwnd_;
  int window_width_;
  int window_height_;

  CD3DX12_VIEWPORT viewport_;
  CD3DX12_RECT scissor_rect_;

  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue_;
  Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain_;

  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_state_;
};

#endif  // APP_H_