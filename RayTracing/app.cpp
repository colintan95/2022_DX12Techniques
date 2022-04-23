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

#define SizeOfInUint32(obj) ((sizeof(obj) - 1) / sizeof(UINT32) + 1)

}  // namespace

App::App(HWND hwnd) : m_hwnd(hwnd) {
  float aspectRatio = static_cast<float>(k_windowWidth) / static_cast<float>(k_windowHeight);

  m_rayGenConstants.Viewport = { aspectRatio, 1.f, -aspectRatio, -1.f };
}

void App::Initialize() {
  InitDeviceAndSwapChain();

  CreateCommandObjects();

  CreatePipeline();

  CreateDescriptorHeap();

  InitData();

  CreateBuffersAndViews();

  CreateShaderTables();

  CreateAccelerationStructure();
}

void App::InitDeviceAndSwapChain() {
  UINT factoryFlags = 0;

#if defined(_DEBUG)
  ComPtr<ID3D12Debug> debugController;
  ComPtr<ID3D12Debug1> debugController1;

  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
    debugController->EnableDebugLayer();

    debugController.As(&debugController1);
    debugController1->SetEnableGPUBasedValidation(true);

    factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
  }
#endif  // defined(_DEBUG)

  ComPtr<IDXGIFactory4> factory;
  ThrowIfFailed(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)));

  ComPtr<IDXGIAdapter1> adapter;
  dx_utils::GetHardwareAdapter(factory.Get(), &adapter, D3D_FEATURE_LEVEL_12_1);

  ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&m_device)));
  ThrowIfFailed(m_device.As(&m_dxrDevice));

  D3D12_COMMAND_QUEUE_DESC queueDesc{};
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

  ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

   DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
  swapChainDesc.BufferCount = k_numFrames;
  swapChainDesc.Width = k_windowWidth;
  swapChainDesc.Height = k_windowHeight;
  swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.SampleDesc.Count = 1;

  ComPtr<IDXGISwapChain1> swapChain;
  ThrowIfFailed(factory->CreateSwapChainForHwnd(m_commandQueue.Get(), m_hwnd, &swapChainDesc,
                                                nullptr, nullptr, &swapChain));
  ThrowIfFailed(swapChain.As(&m_swapChain));
}

void App::CreateCommandObjects()   {
  for (int i = 0; i < k_numFrames; ++i) {
    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   IID_PPV_ARGS(&m_Frames[i].CommandAllocator)));
  }

  ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            m_Frames[m_frameIndex].CommandAllocator.Get(), nullptr,
                                            IID_PPV_ARGS(&m_commandList)));

  ThrowIfFailed(m_commandList.As(&m_dxrCommandList));

  ThrowIfFailed(m_device->CreateFence(m_nextFenceValue, D3D12_FENCE_FLAG_NONE,
                                      IID_PPV_ARGS(&m_fence)));
  ++m_nextFenceValue;

  m_fenceEvent = CreateEvent(nullptr, false, false, nullptr);
  if (m_fenceEvent == nullptr) {
    ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
  }
}

