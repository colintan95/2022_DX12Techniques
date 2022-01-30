#include "app.h"

#include "build\raytracing.hlsl.h"

#include "constants.h"
#include "dx_includes.h"

using Microsoft::WRL::ComPtr;
using DX::ThrowIfFailed;

namespace {

UINT Align(UINT size, UINT alignment) {
    return (size + (alignment - 1)) & ~(alignment - 1);
}

}  // namespace

App::App(HWND hwnd) : hwnd_(hwnd) {}

void App::Initialize() {
  InitDeviceAndSwapChain();

  {
    CD3DX12_DESCRIPTOR_RANGE1 range{};
    range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

    CD3DX12_ROOT_PARAMETER1 root_params[2] = {};
    root_params[0].InitAsDescriptorTable(1, &range);
    root_params[1].InitAsShaderResourceView(0);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
    root_signature_desc.Init_1_1(_countof(root_params), root_params);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&root_signature_desc,
                                                        D3D_ROOT_SIGNATURE_VERSION_1_1, &signature,
                                                        &error));
    ThrowIfFailed(device_->CreateRootSignature(0, signature->GetBufferPointer(),
                                               signature->GetBufferSize(),
                                               IID_PPV_ARGS(&global_root_signature_)));
  }

  {
    CD3DX12_ROOT_PARAMETER1 root_param{};
    root_param.InitAsConstants(0, 0, 0);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
    root_signature_desc.Init_1_1(1, &root_param, 0, nullptr,
                                 D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&root_signature_desc,
                                                        D3D_ROOT_SIGNATURE_VERSION_1_1, &signature,
                                                        &error));
    ThrowIfFailed(device_->CreateRootSignature(0, signature->GetBufferPointer(),
                                               signature->GetBufferSize(),
                                               IID_PPV_ARGS(&local_root_signature_)));
  }

  {
    ThrowIfFailed(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  IID_PPV_ARGS(&command_allocator_)));

    ThrowIfFailed(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             command_allocator_.Get(), nullptr,
                                             IID_PPV_ARGS(&command_list_)));

    ThrowIfFailed(command_list_.As(&dxr_command_list_));

    ThrowIfFailed(device_->CreateFence(latest_fence_value_, D3D12_FENCE_FLAG_NONE,
                                       IID_PPV_ARGS(&fence_)));
    ++latest_fence_value_;

    fence_event_ = CreateEvent(nullptr, false, false, nullptr);
    if (fence_event_ == nullptr) {
      ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }
  }

  {
    CD3DX12_STATE_OBJECT_DESC pipeline_desc(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

    CD3DX12_DXIL_LIBRARY_SUBOBJECT* dxil_lib =
        pipeline_desc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();

    D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE((void*)g_raytracing_shader,
                                                            ARRAYSIZE(g_raytracing_shader));
    dxil_lib->SetDXILLibrary(&libdxil);

    const wchar_t* shader_names[] = { kRayGenShaderName, kClosestHitShaderName, kMissShaderName };
    dxil_lib->DefineExports(shader_names);

    CD3DX12_HIT_GROUP_SUBOBJECT* hit_group =
        pipeline_desc.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hit_group->SetClosestHitShaderImport(kClosestHitShaderName);
    hit_group->SetHitGroupExport(kHitGroupName);
    hit_group->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

    CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT* shader_config =
        pipeline_desc.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    UINT payload_size = sizeof(float) * 4;
    UINT attribute_size = sizeof(float) * 2;
    shader_config->Config(payload_size, attribute_size);

    {
      CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* local_root_signature =
          pipeline_desc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
      local_root_signature->SetRootSignature(local_root_signature_.Get());

      CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* association =
          pipeline_desc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
      association->SetSubobjectToAssociate(*local_root_signature);
      association->AddExport(kRayGenShaderName);
    }

    CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT* global_root_signature =
        pipeline_desc.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    global_root_signature->SetRootSignature(global_root_signature_.Get());

    CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT* pipeline_config =
        pipeline_desc.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    UINT max_recursion_depth = 1;
    pipeline_config->Config(max_recursion_depth);

     ThrowIfFailed(dxr_device_->CreateStateObject(pipeline_desc, IID_PPV_ARGS(&dxr_state_object_)));
  }

  {
    D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc{};
    descriptor_heap_desc.NumDescriptors = 1;
    descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    descriptor_heap_desc.NodeMask = 0;

    device_->CreateDescriptorHeap(&descriptor_heap_desc, IID_PPV_ARGS(&descriptor_heap_));
  }

  {
    // In screen space - right handed coordinates.
    float vertices[] = {
      0.f, -0.5f, 1.f,
      -0.5f, 0.5f, 1.f,
      0.5f, 0.5f, 1.f
    };

    UINT16 indices[] = { 0, 1, 2 };

    {
      CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_DEFAULT);
      CD3DX12_RESOURCE_DESC buffer_desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(vertices));

      ThrowIfFailed(device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                                                     &buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                     nullptr, IID_PPV_ARGS(&vertex_buffer_)));
    }

    {
      CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_DEFAULT);
      CD3DX12_RESOURCE_DESC buffer_desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(vertices));

      ThrowIfFailed(device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                                                     &buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                     nullptr, IID_PPV_ARGS(&index_buffer_)));
    }

    UploadDataToBuffer(indices, sizeof(vertices), vertex_buffer_.Get());
    UploadDataToBuffer(indices, sizeof(indices), index_buffer_.Get());

    ThrowIfFailed(command_list_->Close());
    ID3D12CommandList* command_lists[] = { command_list_.Get() };
    command_queue_->ExecuteCommandLists(_countof(command_lists), command_lists);

    WaitForGpu();
  }

  {
    ThrowIfFailed(command_allocator_->Reset());
    ThrowIfFailed(command_list_->Reset(command_allocator_.Get(), nullptr));

    D3D12_RAYTRACING_GEOMETRY_DESC geometry_desc{};
    geometry_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry_desc.Triangles.IndexBuffer = index_buffer_->GetGPUVirtualAddress();
    geometry_desc.Triangles.IndexCount = 3;
    geometry_desc.Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
    geometry_desc.Triangles.Transform3x4 = 0;
    geometry_desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry_desc.Triangles.VertexCount = 3;
    geometry_desc.Triangles.VertexBuffer.StartAddress = vertex_buffer_->GetGPUVirtualAddress();
    geometry_desc.Triangles.VertexBuffer.StrideInBytes = sizeof(float) * 3;
    geometry_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS top_level_inputs{};
    top_level_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    top_level_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    top_level_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    top_level_inputs.NumDescs = 1;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO top_level_prebuild_info{};
    dxr_device_->GetRaytracingAccelerationStructurePrebuildInfo(&top_level_inputs,
                                                                &top_level_prebuild_info);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottom_level_inputs{};
    bottom_level_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    bottom_level_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    bottom_level_inputs.Flags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    bottom_level_inputs.NumDescs = 1;
    bottom_level_inputs.pGeometryDescs = &geometry_desc;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottom_level_prebuild_info{};
    dxr_device_->GetRaytracingAccelerationStructurePrebuildInfo(&bottom_level_inputs,
                                                                &bottom_level_prebuild_info);

    ComPtr<ID3D12Resource> scratch_resource;

    {
      CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_DEFAULT);
      CD3DX12_RESOURCE_DESC buffer_desc =
          CD3DX12_RESOURCE_DESC::Buffer(max(bottom_level_prebuild_info.ResultDataMaxSizeInBytes,
                                            top_level_prebuild_info.ResultDataMaxSizeInBytes),
                                        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

      ThrowIfFailed(device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                                                     &buffer_desc,
                                                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                     IID_PPV_ARGS(&scratch_resource)));
    }

    {
      CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_DEFAULT);
      CD3DX12_RESOURCE_DESC buffer_desc =
          CD3DX12_RESOURCE_DESC::Buffer(bottom_level_prebuild_info.ResultDataMaxSizeInBytes,
                                        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

      ThrowIfFailed(device_->CreateCommittedResource(
          &heap_props, D3D12_HEAP_FLAG_NONE, &buffer_desc,
          D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr,
          IID_PPV_ARGS(&bottom_level_acceleration_structure_)));
    }

    {
      CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_DEFAULT);
      CD3DX12_RESOURCE_DESC buffer_desc =
          CD3DX12_RESOURCE_DESC::Buffer(top_level_prebuild_info.ResultDataMaxSizeInBytes,
                                        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

      ThrowIfFailed(device_->CreateCommittedResource(
          &heap_props, D3D12_HEAP_FLAG_NONE, &buffer_desc,
          D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr,
          IID_PPV_ARGS(&top_level_acceleration_structure_)));
    }

    D3D12_RAYTRACING_INSTANCE_DESC instance_desc{};
    instance_desc.Transform[0][0] = 1;
    instance_desc.Transform[1][1] = 1;
    instance_desc.Transform[2][2] = 1;
    instance_desc.AccelerationStructure =
        bottom_level_acceleration_structure_->GetGPUVirtualAddress();
    // UploadDataToBuffer(&instance_desc, sizeof(instance_desc), instance_desc_buffer.Get());

    ComPtr<ID3D12Resource> instance_desc_buffer;

    {
      CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_UPLOAD);
      CD3DX12_RESOURCE_DESC buffer_desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(instance_desc));

      ThrowIfFailed(device_->CreateCommittedResource(
          &heap_props, D3D12_HEAP_FLAG_NONE, &buffer_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
          nullptr, IID_PPV_ARGS(&instance_desc_buffer)));

      D3D12_RAYTRACING_INSTANCE_DESC* buffer_ptr;
      instance_desc_buffer->Map(0, nullptr, reinterpret_cast<void**>(&buffer_ptr));

      *buffer_ptr = instance_desc;

      instance_desc_buffer->Unmap(0, nullptr);
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottom_level_desc{};
    bottom_level_desc.Inputs = bottom_level_inputs;
    bottom_level_desc.ScratchAccelerationStructureData = scratch_resource->GetGPUVirtualAddress();
    bottom_level_desc.DestAccelerationStructureData =
        bottom_level_acceleration_structure_->GetGPUVirtualAddress();

    top_level_inputs.InstanceDescs = instance_desc_buffer->GetGPUVirtualAddress();

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC top_level_desc{};
    top_level_desc.Inputs = top_level_inputs;
    top_level_desc.ScratchAccelerationStructureData = scratch_resource->GetGPUVirtualAddress();
    top_level_desc.DestAccelerationStructureData =
        top_level_acceleration_structure_->GetGPUVirtualAddress();

    dxr_command_list_->BuildRaytracingAccelerationStructure(&bottom_level_desc, 0, nullptr);

    {
      CD3DX12_RESOURCE_BARRIER barrier =
          CD3DX12_RESOURCE_BARRIER::UAV(bottom_level_acceleration_structure_.Get());

      command_list_->ResourceBarrier(1, &barrier);
    }

    dxr_command_list_->BuildRaytracingAccelerationStructure(&top_level_desc, 0, nullptr);

    ThrowIfFailed(command_list_->Close());
    ID3D12CommandList* command_lists[] = { command_list_.Get() };
    command_queue_->ExecuteCommandLists(_countof(command_lists), command_lists);

    WaitForGpu();
  }

  {
    ComPtr<ID3D12StateObjectProperties> state_object_props;
    ThrowIfFailed(dxr_state_object_.As(&state_object_props));

    void* ray_gen_shader_identifier = state_object_props->GetShaderIdentifier(kRayGenShaderName);
    void* hit_group_shader_identifier = state_object_props->GetShaderIdentifier(kHitGroupName);
    void* miss_shader_identifier = state_object_props->GetShaderIdentifier(kMissShaderName);

    UINT shader_identifier_size = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

    {
      UINT shader_record_size = shader_identifier_size + sizeof(RayGenConstantBuffer);

      CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_UPLOAD);
      CD3DX12_RESOURCE_DESC buffer_desc =
          CD3DX12_RESOURCE_DESC::Buffer(Align(shader_record_size,
                                              D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT));

      ThrowIfFailed(device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                                                     &buffer_desc,
                                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                     IID_PPV_ARGS(&ray_gen_shader_table_)));

      uint8_t* buffer_ptr;
      ThrowIfFailed(ray_gen_shader_table_->Map(0, nullptr, reinterpret_cast<void**>(&buffer_ptr)));

      memcpy(buffer_ptr, ray_gen_shader_identifier, shader_identifier_size);
      buffer_ptr += shader_identifier_size;
      memcpy(buffer_ptr, &ray_gen_constants_, sizeof(RayGenConstantBuffer));

      ray_gen_shader_table_->Unmap(0, nullptr);
    }

    {
      CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_UPLOAD);
      CD3DX12_RESOURCE_DESC buffer_desc =
          CD3DX12_RESOURCE_DESC::Buffer(Align(shader_identifier_size,
                                              D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT));

      ThrowIfFailed(device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                                                     &buffer_desc,
                                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                     IID_PPV_ARGS(&hit_group_shader_table)));

      uint8_t* buffer_ptr;
      ThrowIfFailed(hit_group_shader_table->Map(0, nullptr, reinterpret_cast<void**>(&buffer_ptr)));

      memcpy(buffer_ptr, hit_group_shader_identifier, shader_identifier_size);

      hit_group_shader_table->Unmap(0, nullptr);
    }

    {
      CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_UPLOAD);
      CD3DX12_RESOURCE_DESC buffer_desc =
          CD3DX12_RESOURCE_DESC::Buffer(Align(shader_identifier_size,
                                              D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT));

      ThrowIfFailed(device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                                                     &buffer_desc,
                                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                     IID_PPV_ARGS(&miss_shader_table_)));

      uint8_t* buffer_ptr;
      ThrowIfFailed(miss_shader_table_->Map(0, nullptr, reinterpret_cast<void**>(&buffer_ptr)));

      memcpy(buffer_ptr, miss_shader_identifier, shader_identifier_size);

      miss_shader_table_->Unmap(0, nullptr);
    }
  }

  {
    CD3DX12_RESOURCE_DESC output_desc =
        CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, kWindowWidth, kWindowHeight, 1, 1,
                                     1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_DEFAULT);

    ThrowIfFailed(device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &output_desc,
                                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                   IID_PPV_ARGS(&raytracing_output_)));

    uav_handle_ = descriptor_heap_->GetCPUDescriptorHandleForHeapStart();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    device_->CreateUnorderedAccessView(raytracing_output_.Get(), nullptr, &uav_desc, uav_handle_);
  }
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

  ThrowIfFailed(device_.As(&dxr_device_));

  DXGI_SWAP_CHAIN_DESC1 swap_chain_desc{};
  swap_chain_desc.BufferCount = kNumFrames;
  swap_chain_desc.Width = kWindowWidth;
  swap_chain_desc.Height = kWindowHeight;
  swap_chain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swap_chain_desc.SampleDesc.Count = 1;

  D3D12_COMMAND_QUEUE_DESC queue_desc{};
  queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

  ThrowIfFailed(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue_)));

  ComPtr<IDXGISwapChain1> swap_chain;
  ThrowIfFailed(factory->CreateSwapChainForHwnd(command_queue_.Get(), hwnd_, &swap_chain_desc,
                                                nullptr, nullptr, &swap_chain));
  ThrowIfFailed(swap_chain.As(&swap_chain_));
}

void App::Cleanup() {

}

void App::RenderFrame() {

}

void App::WaitForGpu() {
  const UINT64 wait_value = latest_fence_value_;

  ThrowIfFailed(command_queue_->Signal(fence_.Get(), wait_value));
  ++latest_fence_value_;

  ThrowIfFailed(fence_->SetEventOnCompletion(wait_value, fence_event_));
  WaitForSingleObjectEx(fence_event_, INFINITE, false);
}