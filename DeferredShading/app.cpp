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

void App::GeometryPass::InitPipeline() {
  CD3DX12_DESCRIPTOR_RANGE1 ranges[2] = {};
  ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0);
  ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, 0);

  CD3DX12_ROOT_PARAMETER1 root_params[3] = {};
  root_params[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_VERTEX);
  root_params[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);
  root_params[2].InitAsConstants(1, 2, 0, D3D12_SHADER_VISIBILITY_PIXEL);

  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
  root_signature_desc.Init_1_1(_countof(root_params), root_params, 0, nullptr,
                               D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

  ComPtr<ID3DBlob> signature;
  ComPtr<ID3DBlob> error;
  ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&root_signature_desc,
                                                      app_->root_signature_version_, &signature,
                                                      &error));
  ThrowIfFailed(app_->device_->CreateRootSignature(0, signature->GetBufferPointer(),
                                                   signature->GetBufferSize(),
                                                   IID_PPV_ARGS(&root_signature_)));

  std::vector<uint8_t> vertex_shader_data = DX::ReadData(L"geometry_pass_vs.cso");
  D3D12_SHADER_BYTECODE vertex_shader = { vertex_shader_data.data(), vertex_shader_data.size() };

  std::vector<uint8_t> pixel_shader_data = DX::ReadData(L"geometry_pass_ps.cso");
  D3D12_SHADER_BYTECODE pixel_shader = { pixel_shader_data.data(), pixel_shader_data.size() };

  D3D12_INPUT_ELEMENT_DESC input_element_descs[] = {
     {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
      0}
  };

  D3D12_INPUT_LAYOUT_DESC input_layout_desc{};
  input_layout_desc.pInputElementDescs = input_element_descs;
  input_layout_desc.NumElements = _countof(input_element_descs);

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
  pso_desc.InputLayout = input_layout_desc;
  pso_desc.pRootSignature = root_signature_.Get();
  pso_desc.VS = vertex_shader;
  pso_desc.PS = pixel_shader;
  pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  pso_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 3;
  pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pso_desc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;
  pso_desc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
  pso_desc.SampleDesc.Count = 1;

  ThrowIfFailed(app_->device_->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_)));
}

void App::LightingPass::InitPipeline() {
  CD3DX12_DESCRIPTOR_RANGE1 ranges[2] = {};
  ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0);
  ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0);

  CD3DX12_ROOT_PARAMETER1 root_params[2] = {};
  root_params[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
  root_params[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);

  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
  root_signature_desc.Init_1_1(_countof(root_params), root_params, 0, nullptr,
                               D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

  ComPtr<ID3DBlob> signature;
  ComPtr<ID3DBlob> error;
  ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&root_signature_desc,
                                                      app_->root_signature_version_,  &signature,
                                                      &error));
  ThrowIfFailed(app_->device_->CreateRootSignature(0, signature->GetBufferPointer(),
                                             signature->GetBufferSize(),
                                             IID_PPV_ARGS(&root_signature_)));

  std::vector<uint8_t> vertex_shader_data = DX::ReadData(L"lighting_pass_vs.cso");
  D3D12_SHADER_BYTECODE vertex_shader = { vertex_shader_data.data(), vertex_shader_data.size() };

  std::vector<uint8_t> pixel_shader_data = DX::ReadData(L"lighting_pass_ps.cso");
  D3D12_SHADER_BYTECODE pixel_shader = { pixel_shader_data.data(), pixel_shader_data.size() };

  D3D12_INPUT_ELEMENT_DESC input_element_descs[] = {
     {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
     {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
  };

  D3D12_INPUT_LAYOUT_DESC input_layout_desc{};
  input_layout_desc.pInputElementDescs = input_element_descs;
  input_layout_desc.NumElements = _countof(input_element_descs);

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
  pso_desc.InputLayout = input_layout_desc;
  pso_desc.pRootSignature = root_signature_.Get();
  pso_desc.VS = vertex_shader;
  pso_desc.PS = pixel_shader;
  pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  pso_desc.DepthStencilState.DepthEnable = false;
  pso_desc.DepthStencilState.StencilEnable = false;
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 1;
  pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pso_desc.SampleDesc.Count = 1;

  ThrowIfFailed(app_->device_->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_)));
}

