#ifndef APP_H_
#define APP_H_

#include <vector>

#include "constants.h"
#include "dx_includes.h"
#include "raytracing_shader.h"

struct Material {
  DirectX::XMFLOAT4 AmbientColor;
  DirectX::XMFLOAT4 DiffuseColor;
};

struct BlasConstants {
  UINT MaterialIndex;
  UINT BaseIbIndex;
};

class App {
public:
  App(HWND hwnd);

  void Initialize();

  void Cleanup();

  void RenderFrame();

private:
  void InitDeviceAndSwapChain();

  void CreateCommandObjects();

  void CreatePipeline();

  void CreateDescriptorHeap();

  void InitData();

  void CreateBuffersAndViews();

  void CreateShaderTables();

  void CreateAccelerationStructure();

  void MoveToNextFrame();

  void WaitForGpu();

  HWND m_hwnd;

  int m_frameIndex = 0;

   Microsoft::WRL::ComPtr<ID3D12Device> m_device;
   Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
   Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;

   Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;

   Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
   UINT64 m_nextFenceValue = 0;
   HANDLE m_fenceEvent;

   Microsoft::WRL::ComPtr<ID3D12Device5> m_dxrDevice;

   Microsoft::WRL::ComPtr<ID3D12RootSignature> m_globalRootSignature;
   Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rayGenRootSignature;
   Microsoft::WRL::ComPtr<ID3D12RootSignature> m_closestHitRootSignature;

   Microsoft::WRL::ComPtr<ID3D12StateObject> m_dxrStateObject;

   Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_cbvSrvUavHeap;
   UINT m_cbvSrvUavOffsetSize = 0;

   CD3DX12_CPU_DESCRIPTOR_HANDLE m_raytracingOutputCpuHandle;
   CD3DX12_GPU_DESCRIPTOR_HANDLE m_raytracingOutputGpuHandle;

   Microsoft::WRL::ComPtr<ID3D12Resource> m_matrixBuffer;
   Microsoft::WRL::ComPtr<ID3D12Resource> m_materialsBuffer;

   CD3DX12_CPU_DESCRIPTOR_HANDLE m_indexBufferCpuHandle;
   CD3DX12_GPU_DESCRIPTOR_HANDLE m_indexBufferGpuHandle;

   CD3DX12_CPU_DESCRIPTOR_HANDLE m_vertexBufferCpuHandle;
   CD3DX12_GPU_DESCRIPTOR_HANDLE m_vertexBufferGpuHandle;

   Microsoft::WRL::ComPtr<ID3D12Resource> m_raytracingOutput;

   Microsoft::WRL::ComPtr<ID3D12Resource> m_rayGenShaderTable;
   Microsoft::WRL::ComPtr<ID3D12Resource> m_hitGroupShaderTable;
   Microsoft::WRL::ComPtr<ID3D12Resource> m_missShaderTable;

   UINT m_hitGroupShaderRecordSize = 0;

   Microsoft::WRL::ComPtr<ID3D12Resource> m_blas;
   Microsoft::WRL::ComPtr<ID3D12Resource> m_tlas;

   Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> m_dxrCommandList;

   struct Frame {
     Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CommandAllocator;
     Microsoft::WRL::ComPtr<ID3D12Resource> SwapChainBuffer;
     UINT64 FenceValue = 0;
   };

   Frame m_Frames[k_NumFrames];

   RayGenConstantBuffer m_rayGenConstants;

   DirectX::XMFLOAT3X4 m_worldViewMat;

   std::unique_ptr<DirectX::GraphicsMemory> m_graphicsMemory;
   std::unique_ptr<DirectX::Model> m_model;

   std::vector<Material> m_materials;
};

#endif  // APP_H_