void App::CreatePipeline() {
  {
    CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1);

    CD3DX12_ROOT_PARAMETER1 root_params[4] = {};
    root_params[0].InitAsDescriptorTable(1, &ranges[0]);
    root_params[1].InitAsShaderResourceView(0);
    root_params[2].InitAsConstantBufferView(1);
    root_params[3].InitAsDescriptorTable(1, &ranges[1]);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
    root_signature_desc.Init_1_1(_countof(root_params), root_params);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&root_signature_desc,
                                                        D3D_ROOT_SIGNATURE_VERSION_1_1, &signature,
                                                        &error));
    ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
                                               signature->GetBufferSize(),
                                               IID_PPV_ARGS(&m_globalRootSignature)));
  }

  {
    CD3DX12_ROOT_PARAMETER1 root_param{};
    root_param.InitAsConstants(SizeOfInUint32(m_rayGenConstants), 0, 0);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
    root_signature_desc.Init_1_1(1, &root_param, 0, nullptr,
                                 D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&root_signature_desc,
                                                        D3D_ROOT_SIGNATURE_VERSION_1_1, &signature,
                                                        &error));
    ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
                                               signature->GetBufferSize(),
                                               IID_PPV_ARGS(&m_rayGenRootSignature)));
  }

  {
    CD3DX12_ROOT_PARAMETER1 root_param{};
    root_param.InitAsConstants(SizeOfInUint32(BlasConstants), 2, 0);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
    root_signature_desc.Init_1_1(1, &root_param, 0, nullptr,
                                 D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&root_signature_desc,
                                                        D3D_ROOT_SIGNATURE_VERSION_1_1, &signature,
                                                        &error));
    ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
                                               signature->GetBufferSize(),
                                               IID_PPV_ARGS(&m_closestHitRootSignature)));
  }

  CD3DX12_STATE_OBJECT_DESC pipeline_desc(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

  CD3DX12_DXIL_LIBRARY_SUBOBJECT* dxil_lib =
      pipeline_desc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();

  D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE((void*)g_raytracing_shader,
                                                          ARRAYSIZE(g_raytracing_shader));
  dxil_lib->SetDXILLibrary(&libdxil);

  const wchar_t* shader_names[] = { k_rayGenShaderName, k_closestHitShaderName, k_missShaderName };
  dxil_lib->DefineExports(shader_names);

  CD3DX12_HIT_GROUP_SUBOBJECT* hit_group =
      pipeline_desc.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
  hit_group->SetClosestHitShaderImport(k_closestHitShaderName);
  hit_group->SetHitGroupExport(k_hitGroupName);
  hit_group->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

  CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT* shader_config =
      pipeline_desc.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
  UINT payload_size = sizeof(float) * 6;
  UINT attribute_size = sizeof(float) * 2;
  shader_config->Config(payload_size, attribute_size);

  {
    CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* local_root_signature =
        pipeline_desc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    local_root_signature->SetRootSignature(m_rayGenRootSignature.Get());

    CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* association =
        pipeline_desc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    association->SetSubobjectToAssociate(*local_root_signature);
    association->AddExport(k_rayGenShaderName);
  }

  {
    CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* local_root_signature =
        pipeline_desc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    local_root_signature->SetRootSignature(m_closestHitRootSignature.Get());

    CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* association =
        pipeline_desc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    association->SetSubobjectToAssociate(*local_root_signature);
    association->AddExport(k_closestHitShaderName);
  }

  CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT* global_root_signature =
      pipeline_desc.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
  global_root_signature->SetRootSignature(m_globalRootSignature.Get());

  CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT* pipeline_config =
      pipeline_desc.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
  UINT max_recursion_depth = 2;
  pipeline_config->Config(max_recursion_depth);

  ThrowIfFailed(m_dxrDevice->CreateStateObject(pipeline_desc, IID_PPV_ARGS(&m_dxrStateObject)));
}

void App::CreateDescriptorHeap() {
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
  heap_desc.NumDescriptors = 3;
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

  m_device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&m_cbvSrvUavHeap));

  m_cbvSrvUavOffsetSize =
      m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
  CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle(m_cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart());

  m_raytracingOutputCpuHandle = cpuHandle;
  m_raytracingOutputGpuHandle = gpuHandle;

  cpuHandle.Offset(m_cbvSrvUavOffsetSize);
  gpuHandle.Offset(m_cbvSrvUavOffsetSize);

  m_indexBufferCpuHandle = cpuHandle;
  m_indexBufferGpuHandle = gpuHandle;

  cpuHandle.Offset(m_cbvSrvUavOffsetSize);
  gpuHandle.Offset(m_cbvSrvUavOffsetSize);

  m_vertexBufferCpuHandle = cpuHandle;
  m_vertexBufferGpuHandle = gpuHandle;
}

void App::InitData() {
  DirectX::XMStoreFloat3x4(&m_worldViewMat, DirectX::XMMatrixIdentity());

  m_graphicsMemory = std::make_unique<DirectX::GraphicsMemory>(m_device.Get());
  m_model = DirectX::Model::CreateFromSDKMESH(m_device.Get(), L"cornell_box.sdkmesh");

  DirectX::ResourceUploadBatch resourceUpload(m_device.Get());
  resourceUpload.Begin();
  m_model->LoadStaticBuffers(m_device.Get(), resourceUpload);

  std::future<void> resourceLoadDone = resourceUpload.End(m_commandQueue.Get());
  resourceLoadDone.wait();

  for (const auto& effectInfo : m_model->materials) {
    Material material{};
    material.AmbientColor = DirectX::XMFLOAT4(effectInfo.ambientColor.x, effectInfo.ambientColor.y,
                                              effectInfo.ambientColor.z, 0.f);
    material.DiffuseColor = DirectX::XMFLOAT4(effectInfo.diffuseColor.x, effectInfo.diffuseColor.y,
                                              effectInfo.diffuseColor.z, 0.f);

    m_materials.push_back(material);
  }
}