void App::InitDescriptorHeapsAndHandles() {
  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
  rtv_heap_desc.NumDescriptors = kNumFrames * 4;
  rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  ThrowIfFailed(device_->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap_)));

  rtv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_heap_->GetCPUDescriptorHandleForHeapStart());
  geometry_pass_.base_rtv_handle_ = rtv_handle;

  rtv_handle.Offset(9, rtv_descriptor_size_);
  lighting_pass_.base_rtv_handle_ = rtv_handle;

  D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc{};
  dsv_heap_desc.NumDescriptors = 1;
  dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  dsv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  ThrowIfFailed(device_->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&dsv_heap_)));

  dsv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

  D3D12_DESCRIPTOR_HEAP_DESC cbv_srv_heap_desc{};
  cbv_srv_heap_desc.NumDescriptors = kNumFrames * 3 + 2;
  cbv_srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  cbv_srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ThrowIfFailed(device_->CreateDescriptorHeap(&cbv_srv_heap_desc, IID_PPV_ARGS(&cbv_srv_heap_)));

  cbv_srv_descriptor_size_ =
       device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  CD3DX12_CPU_DESCRIPTOR_HANDLE cbv_cpu_handle(cbv_srv_heap_->GetCPUDescriptorHandleForHeapStart());
  CD3DX12_GPU_DESCRIPTOR_HANDLE cbv_gpu_handle(cbv_srv_heap_->GetGPUDescriptorHandleForHeapStart());

  geometry_pass_.base_cbv_cpu_handle_ = cbv_cpu_handle;
  geometry_pass_.base_cbv_gpu_handle_ = cbv_gpu_handle;

  cbv_cpu_handle.Offset(2, cbv_srv_descriptor_size_);
  cbv_gpu_handle.Offset(2, cbv_srv_descriptor_size_);

  lighting_pass_.base_srv_cpu_handle_ = cbv_cpu_handle;
  lighting_pass_.base_srv_gpu_handle_ = cbv_gpu_handle;

  D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc{};
  sampler_heap_desc.NumDescriptors = 1;
  sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
  sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ThrowIfFailed(device_->CreateDescriptorHeap(&sampler_heap_desc, IID_PPV_ARGS(&sampler_heap_)));

  lighting_pass_.sampler_cpu_handle_ = sampler_heap_->GetCPUDescriptorHandleForHeapStart();
  lighting_pass_.sampler_gpu_handle_ = sampler_heap_->GetGPUDescriptorHandleForHeapStart();
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

    CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(dsv_heap_->GetCPUDescriptorHandleForHeapStart());
    geometry_pass_.dsv_handle_ = dsv_handle;

    D3D12_DEPTH_STENCIL_VIEW_DESC depth_stencil_desc{};
    depth_stencil_desc.Format = DXGI_FORMAT_D32_FLOAT;
    depth_stencil_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    depth_stencil_desc.Flags = D3D12_DSV_FLAG_NONE;
    device_->CreateDepthStencilView(depth_stencil_.Get(), &depth_stencil_desc, dsv_handle);
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

  DirectX::XMStoreFloat4x4(&world_view_mat_, DirectX::XMMatrixTranspose(world_mat * view_mat));

  // DirectXMath stores the matrix in row-major order while hlsl needs the matrix to be stored in
  // column-major order. So, we apply a transpose when storing the matrix in the buffer.
  DirectX::XMStoreFloat4x4(&world_view_proj_mat_,
                           DirectX::XMMatrixTranspose(world_mat * view_mat * proj_mat));

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

