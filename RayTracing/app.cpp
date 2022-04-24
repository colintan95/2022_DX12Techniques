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
  // Global root signature creation.
  {
    CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 1);

    CD3DX12_ROOT_PARAMETER1 rootParams[3] = {};
    rootParams[0].InitAsDescriptorTable(1, &ranges[0]);
    rootParams[1].InitAsShaderResourceView(0);
    rootParams[2].InitAsDescriptorTable(1, &ranges[1]);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(_countof(rootParams), rootParams);

    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;
    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc,
                                                        D3D_ROOT_SIGNATURE_VERSION_1_1,
                                                        &signatureBlob, &errorBlob));
    ThrowIfFailed(m_device->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                               signatureBlob->GetBufferSize(),
                                               IID_PPV_ARGS(&m_globalRootSignature)));
  }

  // Ray generation root signature creation.
  {
    CD3DX12_ROOT_PARAMETER1 rootParam{};
    rootParam.InitAsConstants(SizeOfInUint32(m_rayGenConstants), 0, 0);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(1, &rootParam, 0, nullptr,
                               D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;
    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc,
                                                        D3D_ROOT_SIGNATURE_VERSION_1_1,
                                                        &signatureBlob, &errorBlob));
    ThrowIfFailed(m_device->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                                signatureBlob->GetBufferSize(),
                                                IID_PPV_ARGS(&m_rayGenRootSignature)));
  }

  // Closest hit root signature creation.
  {
    CD3DX12_ROOT_PARAMETER1 rootParam{};
    rootParam.InitAsConstants(SizeOfInUint32(ClosestHitConstants), 1, 0);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(1, &rootParam, 0, nullptr,
                               D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;
    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc,
                                                        D3D_ROOT_SIGNATURE_VERSION_1_1,
                                                        &signatureBlob, &errorBlob));
    ThrowIfFailed(m_device->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                               signatureBlob->GetBufferSize(),
                                               IID_PPV_ARGS(&m_closestHitRootSignature)));
  }

  CD3DX12_STATE_OBJECT_DESC pipelineDesc(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

  CD3DX12_DXIL_LIBRARY_SUBOBJECT* dxilLib =
      pipelineDesc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();

  D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE((void*)g_raytracing_shader,
                                                          ARRAYSIZE(g_raytracing_shader));
  dxilLib->SetDXILLibrary(&libdxil);

  const wchar_t* shaderNames[] = {
    k_rayGenShaderName, k_closestHitShaderName, k_missShaderName, k_shadowMissShaderName
  };
  dxilLib->DefineExports(shaderNames);

  CD3DX12_HIT_GROUP_SUBOBJECT* hitGroup =
      pipelineDesc.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
  hitGroup->SetClosestHitShaderImport(k_closestHitShaderName);
  hitGroup->SetHitGroupExport(k_hitGroupName);
  hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

  CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT* shaderConfig =
      pipelineDesc.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
  UINT payloadSize = sizeof(float) * 4;
  UINT attributesSize = sizeof(float) * 2;
  shaderConfig->Config(payloadSize, attributesSize);

  {
    CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* localRootSigSubObj =
        pipelineDesc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    localRootSigSubObj->SetRootSignature(m_rayGenRootSignature.Get());

    CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* association =
        pipelineDesc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    association->SetSubobjectToAssociate(*localRootSigSubObj);
    association->AddExport(k_rayGenShaderName);
  }

  {
    CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* localRootSigSubObj =
        pipelineDesc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    localRootSigSubObj->SetRootSignature(m_closestHitRootSignature.Get());

    CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* association =
        pipelineDesc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    association->SetSubobjectToAssociate(*localRootSigSubObj);
    association->AddExport(k_closestHitShaderName);
  }

  {
    CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT* globalRootSigSubObj =
        pipelineDesc.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    globalRootSigSubObj->SetRootSignature(m_globalRootSignature.Get());
  }

  CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT* pipelineConfig =
      pipelineDesc.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
  UINT max_recursion_depth = 2;
  pipelineConfig->Config(max_recursion_depth);

  ThrowIfFailed(m_dxrDevice->CreateStateObject(pipelineDesc, IID_PPV_ARGS(&m_dxrStateObject)));
}

