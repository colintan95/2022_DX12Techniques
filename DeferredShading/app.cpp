#include "app.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstring>
#include <vector>

#include "d3dx12.h"
#include "DirectXMath.h"

#include "Model.h"
#include "ResourceUploadBatch.h"

#include "dx_utils.h"
#include "ReadData.h"

using Microsoft::WRL::ComPtr;
using DX::ThrowIfFailed;

App::App(HWND window_hwnd, int window_width, int window_height)
  : window_hwnd_(window_hwnd),
    window_width_(window_width),
    window_height_(window_height),
    viewport_(0.f, 0.f, static_cast<float>(window_width), static_cast<float>(window_height)),
    scissor_rect_(0, 0, window_width, window_height),
    geometry_pass_(this),
    lighting_pass_(this) {}

void App::Initialize() {
  InitDeviceAndSwapChain();
  InitCommandAllocators();
  InitFence();
  InitPipelines();
  InitDescriptorHeapsAndHandles();
  InitResources();
}

void App::InitDeviceAndSwapChain() {
  UINT factory_flags = 0;

#if defined(_DEBUG)
  ComPtr<ID3D12Debug> debug_controller;
  ComPtr<ID3D12Debug1> debug_controller1;

  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
    debug_controller->EnableDebugLayer();

    debug_controller.As(&debug_controller1);
    debug_controller1->SetEnableGPUBasedValidation(true);

    factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
  }
#endif  // defined(_DEBUG)

  ComPtr<IDXGIFactory4> factory;
  ThrowIfFailed(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory)));

  ComPtr<IDXGIAdapter1> adapter;
  dx_utils::GetHardwareAdapter(factory.Get(), &adapter, D3D_FEATURE_LEVEL_12_1);

  ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device_)));

  DXGI_SWAP_CHAIN_DESC1 swap_chain_desc{};
  swap_chain_desc.BufferCount = kNumFrames;
  swap_chain_desc.Width = window_width_;
  swap_chain_desc.Height = window_height_;
  swap_chain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swap_chain_desc.SampleDesc.Count = 1;

  D3D12_COMMAND_QUEUE_DESC queue_desc{};
  queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

  ThrowIfFailed(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue_)));

  ComPtr<IDXGISwapChain1> swap_chain;
  ThrowIfFailed(factory->CreateSwapChainForHwnd(command_queue_.Get(), window_hwnd_,
                                                &swap_chain_desc, nullptr, nullptr, &swap_chain));
  ThrowIfFailed(swap_chain.As(&swap_chain_));
}

void App::InitCommandAllocators() {
  for (int i = 0; i < kNumFrames; ++i) {
    ThrowIfFailed(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  IID_PPV_ARGS(&frames_[i].command_allocator)));
  }

  ThrowIfFailed(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           frames_[frame_index_].command_allocator.Get(),
                                           nullptr, IID_PPV_ARGS(&command_list_)));
  ThrowIfFailed(command_list_->Close());
}

void App::InitFence() {
  ThrowIfFailed(device_->CreateFence(latest_fence_value_, D3D12_FENCE_FLAG_NONE,
                                     IID_PPV_ARGS(&fence_)));
  ++latest_fence_value_;

  fence_event_ = CreateEvent(nullptr, false, false, nullptr);
  if (fence_event_ == nullptr) {
    ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
  }
}

void App::InitPipelines() {
  D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data;
  feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

  if (FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &feature_data,
                                          sizeof(feature_data)))) {
    feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
  }

  root_signature_version_ = feature_data.HighestVersion;

  geometry_pass_.InitPipeline();

  lighting_pass_.InitPipeline();
}

