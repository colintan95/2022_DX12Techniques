#include "app.h"

#include <d3d12.h>
#include <D3Dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "d3dx12.h"
#include "dx_utils.h"

using Microsoft::WRL::ComPtr;
using DX::ThrowIfFailed;

namespace {

constexpr int kNumFrames = 3;

}  // namespace

App::App(HWND window_hwnd, int window_width, int window_height)
  : window_hwnd_(window_hwnd),
    window_width_(window_width),
    window_height_(window_height),
    viewport_(0.f, 0.f, static_cast<float>(window_width), static_cast<float>(window_height)),
    scissor_rect_(0, 0, window_width, window_height) {}

void App::Initialize() {
  InitDeviceAndSwapChain();
  InitPipeline();
}

void App::InitDeviceAndSwapChain() {
  UINT factory_flags = 0;

#if defined(_DEBUG)
  ComPtr<ID3D12Debug> debug_controller;

  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
    debug_controller->EnableDebugLayer();

    factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
  }
#endif  // defined(_DEBUG)

  ComPtr<IDXGIFactory4> factory;
  ThrowIfFailed(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory)));

  ComPtr<IDXGIAdapter1> adapter;
  dx_utils::GetHardwareAdapter(factory.Get(), &adapter, D3D_FEATURE_LEVEL_12_1);

  ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device_)));

  DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
  swap_chain_desc.BufferCount = kNumFrames;
  swap_chain_desc.Width = window_width_;
  swap_chain_desc.Height = window_height_;
  swap_chain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swap_chain_desc.SampleDesc.Count = 1;

  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

  ThrowIfFailed(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue_)));

  ComPtr<IDXGISwapChain1> swap_chain;
  ThrowIfFailed(factory->CreateSwapChainForHwnd(command_queue_.Get(), window_hwnd_,
                                                &swap_chain_desc, nullptr, nullptr, &swap_chain));
  ThrowIfFailed(swap_chain.As(&swap_chain_));
}

void App::InitPipeline() {
  D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data;
  feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

  if (FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &feature_data, 
                                          sizeof(feature_data)))) {
    feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
  }

  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
  root_signature_desc.Init_1_1(0, nullptr, 0, nullptr,
                               D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

  ComPtr<ID3DBlob> signature;
  ComPtr<ID3DBlob> error;
  ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&root_signature_desc, 
                                                      feature_data.HighestVersion, &signature,
                                                      &error));
  ThrowIfFailed(device_->CreateRootSignature(0, signature->GetBufferPointer(),
                                             signature->GetBufferSize(), 
                                             IID_PPV_ARGS(&root_signature_)));
}

void App::Cleanup() {

}

void App::RenderFrame() {

}