void App::CreateDescriptorHeap() {
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
  heapDesc.NumDescriptors = 4;
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

  m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_cbvSrvUavHeap));

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

  cpuHandle.Offset(m_cbvSrvUavOffsetSize);
  gpuHandle.Offset(m_cbvSrvUavOffsetSize);

  m_materialsBufferCpuHandle = cpuHandle;
  m_materialsBufferGpuHandle = gpuHandle;
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
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

    ComPtr<ID3D12Resource> uploadBuffer;

    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    ThrowIfFailed(m_device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE,
                                                    &bufferDesc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                    IID_PPV_ARGS(&uploadBuffer)));

    CD3DX12_HEAP_PROPERTIES bufferHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(m_device->CreateCommittedResource(&bufferHeapProps, D3D12_HEAP_FLAG_NONE,
                                                    &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                    nullptr, IID_PPV_ARGS(&m_materialsBuffer)));

    D3D12_SUBRESOURCE_DATA subresourceData{};
    subresourceData.pData = m_materials.data();
    subresourceData.RowPitch = sizeof(Material);
    subresourceData.SlicePitch = subresourceData.RowPitch;

    UpdateSubresources<1>(m_commandList.Get(), m_materialsBuffer.Get(), uploadBuffer.Get(), 0, 0, 1,
                          &subresourceData);

    m_uploadBuffers.push_back(uploadBuffer);
  }

  {
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC bufferDesc =
        CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, k_windowWidth, k_windowHeight, 1,
                                     1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    ThrowIfFailed(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                   IID_PPV_ARGS(&m_raytracingOutput)));
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    m_device->CreateUnorderedAccessView(m_raytracingOutput.Get(), nullptr, &uavDesc,
                                        m_raytracingOutputCpuHandle);
  }

  {
    auto const& firstMeshPart = m_model->meshes[0]->opaqueMeshParts[0];

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    srvDesc.Buffer.NumElements = firstMeshPart->indexBufferSize / 4;
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    srvDesc.Buffer.StructureByteStride = 0;

    m_device->CreateShaderResourceView(firstMeshPart->staticIndexBuffer.Get(), &srvDesc,
                                       m_indexBufferCpuHandle);
  }

  {
    auto const& firstMeshPart = m_model->meshes[0]->opaqueMeshParts[0];

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Buffer.NumElements = firstMeshPart->vertexCount;
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    srvDesc.Buffer.StructureByteStride = firstMeshPart->vertexStride;

    m_device->CreateShaderResourceView(firstMeshPart->staticVertexBuffer.Get(), &srvDesc,
                                       m_vertexBufferCpuHandle);
  }

  {
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Buffer.NumElements = m_materials.size();
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    srvDesc.Buffer.StructureByteStride = sizeof(Material);

    m_device->CreateShaderResourceView(m_materialsBuffer.Get(), &srvDesc,
                                       m_materialsBufferCpuHandle);
  }
}

