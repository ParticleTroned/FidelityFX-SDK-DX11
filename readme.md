## Build libs
Run `BuildFidelityFXDX11.bat` from the `sdk` folder. Compiled libs are located in `sdk\bin`.

## DX11 upscaling compatibility

This fork carries the published FSR 3.1.6 upscaling algorithm from
[AMD FSR SDK 2.4.0](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases/tag/v2.4.0)
while preserving its DX11 backend and host ABI. Reprojection updates the
accumulation parameters in place, and both history-sampling precision paths
use the previous frame's upscale dimensions. The DX11 shader initialization
and resource bindings remain compatible with Shader Model 5.

Applications can keep this static DX11 implementation alongside AMD's SDK
2.4 runtime DLLs as an independent FSR3 path or fallback. It does not expose
the SDK 2.4 DLL provider API, implement FSR4, or update frame generation.
The FSR3 and FSR3 upscaler version macros report the upscaling algorithm
level, 3.1.6; the backend API remains the fork's existing interface.

## DX11 regression tests

Enable `FFX_BUILD_DX11_TESTS` in a configured DX11 FSR3 static-library build
to run the backend contract and upscaler tests. For example, with the build
directory at `build/dx11`:

```sh
cmake -S sdk -B build/dx11 -DFFX_BUILD_DX11_TESTS=ON
cmake --build build/dx11 --config Release --target dx11_backend_test fsr3_dx11_test
ctest --test-dir build/dx11 -C Release --output-on-failure
```

The tests cover queue bounds, constant-buffer snapshots and shader slots,
ring wrapping, the separate-buffer upload path, injected failures and
cleanup, automatic history reset after failed dispatch, and two-context
FSR3 output through resolution changes. WARP is
used by CTest; `fsr3_dx11_test` also accepts a DXGI adapter index instead of
`warp`, followed optionally by `fp32`. The backend contract test requires
the D3D11 debug layer and reports a skip if it is unavailable.

<h1>Welcome to the FidelityFX Super Resolution 3 SDK</h1>

![alt text](/docs/media/fidelityfxsdk-logo-rescaled.png)

The FidelityFX SDK is a collection of heavily optimized, open source technologies (shader and runtime code) that can be used by developers to improve their DirectX®12 or Vulkan® applications. 

This release of the FidelityFX SDK includes:

| [FidelityFX SDK](https://gpuopen.com/amd-fidelityfx-sdk/) Technique | [Samples](/docs/samples/index.md) | [GPUOpen](https://gpuopen.com/) page | Description |
| --- | --- | --- | --- |
| [Super Resolution (Interpolation enabled)](/docs/techniques/super-resolution-interpolation.md) 3.0.4 | [Super Resolution sample](/docs/samples/super-resolution.md) | [FidelityFX Super Resolution 3](https://gpuopen.com/fidelityfx-superresolution-3/) | Offers a solution for producing high resolution frames from lower resolution inputs and generating interpolated frames. |
| [Optical Flow](/docs/techniques/optical-flow.md) 1.0                                                 | [Super Resolution sample](/docs/samples/super-resolution.md) | [FidelityFX Super Resolution 3](https://gpuopen.com/fidelityfx-superresolution-3/) | Estimates the motion between the current and the previous scene inputs. |
| [Frame Interpolation](/docs/techniques/frame-interpolation.md) 1.0                                   | [Super Resolution sample](/docs/samples/super-resolution.md) | [FidelityFX Super Resolution 3](https://gpuopen.com/fidelityfx-superresolution-3/) | Generates an image half way between the previous and the current frame. |
| [Frame Interpolation Swap Chain](/docs/techniques/frame-interpolation-swap-chain.md) 1.0             | [Super Resolution sample](/docs/samples/super-resolution.md) | [FidelityFX Super Resolution 3](https://gpuopen.com/fidelityfx-superresolution-3/) | Proxy swap chain to replace the DXGI swap chain and handle frame pacing, UI composition and presentation. |

<h2>Further information</h2>

- [Getting started](/docs/getting-started/index.md)
  - [Overview](/docs/getting-started/index.md)
  - [SDK structure](/docs/getting-started/sdk-structure.md)
  - [Building the samples](/docs/getting-started/building-samples.md)
  - [Running the samples](/docs/getting-started/running-samples.md)
  - [Naming guidelines](/docs/getting-started/naming-guidelines.md)

- [Tools](/docs/tools/index.md)
  - [Shader Precompiler](/docs/tools/ffx-sc.md)
  - [FidelityFX SDK Media Delivery System](/docs/media-delivery.md)

- Additional notes
  - At this time, FSR3 only supports use of the native DirectX®12 backend. Please use [BuildFSRSolutionNative.bat](#BuildFSRSolutionNative.bat) or [BuildFSRSolutionNativeDll.bat](#BuildFSRSolutionNativeDll.bat) to build the backend and generate a solution for the sample.
  - Some combinations of high-refresh rate monitors and graphics cards may not support VRR behaviour at the top range of the monitor's refresh rate. You may therefore have to set your monitor's max refresh rate to a value where VRR is supported.
  - The GPU limiter in the sample can result in poor frame pacing at low frame rates. It is recommended to use the CPU limiter in all scenarios when examining FSR3 in the sample.
   
<h2>Known issues</h2>

| FidelityFX SDK Sample | API / Configuration | Problem Description |
| --- | --- | --- |
| FidelityFX FSR | Vulkan® | Vulkan support is in development and will be released in a future version. |
| FidelityFX FSR | Cauldron | The Cauldron backend is not supported by FSR3. |

<h2>Open source</h2>

AMD FidelityFX SDK is open source, and available under the MIT license.

For more information on the license terms please refer to [license](/docs/license.md).

<h2>Disclaimer</h2>

The information contained herein is for informational purposes only, and is subject to change without notice. While every
precaution has been taken in the preparation of this document, it may contain technical inaccuracies, omissions and typographical
errors, and AMD is under no obligation to update or otherwise correct this information. Advanced Micro Devices, Inc. makes no
representations or warranties with respect to the accuracy or completeness of the contents of this document, and assumes no
liability of any kind, including the implied warranties of noninfringement, merchantability or fitness for particular purposes, with
respect to the operation or use of AMD hardware, software or other products described herein. No license, including implied or
arising by estoppel, to any intellectual property rights is granted by this document. Terms and limitations applicable to the purchase
or use of AMD’s products are as set forth in a signed agreement between the parties or in AMD's Standard Terms and Conditions
of Sale.

AMD, the AMD Arrow logo, Radeon, Ryzen, CrossFire, RDNA and combinations thereof are trademarks of Advanced Micro Devices, Inc.
Other product names used in this publication are for identification purposes only and may be trademarks of their respective companies.

DirectX is a registered trademark of Microsoft Corporation in the US and other jurisdictions.

Vulkan and the Vulkan logo are registered trademarks of the Khronos Group Inc.

OpenCL is a trademark of Apple Inc. used by permission by Khronos Group, Inc.

Microsoft is a registered trademark of Microsoft Corporation in the US and other jurisdictions.

Windows is a registered trademark of Microsoft Corporation in the US and other jurisdictions.

© 2022-2023 Advanced Micro Devices, Inc. All rights reserved.
