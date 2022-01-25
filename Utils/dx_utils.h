#ifndef DX_UTILS_H_
#define DX_UTILS_H_

#include <d3d12.h>
#include <dxgi1_6.h>
#include <winerror.h>

#include <exception>

namespace DX {

inline void ThrowIfFailed(HRESULT hr) {
  if (FAILED(hr))
  {
    // Set a breakpoint on this line to catch DirectX API errors
    throw std::exception();
  }
}

}  // namespace DX

namespace dx_utils {

void GetHardwareAdapter(IDXGIFactory1* factory, IDXGIAdapter1** out_adapter, 
                        D3D_FEATURE_LEVEL feature_level);

}  // namespace dx_utils


#endif  // DX_UTILS_H_