void App::CreateShaderTables() {
  ComPtr<ID3D12StateObjectProperties> stateObjectProps;
  ThrowIfFailed(m_dxrStateObject.As(&stateObjectProps));

  void* rayGenShaderIdentifier = stateObjectProps->GetShaderIdentifier(k_rayGenShaderName);
  void* hitGroupShaderIdentifier = stateObjectProps->GetShaderIdentifier(k_hitGroupName);

  UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

  {
    UINT shaderRecordSize = shaderIdentifierSize + sizeof(RayGenConstantBuffer);

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC bufferDesc =
        CD3DX12_RESOURCE_DESC::Buffer(Align(shaderRecordSize,
                                            D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT));

    ThrowIfFailed(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
                                                    &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                    nullptr, IID_PPV_ARGS(&m_rayGenShaderTable)));

    uint8_t* ptr;
    ThrowIfFailed(m_rayGenShaderTable->Map(0, nullptr, reinterpret_cast<void**>(&ptr)));

    memcpy(ptr, rayGenShaderIdentifier, shaderIdentifierSize);
    ptr += shaderIdentifierSize;
    memcpy(ptr, &m_rayGenConstants, sizeof(RayGenConstantBuffer));

    m_rayGenShaderTable->Unmap(0, nullptr);
  }

  int numMeshes = 0;
  for (auto& mesh : m_model->meshes) {
    numMeshes += static_cast<UINT>(mesh->opaqueMeshParts.size());
  }

  {
    UINT alignedIdentifierSize = Align(shaderIdentifierSize, sizeof(UINT32));
    m_hitGroupShaderRecordSize = Align(alignedIdentifierSize + sizeof(ClosestHitConstants),
                                       D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC bufferDesc =
        CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(m_hitGroupShaderRecordSize) * numMeshes);

    ThrowIfFailed(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
                                                    &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                    nullptr, IID_PPV_ARGS(&m_hitGroupShaderTable)));

    uint8_t* ptr;
    ThrowIfFailed(m_hitGroupShaderTable->Map(0, nullptr, reinterpret_cast<void**>(&ptr)));

    for (auto& mesh : m_model->meshes) {
      uint32_t baseIbIndex = 0;

      for (auto& mesh_part : mesh->opaqueMeshParts) {
        memcpy(ptr, hitGroupShaderIdentifier, shaderIdentifierSize);

        ClosestHitConstants* constantsPtr =
            reinterpret_cast<ClosestHitConstants*>(ptr + shaderIdentifierSize);
        constantsPtr->MaterialIndex = mesh_part->materialIndex;
        constantsPtr->BaseIbIndex = baseIbIndex;

        ptr += m_hitGroupShaderRecordSize;

        baseIbIndex += mesh_part->indexCount;
      }
    }

    m_hitGroupShaderTable->Unmap(0, nullptr);
  }

  {
    void* missShaderId = stateObjectProps->GetShaderIdentifier(k_missShaderName);
    void* shadowMissShaderId = stateObjectProps->GetShaderIdentifier(k_shadowMissShaderName);

    m_missShaderRecordSize = Align(shaderIdentifierSize,
                                   D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC bufferDesc =
        CD3DX12_RESOURCE_DESC::Buffer(m_missShaderRecordSize * 2);

    ThrowIfFailed(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
                                                    &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                    nullptr, IID_PPV_ARGS(&m_missShaderTable)));

    uint8_t* ptr;
    ThrowIfFailed(m_missShaderTable->Map(0, nullptr, reinterpret_cast<void**>(&ptr)));

    memcpy(ptr, missShaderId, shaderIdentifierSize);
    ptr += shaderIdentifierSize;

    memcpy(ptr, shadowMissShaderId, shaderIdentifierSize);

    m_missShaderTable->Unmap(0, nullptr);
  }
}