void App::InitDescriptorHeapsAndHandles() {
  {
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
    rtv_heap_desc.NumDescriptors =
        GeometryPass::RtvPerFrame::kNumDescriptors * kNumFrames +
        LightingPass::RtvPerFrame::kNumDescriptors * kNumFrames;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device_->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap_)));

    rtv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_heap_->GetCPUDescriptorHandleForHeapStart());

    for (int i = 0; i < kNumFrames; ++i) {
      geometry_pass_.frames_[i].base_rtv_handle_ = rtv_handle;

      rtv_handle.Offset(GeometryPass::RtvPerFrame::kNumDescriptors, rtv_descriptor_size_);
    }

    for (int i = 0; i < kNumFrames; ++i) {
      lighting_pass_.frames_[i].base_rtv_handle_ = rtv_handle;

      rtv_handle.Offset(LightingPass::RtvPerFrame::kNumDescriptors, rtv_descriptor_size_);
    }
  }

  {
    D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc{};
    dsv_heap_desc.NumDescriptors = GeometryPass::DsvStatic::kNumDescriptors;
    dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device_->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&dsv_heap_)));

    dsv_descriptor_size_ =
        device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    geometry_pass_.dsv_handle_ = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
  }

  {
    D3D12_DESCRIPTOR_HEAP_DESC cbv_srv_heap_desc{};
    cbv_srv_heap_desc.NumDescriptors =
        GeometryPass::CbvStatic::kNumDescriptors +
        LightingPass::CbvStatic::kNumDescriptors +
        LightingPass::SrvPerFrame::kNumDescriptors * kNumFrames;
    cbv_srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbv_srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device_->CreateDescriptorHeap(&cbv_srv_heap_desc, IID_PPV_ARGS(&cbv_srv_heap_)));

    cbv_srv_descriptor_size_ =
         device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE cbv_srv_cpu_handle(
        cbv_srv_heap_->GetCPUDescriptorHandleForHeapStart());
    CD3DX12_GPU_DESCRIPTOR_HANDLE cbv_srv_gpu_handle(
        cbv_srv_heap_->GetGPUDescriptorHandleForHeapStart());

    geometry_pass_.base_cbv_cpu_handle_ = cbv_srv_cpu_handle;
    geometry_pass_.base_cbv_gpu_handle_ = cbv_srv_gpu_handle;

    cbv_srv_cpu_handle.Offset(GeometryPass::CbvStatic::kNumDescriptors, cbv_srv_descriptor_size_);
    cbv_srv_gpu_handle.Offset(GeometryPass::CbvStatic::kNumDescriptors, cbv_srv_descriptor_size_);

    for (int i = 0; i < kNumFrames; ++i) {
      lighting_pass_.frames_[i].base_srv_cpu_handle_ = cbv_srv_cpu_handle;
      lighting_pass_.frames_[i].base_srv_gpu_handle_ = cbv_srv_gpu_handle;

      cbv_srv_cpu_handle.Offset(LightingPass::SrvPerFrame::kNumDescriptors,
                                cbv_srv_descriptor_size_);
      cbv_srv_gpu_handle.Offset(LightingPass::SrvPerFrame::kNumDescriptors,
                                cbv_srv_descriptor_size_);
    }

    lighting_pass_.cbv_cpu_handle_ = cbv_srv_cpu_handle;
    lighting_pass_.cbv_gpu_handle_ = cbv_srv_gpu_handle;
  }

  {
    D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc{};
    sampler_heap_desc.NumDescriptors = LightingPass::SamplerStatic::kNumDescriptors;
    sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device_->CreateDescriptorHeap(&sampler_heap_desc, IID_PPV_ARGS(&sampler_heap_)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE sampler_cpu_handle(
        sampler_heap_->GetCPUDescriptorHandleForHeapStart());
    CD3DX12_GPU_DESCRIPTOR_HANDLE sampler_gpu_handle(
        sampler_heap_->GetGPUDescriptorHandleForHeapStart());

    sampler_cpu_handle.Offset(LightingPass::SamplerStatic::Index::kGBufferSampler,
                              sampler_descriptor_size_);
    sampler_gpu_handle.Offset(LightingPass::SamplerStatic::Index::kGBufferSampler,
                              sampler_descriptor_size_);

    lighting_pass_.sampler_cpu_handle_ = sampler_cpu_handle;
    lighting_pass_.sampler_gpu_handle_ = sampler_gpu_handle;
  }
}