void App::CreateBuffersAndViews() {;
  for (int i = 0; i < k_numFrames; ++i) {
    ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_Frames[i].SwapChainBuffer)));
  }

  {
    UINT bufferSize = Align(sizeof(DirectX::XMFLOAT3X4),
                            D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

    ThrowIfFailed(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
                                                    &resourceDesc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                    IID_PPV_ARGS(&m_matrixBuffer)));

    DirectX::XMFLOAT3X4* ptr;
    ThrowIfFailed(m_matrixBuffer->Map(0, nullptr, reinterpret_cast<void**>(&ptr)));

    *ptr = m_worldViewMat;

    m_matrixBuffer->Unmap(0, nullptr);
  }

  {
    UINT bufferSize = Align(sizeof(Material) * m_materials.size(),
                            D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

    ThrowIfFailed(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
                                                    &resourceDesc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                    IID_PPV_ARGS(&m_materialsBuffer)));

    DirectX::XMFLOAT4X4* ptr;
    ThrowIfFailed(m_materialsBuffer->Map(0, nullptr, reinterpret_cast<void**>(&ptr)));

    std::memcpy(ptr, m_materials.data(), m_materials.size() * sizeof(Material));

    m_materialsBuffer->Unmap(0, nullptr);
  }

  {
    CD3DX12_RESOURCE_DESC output_desc =
        CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, k_windowWidth, k_windowHeight, 1,
                                     1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_DEFAULT);

    ThrowIfFailed(m_device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &output_desc,
                                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                   IID_PPV_ARGS(&m_raytracingOutput)));
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    m_device->CreateUnorderedAccessView(m_raytracingOutput.Get(), nullptr, &uav_desc,
                                        m_raytracingOutputCpuHandle);
  }

  {
    auto const& first_mesh_part = m_model->meshes[0]->opaqueMeshParts[0];

    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.Buffer.NumElements = first_mesh_part->indexBufferSize / 4;
    desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    desc.Buffer.StructureByteStride = 0;

    m_device->CreateShaderResourceView(first_mesh_part->staticIndexBuffer.Get(), &desc,
                                       m_indexBufferCpuHandle);
  }

  {
    auto const& first_mesh_part = m_model->meshes[0]->opaqueMeshParts[0];

    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.Buffer.NumElements = first_mesh_part->vertexCount;
    desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    desc.Buffer.StructureByteStride = first_mesh_part->vertexStride;

    m_device->CreateShaderResourceView(first_mesh_part->staticVertexBuffer.Get(), &desc,
                                       m_vertexBufferCpuHandle);
  }
}