void App::GeometryPass::CreateBuffersAndUploadData() {
  // Must be a multiple 256 bytes.
  scene_constant_buffer_size_ =
      (sizeof(DirectX::XMFLOAT4X4) * 2 + (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)) &
      ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);

  {
    CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC resource_desc =
        CD3DX12_RESOURCE_DESC::Buffer(scene_constant_buffer_size_);

    ThrowIfFailed(app_->device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                                                          &resource_desc,
                                                          D3D12_RESOURCE_STATE_GENERIC_READ,
                                                          nullptr,
                                                          IID_PPV_ARGS(&scene_constant_buffer_)));

    DirectX::XMFLOAT4X4* buffer_ptr;
    ThrowIfFailed(scene_constant_buffer_->Map(0, nullptr, reinterpret_cast<void**>(&buffer_ptr)));

    buffer_ptr[0] = app_->world_view_mat_;
    buffer_ptr[1] = app_->world_view_proj_mat_;

    scene_constant_buffer_->Unmap(0, nullptr);
  }

  // Must be a multiple 256 bytes.
  materials_buffer_size_ =
      (sizeof(Material) * 16 + (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)) &
      ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);

  {
    CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC resource_desc =
        CD3DX12_RESOURCE_DESC::Buffer(materials_buffer_size_);

    ThrowIfFailed(app_->device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                                                          &resource_desc,
                                                          D3D12_RESOURCE_STATE_GENERIC_READ,
                                                          nullptr,
                                                          IID_PPV_ARGS(&materials_buffer_)));

    DirectX::XMFLOAT4X4* buffer_ptr;
    ThrowIfFailed(materials_buffer_->Map(0, nullptr, reinterpret_cast<void**>(&buffer_ptr)));

    std::memcpy(buffer_ptr, app_->materials_.data(), app_->materials_.size() * sizeof(Material));

    materials_buffer_->Unmap(0, nullptr);
  }

  DirectX::ResourceUploadBatch resource_upload(app_->device_.Get());
  resource_upload.Begin();
  app_->model_->LoadStaticBuffers(app_->device_.Get(), resource_upload);

  std::future<void> resource_upload_done = resource_upload.End(app_->command_queue_.Get());
  resource_upload_done.wait();
}

void App::LightingPass::CreateBuffersAndUploadData() {
  // (x, y) - screen coords, (u,v) - texcoords.
  const float vertex_data[] = {
    1.f, -1.f, 1.f, 1.f,
    -1.f, -1.f, 0.f, 1.f,
    1.f, 1.f, 1.f, 0.f,
    -1.f, 1.f, 0.f, 0.f
  };

  CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_DEFAULT);
  CD3DX12_RESOURCE_DESC buffer_desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(vertex_data));

  ThrowIfFailed(app_->device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                                                        &buffer_desc,
                                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                        IID_PPV_ARGS(&vertex_buffer_)));

  app_->UploadDataToBuffer(vertex_data, sizeof(vertex_data), vertex_buffer_.Get());

  vertex_buffer_view_.BufferLocation = vertex_buffer_->GetGPUVirtualAddress();
  vertex_buffer_view_.SizeInBytes = sizeof(vertex_data);
  vertex_buffer_view_.StrideInBytes = sizeof(float) * 4;
}

void App::GeometryPass::InitResources() {
  CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle = base_rtv_handle_;

  for (int i = 0; i < kNumFrames; ++i) {
    {
      D3D12_RENDER_TARGET_VIEW_DESC rtv_desc{};
      rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

      app_->device_->CreateRenderTargetView(app_->frames_[i].gbuffer.Get(), &rtv_desc, rtv_handle);
    }

    rtv_handle.Offset(1, app_->rtv_descriptor_size_);

    {
      D3D12_RENDER_TARGET_VIEW_DESC rtv_desc{};
      rtv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
      rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

      app_->device_->CreateRenderTargetView(app_->frames_[i].pos_gbuffer.Get(), &rtv_desc,
                                            rtv_handle);
    }

    rtv_handle.Offset(1, app_->rtv_descriptor_size_);

    {
      D3D12_RENDER_TARGET_VIEW_DESC rtv_desc{};
      rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

      app_->device_->CreateRenderTargetView(app_->frames_[i].diffuse_gbuffer.Get(), &rtv_desc,
                                            rtv_handle);
    }

    rtv_handle.Offset(1, app_->rtv_descriptor_size_);
  }

  CD3DX12_CPU_DESCRIPTOR_HANDLE cbv_handle = base_cbv_cpu_handle_;

  {
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc{};
    cbv_desc.BufferLocation = scene_constant_buffer_->GetGPUVirtualAddress();
    cbv_desc.SizeInBytes = scene_constant_buffer_size_;

    app_->device_->CreateConstantBufferView(&cbv_desc, cbv_handle);
  }

  cbv_handle.Offset(1, app_->cbv_srv_descriptor_size_);

  {
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc{};
    cbv_desc.BufferLocation = materials_buffer_->GetGPUVirtualAddress();
    cbv_desc.SizeInBytes = materials_buffer_size_;

    app_->device_->CreateConstantBufferView(&cbv_desc, cbv_handle);
  }

  for (auto& mesh : app_->model_->meshes) {
    for (auto& mesh_part : mesh->opaqueMeshParts) {
      DrawCallArgs args{};

      args.primitive_type = mesh_part->primitiveType;

      args.vertex_buffer_view.BufferLocation =
          mesh_part->staticVertexBuffer->GetGPUVirtualAddress();
      args.vertex_buffer_view.SizeInBytes = mesh_part->vertexBufferSize;
      args.vertex_buffer_view.StrideInBytes = mesh_part->vertexStride;

      args.index_buffer_view.BufferLocation =
          mesh_part->staticIndexBuffer->GetGPUVirtualAddress();
      args.index_buffer_view.SizeInBytes = mesh_part->indexBufferSize;
      args.index_buffer_view.Format = mesh_part->indexFormat;

      args.index_count = mesh_part->indexCount;
      args.start_index = mesh_part->startIndex;
      args.vertex_offset = mesh_part->vertexOffset;

      args.material_index = mesh_part->materialIndex;

      draw_call_args_.push_back(args);
    }
  }
}

