#include "geometry_pass.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "d3dx12.h"

#include "dx_utils.h"
#include "ReadData.h"

#include "app.h"

using Microsoft::WRL::ComPtr;
using DX::ThrowIfFailed;

void GeometryPass::InitPipeline() {
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
      0},
     {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, sizeof(float) * 3,
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
  pso_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 4;
  pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pso_desc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;
  pso_desc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pso_desc.RTVFormats[3] = DXGI_FORMAT_R32G32B32A32_FLOAT;
  pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
  pso_desc.SampleDesc.Count = 1;

  ThrowIfFailed(app_->device_->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_)));
}

void GeometryPass::CreateBuffersAndUploadData() {
  // Must be a multiple 256 bytes.
  matrix_buffer_size_ =
      (sizeof(DirectX::XMFLOAT4X4) * 2 + (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)) &
      ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);

  {
    CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC resource_desc =
        CD3DX12_RESOURCE_DESC::Buffer(matrix_buffer_size_);

    ThrowIfFailed(app_->device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                                                          &resource_desc,
                                                          D3D12_RESOURCE_STATE_GENERIC_READ,
                                                          nullptr, IID_PPV_ARGS(&matrix_buffer_)));

    DirectX::XMFLOAT4X4* buffer_ptr;
    ThrowIfFailed(matrix_buffer_->Map(0, nullptr, reinterpret_cast<void**>(&buffer_ptr)));

    buffer_ptr[0] = app_->view_mat_;
    buffer_ptr[1] = app_->world_view_mat_;
    buffer_ptr[2] = app_->world_view_proj_mat_;

    matrix_buffer_->Unmap(0, nullptr);
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
}

void GeometryPass::InitResources() {
  for (int i = 0; i < kNumFrames; ++i) {
    CD3DX12_CPU_DESCRIPTOR_HANDLE frame_base_rtv_handle = frames_[i].base_rtv_handle_;

    {
      CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(frame_base_rtv_handle,
                                               RtvPerFrame::Index::kAmbientGbufferTexture,
                                               app_->rtv_descriptor_size_);

      D3D12_RENDER_TARGET_VIEW_DESC rtv_desc{};
      rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

      app_->device_->CreateRenderTargetView(app_->frames_[i].gbuffer.Get(), &rtv_desc, rtv_handle);
    }

    {
      CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(frame_base_rtv_handle,
                                               RtvPerFrame::Index::kPositionGbufferTexture,
                                               app_->rtv_descriptor_size_);

      D3D12_RENDER_TARGET_VIEW_DESC rtv_desc{};
      rtv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
      rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

      app_->device_->CreateRenderTargetView(app_->frames_[i].pos_gbuffer.Get(), &rtv_desc,
                                            rtv_handle);
    }

    {
      CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(frame_base_rtv_handle,
                                               RtvPerFrame::Index::kDiffuseGbufferTexture,
                                               app_->rtv_descriptor_size_);

      D3D12_RENDER_TARGET_VIEW_DESC rtv_desc{};
      rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

      app_->device_->CreateRenderTargetView(app_->frames_[i].diffuse_gbuffer.Get(), &rtv_desc,
                                            rtv_handle);
    }

    {
      CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(frame_base_rtv_handle,
                                               RtvPerFrame::Index::kNormalGbufferTexture,
                                               app_->rtv_descriptor_size_);

      D3D12_RENDER_TARGET_VIEW_DESC rtv_desc{};
      rtv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
      rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

      app_->device_->CreateRenderTargetView(app_->frames_[i].normal_gbuffer.Get(), &rtv_desc,
                                            rtv_handle);
    }
  }

  {
    D3D12_DEPTH_STENCIL_VIEW_DESC depth_stencil_desc{};
    depth_stencil_desc.Format = DXGI_FORMAT_D32_FLOAT;
    depth_stencil_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    depth_stencil_desc.Flags = D3D12_DSV_FLAG_NONE;

    app_->device_->CreateDepthStencilView(app_->depth_stencil_.Get(), &depth_stencil_desc,
                                          dsv_handle_);
  }

  {
    CD3DX12_CPU_DESCRIPTOR_HANDLE cbv_handle(base_cbv_cpu_handle_, CbvStatic::Index::kMatrixBuffer,
                                             app_->cbv_srv_descriptor_size_);

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc{};
    cbv_desc.BufferLocation = matrix_buffer_->GetGPUVirtualAddress();
    cbv_desc.SizeInBytes = matrix_buffer_size_;

    app_->device_->CreateConstantBufferView(&cbv_desc, cbv_handle);
  }

  {
    CD3DX12_CPU_DESCRIPTOR_HANDLE cbv_handle(base_cbv_cpu_handle_,
                                             CbvStatic::Index::kMaterialsBuffer,
                                             app_->cbv_srv_descriptor_size_);

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc{};
    cbv_desc.BufferLocation = materials_buffer_->GetGPUVirtualAddress();
    cbv_desc.SizeInBytes = materials_buffer_size_;

    app_->device_->CreateConstantBufferView(&cbv_desc, cbv_handle);
  }
}