void App::CreateShaderTables() {
  ComPtr<ID3D12StateObjectProperties> state_object_props;
  ThrowIfFailed(m_dxrStateObject.As(&state_object_props));

  void* ray_gen_shader_identifier = state_object_props->GetShaderIdentifier(k_rayGenShaderName);
  void* hit_group_shader_identifier = state_object_props->GetShaderIdentifier(k_hitGroupName);
  void* miss_shader_identifier = state_object_props->GetShaderIdentifier(k_missShaderName);

  UINT shader_identifier_size = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

  {
    UINT shader_record_size = shader_identifier_size + sizeof(RayGenConstantBuffer);

    CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC buffer_desc =
        CD3DX12_RESOURCE_DESC::Buffer(Align(shader_record_size,
                                            D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT));

    ThrowIfFailed(m_device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                                                    &buffer_desc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                    IID_PPV_ARGS(&m_rayGenShaderTable)));

    uint8_t* buffer_ptr;
    ThrowIfFailed(m_rayGenShaderTable->Map(0, nullptr, reinterpret_cast<void**>(&buffer_ptr)));

    memcpy(buffer_ptr, ray_gen_shader_identifier, shader_identifier_size);
    buffer_ptr += shader_identifier_size;
    memcpy(buffer_ptr, &m_rayGenConstants, sizeof(RayGenConstantBuffer));

    m_rayGenShaderTable->Unmap(0, nullptr);
  }

  int num_meshes = 0;
  for (auto& mesh : m_model->meshes) {
    num_meshes += static_cast<UINT>(mesh->opaqueMeshParts.size());
  }

  {
    UINT aligned_identifier_size = Align(shader_identifier_size, sizeof(UINT32));
    m_hitGroupShaderRecordSize = Align(aligned_identifier_size + sizeof(BlasConstants),
                                          D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

    CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC buffer_desc =
        CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(m_hitGroupShaderRecordSize) *
                                      num_meshes);

    ThrowIfFailed(m_device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                                                    &buffer_desc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                    IID_PPV_ARGS(&m_hitGroupShaderTable)));

    uint8_t* ptr;
    ThrowIfFailed(m_hitGroupShaderTable->Map(0, nullptr,
                                                reinterpret_cast<void**>(&ptr)));

    for (auto& mesh : m_model->meshes) {
      uint32_t base_ib_index = 0;

      for (auto& mesh_part : mesh->opaqueMeshParts) {
        memcpy(ptr, hit_group_shader_identifier, shader_identifier_size);

        BlasConstants* constants_ptr =
            reinterpret_cast<BlasConstants*>(ptr + aligned_identifier_size);
        constants_ptr->MaterialIndex = mesh_part->materialIndex;
        constants_ptr->BaseIbIndex = base_ib_index;

        ptr += m_hitGroupShaderRecordSize;

        base_ib_index += mesh_part->indexCount;
      }
    }

    m_hitGroupShaderTable->Unmap(0, nullptr);
  }

  {
    CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC buffer_desc =
        CD3DX12_RESOURCE_DESC::Buffer(Align(shader_identifier_size,
                                            D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT));

    ThrowIfFailed(m_device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                                                    &buffer_desc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                    IID_PPV_ARGS(&m_missShaderTable)));

    uint8_t* buffer_ptr;
    ThrowIfFailed(m_missShaderTable->Map(0, nullptr, reinterpret_cast<void**>(&buffer_ptr)));

    memcpy(buffer_ptr, miss_shader_identifier, shader_identifier_size);

    m_missShaderTable->Unmap(0, nullptr);
  }
}