void App::InitResources() {
  for (int i = 0; i < kNumFrames; ++i) {
    ThrowIfFailed(swap_chain_->GetBuffer(i, IID_PPV_ARGS(&frames_[i].swap_chain_buffer)));
  }

  D3D12_CLEAR_VALUE clear_color{};
  clear_color.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  clear_color.Color[0] = 0.f;
  clear_color.Color[1] = 0.f;
  clear_color.Color[2] = 0.f;
  clear_color.Color[3] = 1.f;

  for (int i = 0; i < kNumFrames; ++i) {
    CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC resource_desc =
        CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, window_width_, window_height_, 1,
                                     1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

    ThrowIfFailed(device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                                                   &resource_desc,
                                                   D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_color,
                                                   IID_PPV_ARGS(&frames_[i].gbuffer)));

    ThrowIfFailed(device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                                                   &resource_desc,
                                                   D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_color,
                                                   IID_PPV_ARGS(&frames_[i].diffuse_gbuffer)));
  }

  D3D12_CLEAR_VALUE clear_pos{};
  clear_pos.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  clear_pos.Color[0] = 0.f;
  clear_pos.Color[1] = 0.f;
  clear_pos.Color[2] = 0.f;
  clear_pos.Color[3] = 1.f;

  for (int i = 0; i < kNumFrames; ++i) {
    CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC resource_desc =
        CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT, window_width_, window_height_,
                                     1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

    ThrowIfFailed(device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                                                   &resource_desc,
                                                   D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_pos,
                                                   IID_PPV_ARGS(&frames_[i].pos_gbuffer)));

    ThrowIfFailed(device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                                                   &resource_desc,
                                                   D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_pos,
                                                   IID_PPV_ARGS(&frames_[i].normal_gbuffer)));
  }

  {
    D3D12_CLEAR_VALUE clear_value{};
    clear_value.Format = DXGI_FORMAT_D32_FLOAT;
    clear_value.DepthStencil.Depth = 1.0f;
    clear_value.DepthStencil.Stencil = 0;

    CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC resource_desc =
        CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, window_width_, window_height_, 1, 0, 1,
                                     0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    ThrowIfFailed(device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                                                   &resource_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                                   &clear_value, IID_PPV_ARGS(&depth_stencil_)));
  }

  graphics_memory_ = std::make_unique<DirectX::GraphicsMemory>(device_.Get());
  model_ = DirectX::Model::CreateFromSDKMESH(device_.Get(), L"cornell_box.sdkmesh");

  for (const auto& effect_info : model_->materials) {
    Material material{};
    material.ambient_color = DirectX::XMFLOAT4(effect_info.ambientColor.x,
                                               effect_info.ambientColor.y,
                                               effect_info.ambientColor.z, 0.f);
    material.diffuse_color = DirectX::XMFLOAT4(effect_info.diffuseColor.x,
                                               effect_info.diffuseColor.y,
                                               effect_info.diffuseColor.z, 0.f);

    materials_.push_back(material);
  }

  DirectX::XMMATRIX world_mat = DirectX::XMMatrixIdentity();
  DirectX::XMMATRIX view_mat =
      DirectX::XMMatrixTranslation(0.f, -1.f, -4.f) * DirectX::XMMatrixRotationY(DirectX::XM_PI);
  DirectX::XMMATRIX proj_mat =
      DirectX::XMMatrixPerspectiveFovLH(
          45.f, static_cast<float>(window_width_) / static_cast<float>(window_height_), 0.1f,
          1000.f);

  DirectX::XMStoreFloat4x4(&view_mat_, DirectX::XMMatrixTranspose(view_mat));

  DirectX::XMStoreFloat4x4(&world_view_mat_, DirectX::XMMatrixTranspose(world_mat * view_mat));

  // DirectXMath stores the matrix in row-major order while hlsl needs the matrix to be stored in
  // column-major order. So, we apply a transpose when storing the matrix in the buffer.
  DirectX::XMStoreFloat4x4(&world_view_proj_mat_,
                           DirectX::XMMatrixTranspose(world_mat * view_mat * proj_mat));

  DirectX::XMVECTOR light_camera_pos =
      DirectX::XMVector4Transform(DirectX::XMVectorSet(0.f, 1.9f, 0.f, 1.f), view_mat);

  DirectX::XMStoreFloat4(&light_camera_pos_ , light_camera_pos);

  ThrowIfFailed(frames_[frame_index_].command_allocator->Reset());
  ThrowIfFailed(command_list_->Reset(frames_[frame_index_].command_allocator.Get(), nullptr));

  geometry_pass_.CreateBuffersAndUploadData();

  lighting_pass_.CreateBuffersAndUploadData();

  ThrowIfFailed(command_list_->Close());
  ID3D12CommandList* command_lists[] = { command_list_.Get() };
  command_queue_->ExecuteCommandLists(_countof(command_lists), command_lists);

  // TODO: Don't stall here.
  WaitForGpu();

  geometry_pass_.InitResources();

  lighting_pass_.InitResources();
}

