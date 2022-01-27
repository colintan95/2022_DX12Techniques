#include "shadow_pass.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "d3dx12.h"
#include "DirectXMath.h"

#include "dx_utils.h"
#include "ReadData.h"

#include "app.h"

using Microsoft::WRL::ComPtr;
using DX::ThrowIfFailed;

ShadowPass::ShadowPass(App* app)
  : app_(app),
    viewport_(0.f, 0.f, static_cast<float>(kShadowBufferWidth),
              static_cast<float>(kShadowBufferHeight)),
    scissor_rect_(0, 0, kShadowBufferWidth, kShadowBufferHeight) {}

void ShadowPass::InitPipeline() {
  CD3DX12_DESCRIPTOR_RANGE1 range;
  range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0);

  CD3DX12_ROOT_PARAMETER1 root_param;
  root_param.InitAsDescriptorTable(1, &range, D3D12_SHADER_VISIBILITY_VERTEX);

  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
  root_signature_desc.Init_1_1(1, &root_param, 0, nullptr,
                               D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

  ComPtr<ID3DBlob> signature;
  ComPtr<ID3DBlob> error;
  ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&root_signature_desc,
                                                      app_->root_signature_version_, &signature,
                                                      &error));
  ThrowIfFailed(app_->device_->CreateRootSignature(0, signature->GetBufferPointer(),
                                                   signature->GetBufferSize(),
                                                   IID_PPV_ARGS(&root_signature_)));

  std::vector<uint8_t> vertex_shader_data = DX::ReadData(L"shadow_pass_vs.cso");
  D3D12_SHADER_BYTECODE vertex_shader = { vertex_shader_data.data(), vertex_shader_data.size() };


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
  pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  pso_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 0;
  pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
  pso_desc.SampleDesc.Count = 1;

  ThrowIfFailed(app_->device_->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_)));
}


void ShadowPass::CreateBuffersAndUploadData() {
  // Must be a multiple 256 bytes.
  matrix_buffer_size_ =
      (sizeof(DirectX::XMFLOAT4X4) + (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)) &
      ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);

  CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_UPLOAD);
  CD3DX12_RESOURCE_DESC resource_desc =
      CD3DX12_RESOURCE_DESC::Buffer(matrix_buffer_size_);

  DirectX::XMMATRIX world_mat = DirectX::XMMatrixIdentity();
  DirectX::XMMATRIX view_mat =
      DirectX::XMMatrixTranslation(-(app_->light_pos_.x), -(app_->light_pos_.y),
                                   -(app_->light_pos_.z)) *
      DirectX::XMMatrixRotationX(-DirectX::XM_PI / 2.f);
  DirectX::XMMATRIX proj_mat = DirectX::XMMatrixPerspectiveFovLH(
      90.f, static_cast<float>(kShadowBufferWidth) / static_cast<float>(kShadowBufferHeight), 0.01f,
      10.f);

  DirectX::XMStoreFloat4x4(&shadow_mat_,
                           DirectX::XMMatrixTranspose(world_mat * view_mat * proj_mat));

  ThrowIfFailed(app_->device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                                                        &resource_desc,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        nullptr, IID_PPV_ARGS(&matrix_buffer_)));

  DirectX::XMFLOAT4X4* buffer_ptr;
  ThrowIfFailed(matrix_buffer_->Map(0, nullptr, reinterpret_cast<void**>(&buffer_ptr)));

  *buffer_ptr = shadow_mat_;

  matrix_buffer_->Unmap(0, nullptr);
}

void ShadowPass::InitResources() {
  for (int i = 0; i < kNumFrames; ++i) {
    D3D12_DEPTH_STENCIL_VIEW_DESC depth_stencil_desc{};
    depth_stencil_desc.Format = DXGI_FORMAT_D32_FLOAT;
    depth_stencil_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    depth_stencil_desc.Flags = D3D12_DSV_FLAG_NONE;

    app_->device_->CreateDepthStencilView(app_->frames_[i].shadow_buffer_.Get(),
                                          &depth_stencil_desc, frames_[i].dsv_handle);
  }

  {
    CD3DX12_CPU_DESCRIPTOR_HANDLE cbv_handle(cbv_cpu_handle_, CbvStatic::Index::kMatrixBuffer,
                                             app_->cbv_srv_descriptor_size_);

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc{};
    cbv_desc.BufferLocation = matrix_buffer_->GetGPUVirtualAddress();
    cbv_desc.SizeInBytes = matrix_buffer_size_;

    app_->device_->CreateConstantBufferView(&cbv_desc, cbv_handle);
  }
}

void ShadowPass::RenderFrame(ID3D12GraphicsCommandList* command_list) {
  command_list->SetPipelineState(pipeline_.Get());
  command_list->SetGraphicsRootSignature(root_signature_.Get());

  command_list->RSSetViewports(1, &viewport_);
  command_list->RSSetScissorRects(1, &scissor_rect_);

  ID3D12DescriptorHeap* heaps[] = { app_->cbv_srv_heap_.Get() };
  command_list->SetDescriptorHeaps(_countof(heaps), heaps);

  command_list->SetGraphicsRootDescriptorTable(0, cbv_gpu_handle_);

  CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle = frames_[app_->frame_index_].dsv_handle;

  command_list->OMSetRenderTargets(0, nullptr, false, &dsv_handle);

  command_list->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);

  for (App::DrawCallArgs& args : app_->draw_call_args_) {
    command_list->IASetPrimitiveTopology(args.primitive_type);

    command_list->IASetVertexBuffers(0, 1, &args.vertex_buffer_view);
    command_list->IASetIndexBuffer(&args.index_buffer_view);

    command_list->DrawIndexedInstanced(args.index_count, 1, args.start_index, args.vertex_offset,
                                       0);
  }
}