void App::LightingPass::InitResources() {
  CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle = base_rtv_handle_;

  for (int i = 0; i < kNumFrames; ++i) {
    app_->device_->CreateRenderTargetView(app_->frames_[i].swap_chain_buffer.Get(), nullptr,
                                          rtv_handle);

    rtv_handle.Offset(1, app_->rtv_descriptor_size_);
  }

  CD3DX12_CPU_DESCRIPTOR_HANDLE srv_handle = base_srv_cpu_handle_;

  for (int i = 0; i < kNumFrames; ++i) {
    {
      // TODO: Fix the mip levels here.
      D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
      srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MipLevels = -1;

      app_->device_->CreateShaderResourceView(app_->frames_[i].gbuffer.Get(), &srv_desc,
                                              srv_handle);
    }

    srv_handle.Offset(1, app_->cbv_srv_descriptor_size_);

    {
      // TODO: Fix the mip levels here.
      D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
      srv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
      srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MipLevels = -1;

      app_->device_->CreateShaderResourceView(app_->frames_[i].pos_gbuffer.Get(), &srv_desc,
                                              srv_handle);
    }

    srv_handle.Offset(1, app_->cbv_srv_descriptor_size_);

    {
      // TODO: Fix the mip levels here.
      D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
      srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MipLevels = -1;

      app_->device_->CreateShaderResourceView(app_->frames_[i].diffuse_gbuffer.Get(), &srv_desc,
                                              srv_handle);
    }

    srv_handle.Offset(1, app_->cbv_srv_descriptor_size_);

  }

  D3D12_SAMPLER_DESC sampler_desc{};
  sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  sampler_desc.MinLOD = 0;
  sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
  sampler_desc.MipLODBias = 0.0f;
  sampler_desc.MaxAnisotropy = 1;
  sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  sampler_desc.BorderColor[0] = 0;
  sampler_desc.BorderColor[1] = 0;
  sampler_desc.BorderColor[2] = 0;
  sampler_desc.BorderColor[3] = 0;

  app_->device_->CreateSampler(&sampler_desc, sampler_cpu_handle_);
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

void App::GeometryPass::RenderFrame(ID3D12GraphicsCommandList* command_list) {

  command_list->SetPipelineState(pipeline_.Get());

  command_list->SetGraphicsRootSignature(root_signature_.Get());

  ID3D12DescriptorHeap* heaps[] = { app_->cbv_srv_heap_.Get() };
  command_list->SetDescriptorHeaps(_countof(heaps), heaps);

  CD3DX12_GPU_DESCRIPTOR_HANDLE cbv_handle = base_cbv_gpu_handle_;

  command_list->SetGraphicsRootDescriptorTable(0, cbv_handle);

  cbv_handle.Offset(1, app_->cbv_srv_descriptor_size_);

  command_list->SetGraphicsRootDescriptorTable(1, cbv_handle);

  command_list->RSSetViewports(1, &app_->viewport_);
  command_list->RSSetScissorRects(1, &app_->scissor_rect_);

  CD3DX12_CPU_DESCRIPTOR_HANDLE color_rtv_handle(base_rtv_handle_, app_->frame_index_ * 3,
                                                 app_->rtv_descriptor_size_);
  CD3DX12_CPU_DESCRIPTOR_HANDLE pos_rtv_handle(base_rtv_handle_, app_->frame_index_ * 3 + 1,
                                               app_->rtv_descriptor_size_);
  CD3DX12_CPU_DESCRIPTOR_HANDLE diffuse_rtv_handle(base_rtv_handle_, app_->frame_index_ * 3 + 2,
                                                   app_->rtv_descriptor_size_);

  CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handles[] = {
    color_rtv_handle,
    pos_rtv_handle,
    diffuse_rtv_handle
  };

  command_list->OMSetRenderTargets(_countof(rtv_handles), rtv_handles, false, &dsv_handle_);

  const float clear_color[] = {0.f, 0.f, 0.f, 1.f};
  command_list->ClearRenderTargetView(color_rtv_handle, clear_color, 0, nullptr);
  command_list->ClearRenderTargetView(pos_rtv_handle, clear_color, 0, nullptr);

  command_list->ClearDepthStencilView(dsv_handle_, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);

  for (DrawCallArgs& args : draw_call_args_) {
    command_list->SetGraphicsRoot32BitConstant(2, args.material_index, 0);

    command_list->IASetPrimitiveTopology(args.primitive_type);

    command_list->IASetVertexBuffers(0, 1, &args.vertex_buffer_view);
    command_list->IASetIndexBuffer(&args.index_buffer_view);

    command_list->DrawIndexedInstanced(args.index_count, 1, args.start_index, args.vertex_offset,
                                       0);
  }

  command_list->DrawInstanced(3, 1, 0, 0);
}

void App::LightingPass::RenderFrame(ID3D12GraphicsCommandList* command_list) {
  {
    CD3DX12_RESOURCE_BARRIER barriers[3] = {};

    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
        app_->frames_[app_->frame_index_].gbuffer.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
        app_->frames_[app_->frame_index_].pos_gbuffer.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    barriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(
        app_->frames_[app_->frame_index_].diffuse_gbuffer.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    command_list->ResourceBarrier(_countof(barriers), barriers);
  }

  command_list->SetPipelineState(pipeline_.Get());

  command_list->SetGraphicsRootSignature(root_signature_.Get());

  ID3D12DescriptorHeap* heaps[] = { app_->cbv_srv_heap_.Get(), app_->sampler_heap_.Get() };
  command_list->SetDescriptorHeaps(_countof(heaps), heaps);

  CD3DX12_GPU_DESCRIPTOR_HANDLE srv_gpu_handle(base_srv_gpu_handle_, app_->frame_index_ * 3,
                                               app_->cbv_srv_descriptor_size_);

  command_list->SetGraphicsRootDescriptorTable(0, srv_gpu_handle);
  command_list->SetGraphicsRootDescriptorTable(1, sampler_gpu_handle_);

  command_list->RSSetViewports(1, &app_->viewport_);
  command_list->RSSetScissorRects(1, &app_->scissor_rect_);

  CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(base_rtv_handle_, app_->frame_index_,
                                           app_->rtv_descriptor_size_);

  CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handles[] = { rtv_handle };

  command_list->OMSetRenderTargets(_countof(rtv_handles), rtv_handles, false, nullptr);

  const float clear_color[] = {0.f, 0.f, 0.f, 1.f};
  command_list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);;

  command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

  command_list->IASetVertexBuffers(0, 1, &vertex_buffer_view_);

  command_list->DrawInstanced(4, 1, 0, 0);

  {
    CD3DX12_RESOURCE_BARRIER barriers[3] = {};

    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
        app_->frames_[app_->frame_index_].gbuffer.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
        app_->frames_[app_->frame_index_].pos_gbuffer.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    barriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(
        app_->frames_[app_->frame_index_].diffuse_gbuffer.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    command_list->ResourceBarrier(_countof(barriers), barriers);
  }
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