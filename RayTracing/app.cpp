#include "app.h"

#include "build\raytracing.hlsl.h"

#include "constants.h"
#include "dx_includes.h"

using Microsoft::WRL::ComPtr;
using DX::ThrowIfFailed;

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