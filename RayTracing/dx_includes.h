#ifndef DX_INCLUDES_H_
#define DX_INCLUDES_H_

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "d3dx12.h"
#include "DirectXMath.h"

// Without this, "Model.h" will error out.
#include <stdexcept>

#include "GraphicsMemory.h"
#include "Model.h"
#include "ResourceUploadBatch.h"

#include "dx_utils.h"
#include "ReadData.h"

#endif  // DX_INCLUDES_H_