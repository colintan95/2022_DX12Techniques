#include "dx_utils.h"

#include <d3d12.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace dx_utils {

void GetHardwareAdapter(IDXGIFactory1* factory, IDXGIAdapter1** out_adapter,
                        D3D_FEATURE_LEVEL feature_level) {
  ComPtr<IDXGIAdapter1> adapter;

  ComPtr<IDXGIFactory6> factory6;
  if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory6)))) {
    for (UINT adapter_index = 0;
         factory6->EnumAdapterByGpuPreference(adapter_index, DXGI_GPU_PREFERENCE_UNSPECIFIED,
                                             IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
         ++adapter_index) {
      DXGI_ADAPTER_DESC1 desc;
      adapter->GetDesc1(&desc);

      if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        continue;

      if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), feature_level,  _uuidof(ID3D12Device),
                                      nullptr))) {
        break;
      }
    }
  } else {
    for (UINT adapter_index = 0; 
         factory->EnumAdapters1(adapter_index, &adapter) != DXGI_ERROR_NOT_FOUND; ++adapter_index) {
      DXGI_ADAPTER_DESC1 desc;
      adapter->GetDesc1(&desc);

      if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        continue;

      if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), feature_level,  _uuidof(ID3D12Device),
                                      nullptr))) {
        break;
      }
    }
  }

  *out_adapter = adapter.Detach();
}

}  // namespace dx_utils