void App::CreateAccelerationStructure() {
  std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometry_descs;

  for (auto& mesh : m_model->meshes) {
    for (auto& mesh_part : mesh->opaqueMeshParts) {
      // TODO: Right now the vertex buffers also contains the normals. See if we can separate the
      // positions from the normals and uv coords when loading the model.
      D3D12_RAYTRACING_GEOMETRY_DESC geometry_desc{};
      geometry_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
      geometry_desc.Triangles.IndexBuffer =
          mesh_part->staticIndexBuffer->GetGPUVirtualAddress() + mesh_part->startIndex * sizeof(UINT16);
      geometry_desc.Triangles.IndexCount = mesh_part->indexCount;
      geometry_desc.Triangles.IndexFormat = mesh_part->indexFormat;
      geometry_desc.Triangles.Transform3x4 = m_matrixBuffer->GetGPUVirtualAddress();
      geometry_desc.Triangles.VertexBuffer.StartAddress =
          mesh_part->staticVertexBuffer->GetGPUVirtualAddress();
      geometry_desc.Triangles.VertexBuffer.StrideInBytes = mesh_part->vertexStride;
      geometry_desc.Triangles.VertexCount = mesh_part->vertexCount;
      geometry_desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
      geometry_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

      geometry_descs.push_back(geometry_desc);
    }
  }

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS top_level_inputs{};
  top_level_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  top_level_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  top_level_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  top_level_inputs.NumDescs = 1;

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO top_level_prebuild_info{};
  m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&top_level_inputs,
                                                              &top_level_prebuild_info);

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottom_level_inputs{};
  bottom_level_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  bottom_level_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  bottom_level_inputs.Flags =
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  bottom_level_inputs.NumDescs = static_cast<UINT>(geometry_descs.size());
  bottom_level_inputs.pGeometryDescs = geometry_descs.data();

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottom_level_prebuild_info{};
  m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&bottom_level_inputs,
                                                              &bottom_level_prebuild_info);

  ComPtr<ID3D12Resource> scratch_resource;

  {
    CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC buffer_desc =
        CD3DX12_RESOURCE_DESC::Buffer(max(bottom_level_prebuild_info.ResultDataMaxSizeInBytes,
                                          top_level_prebuild_info.ResultDataMaxSizeInBytes),
                                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    ThrowIfFailed(m_device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE,
                                                    &buffer_desc,
                                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                    IID_PPV_ARGS(&scratch_resource)));
  }

  {
    CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC buffer_desc =
        CD3DX12_RESOURCE_DESC::Buffer(bottom_level_prebuild_info.ResultDataMaxSizeInBytes,
                                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    ThrowIfFailed(m_device->CreateCommittedResource(
        &heap_props, D3D12_HEAP_FLAG_NONE, &buffer_desc,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr,
        IID_PPV_ARGS(&m_blas)));
  }

  {
    CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC buffer_desc =
        CD3DX12_RESOURCE_DESC::Buffer(top_level_prebuild_info.ResultDataMaxSizeInBytes,
                                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    ThrowIfFailed(m_device->CreateCommittedResource(
        &heap_props, D3D12_HEAP_FLAG_NONE, &buffer_desc,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr,
        IID_PPV_ARGS(&m_tlas)));
  }

  D3D12_RAYTRACING_INSTANCE_DESC instance_desc{};
  instance_desc.Transform[0][0] = 1;
  instance_desc.Transform[1][1] = 1;
  instance_desc.Transform[2][2] = 1;
  instance_desc.InstanceMask = 1;
  instance_desc.AccelerationStructure = m_blas->GetGPUVirtualAddress();

  ComPtr<ID3D12Resource> instance_desc_buffer;

  {
    CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC buffer_desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(instance_desc));

    ThrowIfFailed(m_device->CreateCommittedResource(
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
  bottom_level_desc.DestAccelerationStructureData = m_blas->GetGPUVirtualAddress();

  top_level_inputs.InstanceDescs = instance_desc_buffer->GetGPUVirtualAddress();

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC top_level_desc{};
  top_level_desc.Inputs = top_level_inputs;
  top_level_desc.ScratchAccelerationStructureData = scratch_resource->GetGPUVirtualAddress();
  top_level_desc.DestAccelerationStructureData = m_tlas->GetGPUVirtualAddress();

  m_dxrCommandList->BuildRaytracingAccelerationStructure(&bottom_level_desc, 0, nullptr);

  {
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_blas.Get());

    m_commandList->ResourceBarrier(1, &barrier);
  }

  m_dxrCommandList->BuildRaytracingAccelerationStructure(&top_level_desc, 0, nullptr);

  ThrowIfFailed(m_commandList->Close());
  ID3D12CommandList* command_lists[] = { m_commandList.Get() };
  m_commandQueue->ExecuteCommandLists(_countof(command_lists), command_lists);

  WaitForGpu();
}

void App::Cleanup() {

}

void App::RenderFrame() {
  ThrowIfFailed(m_Frames[m_frameIndex].CommandAllocator->Reset());
  ThrowIfFailed(m_dxrCommandList->Reset(m_Frames[m_frameIndex].CommandAllocator.Get(), nullptr));

  {
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_Frames[m_frameIndex].SwapChainBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_dxrCommandList->ResourceBarrier(1, &barrier);
  }

  m_dxrCommandList->SetComputeRootSignature(m_globalRootSignature.Get());

  ID3D12DescriptorHeap* descriptor_heaps[] = { m_cbvSrvUavHeap.Get() };
  m_dxrCommandList->SetDescriptorHeaps(_countof(descriptor_heaps), descriptor_heaps);

  m_dxrCommandList->SetComputeRootDescriptorTable(0, m_raytracingOutputGpuHandle);
  m_dxrCommandList->SetComputeRootShaderResourceView(1, m_tlas->GetGPUVirtualAddress());
  m_dxrCommandList->SetComputeRootConstantBufferView(2, m_materialsBuffer->GetGPUVirtualAddress());
  m_dxrCommandList->SetComputeRootDescriptorTable(3, m_indexBufferGpuHandle);

  D3D12_DISPATCH_RAYS_DESC dispatch_desc{};

  dispatch_desc.RayGenerationShaderRecord.StartAddress =
      m_rayGenShaderTable->GetGPUVirtualAddress();
  dispatch_desc.RayGenerationShaderRecord.SizeInBytes = m_rayGenShaderTable->GetDesc().Width;

  dispatch_desc.HitGroupTable.StartAddress = m_hitGroupShaderTable->GetGPUVirtualAddress();
  dispatch_desc.HitGroupTable.SizeInBytes = m_hitGroupShaderTable->GetDesc().Width;
  dispatch_desc.HitGroupTable.StrideInBytes = m_hitGroupShaderRecordSize;

  dispatch_desc.MissShaderTable.StartAddress = m_missShaderTable->GetGPUVirtualAddress();
  dispatch_desc.MissShaderTable.SizeInBytes = m_missShaderTable->GetDesc().Width;
  dispatch_desc.MissShaderTable.StrideInBytes = dispatch_desc.MissShaderTable.SizeInBytes;

  dispatch_desc.Width = k_windowWidth;
  dispatch_desc.Height = k_windowHeight;
  dispatch_desc.Depth = 1;

  m_dxrCommandList->SetPipelineState1(m_dxrStateObject.Get());
  m_dxrCommandList->DispatchRays(&dispatch_desc);

  {
    D3D12_RESOURCE_BARRIER pre_copy_barriers[2] = {};
    pre_copy_barriers[0] =
        CD3DX12_RESOURCE_BARRIER::Transition(m_Frames[m_frameIndex].SwapChainBuffer.Get(),
                                             D3D12_RESOURCE_STATE_RENDER_TARGET,
                                             D3D12_RESOURCE_STATE_COPY_DEST);
    pre_copy_barriers[1] =
        CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutput.Get(),
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                             D3D12_RESOURCE_STATE_COPY_SOURCE);

    m_dxrCommandList->ResourceBarrier(_countof(pre_copy_barriers), pre_copy_barriers);
  }

  m_dxrCommandList->CopyResource(m_Frames[m_frameIndex].SwapChainBuffer.Get(),
                                 m_raytracingOutput.Get());

  {
    D3D12_RESOURCE_BARRIER post_copy_barriers[2] = {};
    post_copy_barriers[0] =
        CD3DX12_RESOURCE_BARRIER::Transition(m_Frames[m_frameIndex].SwapChainBuffer.Get(),
                                             D3D12_RESOURCE_STATE_COPY_DEST,
                                             D3D12_RESOURCE_STATE_PRESENT);
    post_copy_barriers[1] =
        CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutput.Get(),
                                             D3D12_RESOURCE_STATE_COPY_SOURCE,
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    m_dxrCommandList->ResourceBarrier(_countof(post_copy_barriers), post_copy_barriers);
  }

  m_dxrCommandList->Close();

  ID3D12CommandList* command_lists[] = { m_dxrCommandList.Get() };
  m_commandQueue->ExecuteCommandLists(_countof(command_lists), command_lists);

  ThrowIfFailed(m_swapChain->Present(1, 0));

  MoveToNextFrame();
}

void App::MoveToNextFrame() {
  ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_nextFenceValue));
  m_Frames[m_frameIndex].FenceValue = m_nextFenceValue;

  ++m_nextFenceValue;

  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

  if (m_fence->GetCompletedValue() < m_Frames[m_frameIndex].FenceValue) {
    ThrowIfFailed(m_fence->SetEventOnCompletion(m_Frames[m_frameIndex].FenceValue, m_fenceEvent));
    WaitForSingleObjectEx(m_fenceEvent, INFINITE, false);
  }
}

void App::WaitForGpu() {
  const UINT64 wait_value = m_nextFenceValue;

  ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), wait_value));
  ++m_nextFenceValue;

  ThrowIfFailed(m_fence->SetEventOnCompletion(wait_value, m_fenceEvent));
  WaitForSingleObjectEx(m_fenceEvent, INFINITE, false);
}