void App::CreateAccelerationStructure() {
  std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs;

  for (auto& mesh : m_model->meshes) {
    for (auto& meshPart : mesh->opaqueMeshParts) {
      D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
      geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
      geometryDesc.Triangles.IndexBuffer =
          meshPart->staticIndexBuffer->GetGPUVirtualAddress() +
          meshPart->startIndex * sizeof(UINT16);
      geometryDesc.Triangles.IndexCount = meshPart->indexCount;
      geometryDesc.Triangles.IndexFormat = meshPart->indexFormat;
      geometryDesc.Triangles.Transform3x4 = m_matrixBuffer->GetGPUVirtualAddress();
      geometryDesc.Triangles.VertexBuffer.StartAddress =
          meshPart->staticVertexBuffer->GetGPUVirtualAddress();
      geometryDesc.Triangles.VertexBuffer.StrideInBytes = meshPart->vertexStride;
      geometryDesc.Triangles.VertexCount = meshPart->vertexCount;
      geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
      geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

      geometryDescs.push_back(geometryDesc);
    }
  }

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlasInputs{};
  tlasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  tlasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  tlasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  tlasInputs.NumDescs = 1;

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlasPrebuildInfo{};
  m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&tlasInputs, &tlasPrebuildInfo);

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blasInputs{};
  blasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  blasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  blasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  blasInputs.NumDescs = static_cast<UINT>(geometryDescs.size());
  blasInputs.pGeometryDescs = geometryDescs.data();

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blasPrebuildInfo{};
  m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&blasInputs, &blasPrebuildInfo);

  ComPtr<ID3D12Resource> scratchResource;

  {
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC bufferDesc =
        CD3DX12_RESOURCE_DESC::Buffer(max(blasPrebuildInfo.ResultDataMaxSizeInBytes,
                                          tlasPrebuildInfo.ResultDataMaxSizeInBytes),
                                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    ThrowIfFailed(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
                                                    &bufferDesc,
                                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                    IID_PPV_ARGS(&scratchResource)));
  }

  {
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC bufferDesc =
        CD3DX12_RESOURCE_DESC::Buffer(blasPrebuildInfo.ResultDataMaxSizeInBytes,
                                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    ThrowIfFailed(m_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&m_blas)));
  }

  {
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC bufferDesc =
        CD3DX12_RESOURCE_DESC::Buffer(tlasPrebuildInfo.ResultDataMaxSizeInBytes,
                                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    ThrowIfFailed(m_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&m_tlas)));
  }

  D3D12_RAYTRACING_INSTANCE_DESC instanceDesc{};
  instanceDesc.Transform[0][0] = 1;
  instanceDesc.Transform[1][1] = 1;
  instanceDesc.Transform[2][2] = 1;
  instanceDesc.InstanceMask = 1;
  instanceDesc.AccelerationStructure = m_blas->GetGPUVirtualAddress();

  ComPtr<ID3D12Resource> instanceDescBuffer;

  {
    CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC buffer_desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(instanceDesc));

    ThrowIfFailed(m_device->CreateCommittedResource(
        &heap_props, D3D12_HEAP_FLAG_NONE, &buffer_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&instanceDescBuffer)));

    D3D12_RAYTRACING_INSTANCE_DESC* ptr;
    instanceDescBuffer->Map(0, nullptr, reinterpret_cast<void**>(&ptr));

    *ptr = instanceDesc;

    instanceDescBuffer->Unmap(0, nullptr);
  }

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blasBuildDesc{};
  blasBuildDesc.Inputs = blasInputs;
  blasBuildDesc.ScratchAccelerationStructureData = scratchResource->GetGPUVirtualAddress();
  blasBuildDesc.DestAccelerationStructureData = m_blas->GetGPUVirtualAddress();

  m_dxrCommandList->BuildRaytracingAccelerationStructure(&blasBuildDesc, 0, nullptr);

  {
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_blas.Get());

    m_commandList->ResourceBarrier(1, &barrier);
  }

  tlasInputs.InstanceDescs = instanceDescBuffer->GetGPUVirtualAddress();

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlasBuildDesc{};
  tlasBuildDesc.Inputs = tlasInputs;
  tlasBuildDesc.ScratchAccelerationStructureData = scratchResource->GetGPUVirtualAddress();
  tlasBuildDesc.DestAccelerationStructureData = m_tlas->GetGPUVirtualAddress();

  m_dxrCommandList->BuildRaytracingAccelerationStructure(&tlasBuildDesc, 0, nullptr);

  ThrowIfFailed(m_commandList->Close());
  ID3D12CommandList* commandLists[] = { m_commandList.Get() };
  m_commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

  WaitForGpu();

  m_uploadBuffers.clear();
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

  ID3D12DescriptorHeap* descriptorHeaps[] = { m_cbvSrvUavHeap.Get() };
  m_dxrCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

  m_dxrCommandList->SetComputeRootDescriptorTable(0, m_raytracingOutputGpuHandle);
  m_dxrCommandList->SetComputeRootShaderResourceView(1, m_tlas->GetGPUVirtualAddress());
  m_dxrCommandList->SetComputeRootDescriptorTable(2, m_indexBufferGpuHandle);

  D3D12_DISPATCH_RAYS_DESC dispatchDesc{};

  dispatchDesc.RayGenerationShaderRecord.StartAddress = m_rayGenShaderTable->GetGPUVirtualAddress();
  dispatchDesc.RayGenerationShaderRecord.SizeInBytes = m_rayGenShaderTable->GetDesc().Width;

  dispatchDesc.HitGroupTable.StartAddress = m_hitGroupShaderTable->GetGPUVirtualAddress();
  dispatchDesc.HitGroupTable.SizeInBytes = m_hitGroupShaderTable->GetDesc().Width;
  dispatchDesc.HitGroupTable.StrideInBytes = m_hitGroupShaderRecordSize;

  dispatchDesc.MissShaderTable.StartAddress = m_missShaderTable->GetGPUVirtualAddress();
  dispatchDesc.MissShaderTable.SizeInBytes = m_missShaderTable->GetDesc().Width;
  dispatchDesc.MissShaderTable.StrideInBytes = m_missShaderRecordSize;

  dispatchDesc.Width = k_windowWidth;
  dispatchDesc.Height = k_windowHeight;
  dispatchDesc.Depth = 1;

  m_dxrCommandList->SetPipelineState1(m_dxrStateObject.Get());
  m_dxrCommandList->DispatchRays(&dispatchDesc);

  {
    D3D12_RESOURCE_BARRIER preCopyBarriers[2] = {};
    preCopyBarriers[0] =
        CD3DX12_RESOURCE_BARRIER::Transition(m_Frames[m_frameIndex].SwapChainBuffer.Get(),
                                             D3D12_RESOURCE_STATE_RENDER_TARGET,
                                             D3D12_RESOURCE_STATE_COPY_DEST);
    preCopyBarriers[1] =
        CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutput.Get(),
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                             D3D12_RESOURCE_STATE_COPY_SOURCE);

    m_dxrCommandList->ResourceBarrier(_countof(preCopyBarriers), preCopyBarriers);
  }

  m_dxrCommandList->CopyResource(m_Frames[m_frameIndex].SwapChainBuffer.Get(),
                                 m_raytracingOutput.Get());

  {
    D3D12_RESOURCE_BARRIER postCopyBarriers[2] = {};
    postCopyBarriers[0] =
        CD3DX12_RESOURCE_BARRIER::Transition(m_Frames[m_frameIndex].SwapChainBuffer.Get(),
                                             D3D12_RESOURCE_STATE_COPY_DEST,
                                             D3D12_RESOURCE_STATE_PRESENT);
    postCopyBarriers[1] =
        CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutput.Get(),
                                             D3D12_RESOURCE_STATE_COPY_SOURCE,
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    m_dxrCommandList->ResourceBarrier(_countof(postCopyBarriers), postCopyBarriers);
  }

  m_dxrCommandList->Close();

  ID3D12CommandList* commandLists[] = { m_dxrCommandList.Get() };
  m_commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

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
  const UINT64 waitValue = m_nextFenceValue;

  ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), waitValue));
  ++m_nextFenceValue;

  ThrowIfFailed(m_fence->SetEventOnCompletion(waitValue, m_fenceEvent));
  WaitForSingleObjectEx(m_fenceEvent, INFINITE, false);
}