void GeometryPass::RenderFrame(ID3D12GraphicsCommandList* command_list) {

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

  CD3DX12_CPU_DESCRIPTOR_HANDLE frame_base_rtv_handle =
      frames_[app_->frame_index_].base_rtv_handle_;

  CD3DX12_CPU_DESCRIPTOR_HANDLE ambient_rtv_handle(frame_base_rtv_handle,
                                                   RtvPerFrame::Index::kAmbientGbufferTexture,
                                                   app_->rtv_descriptor_size_);
  CD3DX12_CPU_DESCRIPTOR_HANDLE pos_rtv_handle(frame_base_rtv_handle,
                                               RtvPerFrame::Index::kPositionGbufferTexture,
                                               app_->rtv_descriptor_size_);
  CD3DX12_CPU_DESCRIPTOR_HANDLE diffuse_rtv_handle(frame_base_rtv_handle,
                                                   RtvPerFrame::Index::kDiffuseGbufferTexture,
                                                   app_->rtv_descriptor_size_);
  CD3DX12_CPU_DESCRIPTOR_HANDLE normal_rtv_handle(frame_base_rtv_handle,
                                                  RtvPerFrame::Index::kNormalGbufferTexture,
                                                  app_->rtv_descriptor_size_);

  CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handles[] = {
    ambient_rtv_handle,
    pos_rtv_handle,
    diffuse_rtv_handle,
    normal_rtv_handle
  };

  command_list->OMSetRenderTargets(_countof(rtv_handles), rtv_handles, false, &dsv_handle_);

  const float clear_color[] = {0.f, 0.f, 0.f, 1.f};
  command_list->ClearRenderTargetView(ambient_rtv_handle, clear_color, 0, nullptr);
  command_list->ClearRenderTargetView(diffuse_rtv_handle, clear_color, 0, nullptr);

  const float clear_pos[] = {0.f, 0.f, 0.f, 1.f};
  command_list->ClearRenderTargetView(pos_rtv_handle, clear_pos, 0, nullptr);
  command_list->ClearRenderTargetView(normal_rtv_handle, clear_pos, 0, nullptr);

  command_list->ClearDepthStencilView(dsv_handle_, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);

  for (App::DrawCallArgs& args : app_->draw_call_args_) {
    command_list->SetGraphicsRoot32BitConstant(2, args.material_index, 0);

    command_list->IASetPrimitiveTopology(args.primitive_type);

    command_list->IASetVertexBuffers(0, 1, &args.vertex_buffer_view);
    command_list->IASetIndexBuffer(&args.index_buffer_view);

    command_list->DrawIndexedInstanced(args.index_count, 1, args.start_index, args.vertex_offset,
                                       0);
  }
}