void App::UploadDataToBuffer(const void* data, UINT64 data_size, ID3D12Resource* dst_buffer) {
  Microsoft::WRL::ComPtr<ID3D12Resource> upload_buffer;

  CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_UPLOAD);
  CD3DX12_RESOURCE_DESC buffer_desc = CD3DX12_RESOURCE_DESC::Buffer(data_size);

  ThrowIfFailed(device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                                  D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                  IID_PPV_ARGS(&upload_buffer)));

  D3D12_SUBRESOURCE_DATA subresource_data = {};
  subresource_data.pData = data;
  subresource_data.RowPitch = data_size;
  subresource_data.SlicePitch = subresource_data.RowPitch;

  UpdateSubresources<1>(command_list_.Get(), dst_buffer, upload_buffer.Get(), 0, 0, 1,
                        &subresource_data);

  D3D12_RESOURCE_BARRIER barrier =
      CD3DX12_RESOURCE_BARRIER::Transition(dst_buffer, D3D12_RESOURCE_STATE_COPY_DEST,
                                           D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
  command_list_->ResourceBarrier(1, &barrier);

  // Upload buffers must be kept alive until the copy commands are completed.
  upload_buffers_.push_back(upload_buffer);
}

void App::Cleanup() {
  WaitForGpu();

  if (fence_event_ != nullptr)
    CloseHandle(fence_event_);
}

void App::RenderFrame() {
  ThrowIfFailed(frames_[frame_index_].command_allocator->Reset());
  ThrowIfFailed(command_list_->Reset(frames_[frame_index_].command_allocator.Get(), nullptr));

  {
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        frames_[frame_index_].swap_chain_buffer.Get(), D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    command_list_->ResourceBarrier(1, &barrier);
  }

  geometry_pass_.RenderFrame(command_list_.Get());

  lighting_pass_.RenderFrame(command_list_.Get());

  {
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        frames_[frame_index_].swap_chain_buffer.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    command_list_->ResourceBarrier(1, &barrier);
  }

  ThrowIfFailed(command_list_->Close());

  ID3D12CommandList* command_lists[] = { command_list_.Get() };
  command_queue_->ExecuteCommandLists(_countof(command_lists), command_lists);

  ThrowIfFailed(swap_chain_->Present(1, 0));

  MoveToNextFrame();
}

void App::MoveToNextFrame() {
  ThrowIfFailed(command_queue_->Signal(fence_.Get(), latest_fence_value_));
  frames_[frame_index_].fence_value = latest_fence_value_;

  ++latest_fence_value_;

  frame_index_ = swap_chain_->GetCurrentBackBufferIndex();

  if (fence_->GetCompletedValue() < frames_[frame_index_].fence_value) {
    ThrowIfFailed(fence_->SetEventOnCompletion(frames_[frame_index_].fence_value, fence_event_));
    WaitForSingleObjectEx(fence_event_, INFINITE, false);
  }
}

void App::WaitForGpu() {
  const UINT64 wait_value = latest_fence_value_;

  ThrowIfFailed(command_queue_->Signal(fence_.Get(), wait_value));
  ++latest_fence_value_;

  ThrowIfFailed(fence_->SetEventOnCompletion(wait_value, fence_event_));
  WaitForSingleObjectEx(fence_event_, INFINITE, false);
}