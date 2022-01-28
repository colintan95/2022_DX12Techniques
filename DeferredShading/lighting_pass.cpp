#include "lighting_pass.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "d3dx12.h"

#include "dx_utils.h"
#include "ReadData.h"

#include "app.h"

using Microsoft::WRL::ComPtr;
using DX::ThrowIfFailed;

void LightingPass::InitPipeline() {
  CD3DX12_DESCRIPTOR_RANGE1 ranges[3] = {};
  ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0, 0);
  ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0);
  ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2, 0, 0);

  CD3DX12_ROOT_PARAMETER1 root_params[3] = {};
  root_params[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
  root_params[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);
  root_params[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL);

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
     {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, sizeof(float) * 2,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
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

void LightingPass::CreateBuffersAndUploadData() {
   // Must be a multiple 256 bytes.
  light_pos_buffer_size_ =
      (sizeof(DirectX::XMFLOAT4) + (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)) &
      ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);

  {
    CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC resource_desc =
        CD3DX12_RESOURCE_DESC::Buffer(light_pos_buffer_size_);

    ThrowIfFailed(app_->device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                                                          &resource_desc,
                                                          D3D12_RESOURCE_STATE_GENERIC_READ,
                                                          nullptr,
                                                          IID_PPV_ARGS(&light_pos_buffer_)));

    DirectX::XMFLOAT4* buffer_ptr;
    ThrowIfFailed(light_pos_buffer_->Map(0, nullptr, reinterpret_cast<void**>(&buffer_ptr)));

    *buffer_ptr = app_->light_pos_;

    light_pos_buffer_->Unmap(0, nullptr);
  }

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


void LightingPass::CreateResourceViews() {
  for (int i = 0; i < kNumFrames; ++i) {
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(frames_[i].base_rtv_handle_,
                                             RtvPerFrame::Index::kSwapChainBuffer,
                                             app_->rtv_descriptor_size_);

    app_->device_->CreateRenderTargetView(app_->frames_[i].swap_chain_buffer.Get(), nullptr,
                                          rtv_handle);
  }


  for (int i = 0; i < kNumFrames; ++i) {
    CD3DX12_CPU_DESCRIPTOR_HANDLE frame_base_srv_cpu_handle = frames_[i].base_srv_cpu_handle_;

    {
      CD3DX12_CPU_DESCRIPTOR_HANDLE srv_handle(frame_base_srv_cpu_handle,
                                               SrvPerFrame::Index::kAmbientGbufferTexture,
                                               app_->cbv_srv_descriptor_size_);
      D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
      srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MipLevels = 1;
      srv_desc.Texture2D.MostDetailedMip = 0;

      app_->device_->CreateShaderResourceView(app_->frames_[i].gbuffer.Get(), &srv_desc,
                                              srv_handle);
    }

    {
      CD3DX12_CPU_DESCRIPTOR_HANDLE srv_handle(frame_base_srv_cpu_handle,
                                               SrvPerFrame::Index::kPositionGbufferTexture,
                                               app_->cbv_srv_descriptor_size_);
      D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
      srv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
      srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MipLevels = 1;
      srv_desc.Texture2D.MostDetailedMip = 0;

      app_->device_->CreateShaderResourceView(app_->frames_[i].pos_gbuffer.Get(), &srv_desc,
                                              srv_handle);
    }

    {
      CD3DX12_CPU_DESCRIPTOR_HANDLE srv_handle(frame_base_srv_cpu_handle,
                                               SrvPerFrame::Index::kDiffuseGbufferTexture,
                                               app_->cbv_srv_descriptor_size_);
      D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
      srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MipLevels = 1;
      srv_desc.Texture2D.MostDetailedMip = 0;

      app_->device_->CreateShaderResourceView(app_->frames_[i].diffuse_gbuffer.Get(), &srv_desc,
                                              srv_handle);
    }

    {
      CD3DX12_CPU_DESCRIPTOR_HANDLE srv_handle(frame_base_srv_cpu_handle,
                                               SrvPerFrame::Index::kNormalGbufferTexture,
                                               app_->cbv_srv_descriptor_size_);
      D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
      srv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
      srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MipLevels = 1;
      srv_desc.Texture2D.MostDetailedMip = 0;

      app_->device_->CreateShaderResourceView(app_->frames_[i].normal_gbuffer.Get(), &srv_desc,
                                              srv_handle);
    }

    {
      CD3DX12_CPU_DESCRIPTOR_HANDLE srv_handle(frame_base_srv_cpu_handle,
                                               SrvPerFrame::Index::kShadowCubemapTexture,
                                               app_->cbv_srv_descriptor_size_);
      D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
      srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
      srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
      srv_desc.Texture2D.MipLevels = 1;
      srv_desc.Texture2D.MostDetailedMip = 0;

      app_->device_->CreateShaderResourceView(app_->frames_[i].shadow_cubemap.Get(), &srv_desc,
                                              srv_handle);
    }
  }

  D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc{};
  cbv_desc.BufferLocation = light_pos_buffer_->GetGPUVirtualAddress();
  cbv_desc.SizeInBytes = light_pos_buffer_size_;

  app_->device_->CreateConstantBufferView(&cbv_desc, cbv_cpu_handle_);

  {
    CD3DX12_CPU_DESCRIPTOR_HANDLE sampler_handle(base_sampler_cpu_handle_,
                                                 SamplerStatic::Index::kGBufferSampler,
                                                 app_->cbv_srv_descriptor_size_);

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

    app_->device_->CreateSampler(&sampler_desc, sampler_handle);
  }

  {
    CD3DX12_CPU_DESCRIPTOR_HANDLE sampler_handle(base_sampler_cpu_handle_,
                                                 SamplerStatic::Index::kShadowCubemapSampler,
                                                 app_->cbv_srv_descriptor_size_);

    D3D12_SAMPLER_DESC sampler_desc{};
    sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.MinLOD = 0.f;
    sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
    sampler_desc.MipLODBias = 0.0f;
    sampler_desc.MaxAnisotropy = 1;
    sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler_desc.BorderColor[0] = 0;
    sampler_desc.BorderColor[1] = 0;
    sampler_desc.BorderColor[2] = 0;
    sampler_desc.BorderColor[3] = 0;

    app_->device_->CreateSampler(&sampler_desc, sampler_handle);
  }
}


void LightingPass::RenderFrame(ID3D12GraphicsCommandList* command_list) {
  {
    CD3DX12_RESOURCE_BARRIER barriers[5] = {};

    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
        app_->frames_[app_->frame_index_].gbuffer.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
        app_->frames_[app_->frame_index_].pos_gbuffer.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    barriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(
        app_->frames_[app_->frame_index_].diffuse_gbuffer.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    barriers[3] = CD3DX12_RESOURCE_BARRIER::Transition(
        app_->frames_[app_->frame_index_].normal_gbuffer.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    barriers[4] = CD3DX12_RESOURCE_BARRIER::Transition(
        app_->frames_[app_->frame_index_].shadow_cubemap.Get(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    command_list->ResourceBarrier(_countof(barriers), barriers);
  }

  command_list->SetPipelineState(pipeline_.Get());

  command_list->SetGraphicsRootSignature(root_signature_.Get());

  ID3D12DescriptorHeap* heaps[] = { app_->cbv_srv_heap_.Get(), app_->sampler_heap_.Get() };
  command_list->SetDescriptorHeaps(_countof(heaps), heaps);

  CD3DX12_GPU_DESCRIPTOR_HANDLE cbv_gpu_handle = cbv_gpu_handle_;

  command_list->SetGraphicsRootDescriptorTable(0, frames_[app_->frame_index_].base_srv_gpu_handle_);
  command_list->SetGraphicsRootDescriptorTable(1, cbv_gpu_handle);
  command_list->SetGraphicsRootDescriptorTable(2, base_sampler_gpu_handle_);

  command_list->RSSetViewports(1, &app_->viewport_);
  command_list->RSSetScissorRects(1, &app_->scissor_rect_);

  CD3DX12_CPU_DESCRIPTOR_HANDLE swap_chain_rtv_handle(frames_[app_->frame_index_].base_rtv_handle_,
                                                      RtvPerFrame::Index::kSwapChainBuffer,
                                                      app_->rtv_descriptor_size_);

  CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handles[] = { swap_chain_rtv_handle };

  command_list->OMSetRenderTargets(_countof(rtv_handles), rtv_handles, false, nullptr);

  const float clear_color[] = {0.f, 0.f, 0.f, 1.f};
  command_list->ClearRenderTargetView(swap_chain_rtv_handle, clear_color, 0, nullptr);;

  command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

  command_list->IASetVertexBuffers(0, 1, &vertex_buffer_view_);

  command_list->DrawInstanced(4, 1, 0, 0);

  {
    CD3DX12_RESOURCE_BARRIER barriers[5] = {};

    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
        app_->frames_[app_->frame_index_].gbuffer.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
        app_->frames_[app_->frame_index_].pos_gbuffer.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    barriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(
        app_->frames_[app_->frame_index_].diffuse_gbuffer.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    barriers[3] = CD3DX12_RESOURCE_BARRIER::Transition(
        app_->frames_[app_->frame_index_].normal_gbuffer.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    barriers[4] = CD3DX12_RESOURCE_BARRIER::Transition(
        app_->frames_[app_->frame_index_].shadow_cubemap.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    command_list->ResourceBarrier(_countof(barriers), barriers);
  }
}