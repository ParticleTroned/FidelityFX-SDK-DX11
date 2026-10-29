#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/backends/dx11/ffx_dx11.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
static_assert(FFX_FSR3_VERSION_PATCH == 6 && FFX_FSR3UPSCALER_VERSION_PATCH == 6);
void Check(HRESULT result, const char* operation)
{
    if (FAILED(result)) throw std::runtime_error(std::string(operation) + " HRESULT=" + std::to_string(result));
}
void CheckFfx(FfxErrorCode result, const char* operation)
{
    if (result != FFX_OK) throw std::runtime_error(std::string(operation) + " FFX=" + std::to_string(result));
}
ComPtr<ID3D11Texture2D> Texture(ID3D11Device* device, DXGI_FORMAT format, UINT pixelBytes,
    const void* initialPixel, const char* name, bool staging = false)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = 128;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = format;
    desc.Usage = staging ? D3D11_USAGE_STAGING : D3D11_USAGE_DEFAULT;
    desc.BindFlags = staging ? 0 : D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    desc.CPUAccessFlags = staging ? D3D11_CPU_ACCESS_READ : 0;
    std::vector<unsigned char> bytes(128 * 128 * pixelBytes);
    if (initialPixel) for (size_t i = 0; i < bytes.size(); i += pixelBytes) std::memcpy(bytes.data() + i, initialPixel, pixelBytes);
    D3D11_SUBRESOURCE_DATA data{bytes.data(), 128 * pixelBytes, 0};
    ComPtr<ID3D11Texture2D> result;
    Check(device->CreateTexture2D(&desc, initialPixel ? &data : nullptr, &result), "CreateTexture2D");
    Check(result->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(std::strlen(name)), name), "resource name");
    return result;
}
FfxResource Resource(ID3D11Texture2D* texture, const wchar_t* name, FfxResourceStates state = FFX_RESOURCE_STATE_COMPUTE_READ)
{
    return ffxGetResourceDX11_Fsr31(texture, GetFfxResourceDescriptionDX11(texture), name, state);
}
struct Context
{
    std::unique_ptr<FfxFsr3Context> value = std::make_unique<FfxFsr3Context>();
    bool active = false;
    ~Context() { if (active) ffxFsr3ContextDestroy(value.get()); }
};
static FfxScheduleGpuJobFunc originalSchedule;
static unsigned clearJobs = 0;
FfxErrorCode TrackScheduledJobs(FfxInterface* backend, const FfxGpuJobDescription* job)
{
    clearJobs += job->jobType == FFX_GPU_JOB_CLEAR_FLOAT;
    return originalSchedule(backend, job);
}
static FfxExecuteGpuJobsFunc originalExecute;
static FfxUnregisterResourcesFunc originalUnregister;
static bool failExecution = false;
static bool failCleanup = false;
static unsigned cleanupCalls = 0;
FfxErrorCode ExecuteWithFailure(FfxInterface* backend, FfxCommandList commands)
{
    const auto result = originalExecute(backend, commands);
    return failExecution ? FFX_ERROR_BACKEND_API_ERROR : result;
}
FfxErrorCode UnregisterWithFailure(FfxInterface* backend, FfxCommandList commands, FfxUInt32 contextId)
{
    ++cleanupCalls;
    const auto result = originalUnregister(backend, commands, contextId);
    return failCleanup ? FFX_ERROR_INVALID_ARGUMENT : result;
}
template<class Dispatch>
void CheckFailureReports(Dispatch&& dispatch)
{
    for (unsigned failure = 1; failure <= 3; ++failure) {
        failExecution = (failure & 1) != 0;
        failCleanup = (failure & 2) != 0;
        const auto before = cleanupCalls;
        const auto error = dispatch();
        const auto expected = failExecution ? FFX_ERROR_BACKEND_API_ERROR : FFX_ERROR_INVALID_ARGUMENT;
        if (error != expected || cleanupCalls != before + 1)
            throw std::runtime_error("dispatch error propagation or cleanup failed");
    }
    failExecution = failCleanup = false;
}
static FfxGetDeviceCapabilitiesFunc originalCapabilities;
FfxErrorCode ForceFullPrecision(FfxInterface* backend, FfxDeviceCapabilities* caps)
{
    const auto result = originalCapabilities(backend, caps);
    caps->fp16Supported = false;
    return result;
}
int main(int argc, char** argv)
try {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> commands;
    D3D_FEATURE_LEVEL level{};
    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1};
    ComPtr<IDXGIFactory1> factory;
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
    ComPtr<IDXGIAdapter1> selected;
    const bool warp = argc > 1 && std::string(argv[1]) == "warp";
    if (!warp) Check(factory->EnumAdapters1(argc > 1 ? std::atoi(argv[1]) : 0, &selected), "EnumAdapters1");
    const auto driver = warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_UNKNOWN;
    auto result = D3D11CreateDevice(selected.Get(), driver, nullptr, D3D11_CREATE_DEVICE_DEBUG,
        levels, 3, D3D11_SDK_VERSION, &device, &level, &commands);
    bool debug = SUCCEEDED(result);
    if (result == DXGI_ERROR_SDK_COMPONENT_MISSING)
        result = D3D11CreateDevice(selected.Get(), driver, nullptr, 0,
            levels, 3, D3D11_SDK_VERSION, &device, &level, &commands);
    Check(result, "D3D11CreateDevice");
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    Check(device.As(&dxgiDevice), "IDXGIDevice");
    Check(dxgiDevice->GetAdapter(&adapter), "GetAdapter");
    DXGI_ADAPTER_DESC adapterDesc{};
    Check(adapter->GetDesc(&adapterDesc), "GetDesc");
    std::printf("DX11 FSR %d.%d.%d; adapter=%ls; featureLevel=0x%x; debug=%d\n",
        FFX_FSR3_VERSION_MAJOR, FFX_FSR3_VERSION_MINOR, FFX_FSR3_VERSION_PATCH, adapterDesc.Description, level, debug);
    D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
    Check(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options)), "D3D11Options");
    std::printf("constantBufferOffsetting=%d noOverwriteConstantBuffers=%d\n", options.ConstantBufferOffsetting, options.MapNoOverwriteOnDynamicConstantBuffer);
    const auto scratchBytes = ffxGetScratchMemorySizeDX11(2);
    std::vector<unsigned char> scratch(scratchBytes);
    FfxInterface backend{};
    CheckFfx(ffxGetInterfaceDX11(&backend, ffxGetDeviceDX11_Fsr31(device.Get()), scratch.data(), scratch.size(), 2), "GetInterface");
    FfxDeviceCapabilities caps{};
    CheckFfx(backend.fpGetDeviceCapabilities(&backend, &caps), "GetCapabilities");
    std::printf("native fp16 support=%d\n", caps.fp16Supported);
    if (argc > 2 && std::string(argv[2]) == "fp32") {
        originalCapabilities = backend.fpGetDeviceCapabilities;
        backend.fpGetDeviceCapabilities = ForceFullPrecision;
        std::puts("forcing FP32 shader permutations through backend capabilities");
    }
    originalSchedule = backend.fpScheduleGpuJob;
    backend.fpScheduleGpuJob = TrackScheduledJobs;
    originalExecute = backend.fpExecuteGpuJobs;
    originalUnregister = backend.fpUnregisterResources;
    backend.fpExecuteGpuJobs = ExecuteWithFailure;
    backend.fpUnregisterResources = UnregisterWithFailure;
    std::array<Context, 2> eyes;
    FfxFsr3ContextDescription create{};
    create.flags = FFX_FSR3_ENABLE_UPSCALING_ONLY | FFX_FSR3_ENABLE_AUTO_EXPOSURE | FFX_FSR3_ENABLE_DYNAMIC_RESOLUTION;
    create.maxRenderSize = create.maxUpscaleSize = create.displaySize = {128,128};
    create.backendInterfaceUpscaling = backend;
    for (auto& eye : eyes) {
        CheckFfx(ffxFsr3ContextCreate(eye.value.get(), &create), "ContextCreate");
        eye.active = true;
    }
    const float depthValue = 0.5f;
    const std::array<float, 2> motionValue{0.0f,0.0f};
    const std::array<std::array<float, 4>, 2> colors{{{0.2f,0.4f,0.6f,1.0f},{0.6f,0.3f,0.1f,1.0f}}};
    auto depth = Texture(device.Get(), DXGI_FORMAT_R32_FLOAT, 4, &depthValue, "FSRSmoke::Depth");
    auto motion = Texture(device.Get(), DXGI_FORMAT_R32G32_FLOAT, 8, motionValue.data(), "FSRSmoke::Motion");
    std::array<ComPtr<ID3D11Texture2D>, 2> inputs;
    std::array<ComPtr<ID3D11Texture2D>, 2> outputs;
    for (size_t eye = 0; eye < 2; ++eye) {
        inputs[eye] = Texture(device.Get(), DXGI_FORMAT_R32G32B32A32_FLOAT, 16, colors[eye].data(), "FSRSmoke::Color");
        outputs[eye] = Texture(device.Get(), DXGI_FORMAT_R32G32B32A32_FLOAT, 16, nullptr, "FSRSmoke::Output");
    }
    auto readback = Texture(device.Get(), DXGI_FORMAT_R32G32B32A32_FLOAT, 16, nullptr, "FSRSmoke::Readback", true);
    const std::array<UINT, 6> renderSizes{64,48,64,40,80,64};
    const std::array<UINT, 6> outputSizes{96,128,80,96,128,128};
    unsigned initialClearJobs = 0;
    for (size_t frame = 0; frame < 128; ++frame) {
        for (size_t eye = 0; eye < 2; ++eye) {
            FfxFsr3DispatchUpscaleDescription dispatch{};
            dispatch.commandList = ffxGetCommandListDX11(commands.Get());
            dispatch.color = Resource(inputs[eye].Get(), L"FSRSmoke::Color");
            dispatch.depth = Resource(depth.Get(), L"FSRSmoke::Depth");
            dispatch.motionVectors = Resource(motion.Get(), L"FSRSmoke::Motion");
            dispatch.upscaleOutput = Resource(outputs[eye].Get(), L"FSRSmoke::Output", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
            dispatch.renderSize = {renderSizes[frame % renderSizes.size()],renderSizes[frame % renderSizes.size()]};
            dispatch.upscaleSize = {outputSizes[frame % outputSizes.size()],outputSizes[frame % outputSizes.size()]};
            dispatch.motionVectorScale = {static_cast<float>(renderSizes[frame % renderSizes.size()]),static_cast<float>(renderSizes[frame % renderSizes.size()])};
            dispatch.enableSharpening = frame % 2 == 1;
            dispatch.sharpness = 0.2f;
            dispatch.frameTimeDelta = 16.6667f;
            dispatch.preExposure = 1.0f;
            dispatch.reset = frame == 0;
            dispatch.cameraNear = 0.1f;
            dispatch.cameraFar = 1000.0f;
            dispatch.cameraFovAngleVertical = 1.0f;
            dispatch.viewSpaceToMetersFactor = 1.0f;
            dispatch.frameID = frame;
            if (frame == 127 && eye == 1) {
                CheckFailureReports([&] { return ffxFsr3ContextDispatchUpscale(eyes[eye].value.get(), &dispatch); });
            }
            const auto clearsBefore = clearJobs;
            CheckFfx(ffxFsr3ContextDispatchUpscale(eyes[eye].value.get(), &dispatch), "DispatchUpscale");
            if (frame == 0 && eye == 0) {
                initialClearJobs = clearJobs - clearsBefore;
                if (!initialClearJobs) throw std::runtime_error("initial history clear was not observed");
            }
            if (frame == 127 && eye == 1 && clearJobs - clearsBefore < initialClearJobs)
                throw std::runtime_error("failed dispatch did not reinitialize temporal history on recovery");
            commands->ClearState();
            if (frame % 8 != 7) continue;
            commands->CopyResource(readback.Get(), outputs[eye].Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            Check(commands->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped), "Map");
            float maxError = 0;
            bool finite = true;
            for (UINT y=0;y<outputSizes[frame % outputSizes.size()];++y) {
                const auto* row = reinterpret_cast<const float*>(static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch);
                for (UINT x=0;x<outputSizes[frame % outputSizes.size()];++x) for (size_t channel=0;channel<3;++channel) {
                    const float value=row[x*4+channel];
                    finite &= std::isfinite(value);
                    maxError=std::max(maxError,std::abs(value-colors[eye][channel]));
                }
            }
            commands->Unmap(readback.Get(),0);
            std::printf("frame=%zu eye=%zu render=%u output=%u sharpen=%d finite=%d maxColorError=%.6f\n",
                frame,eye,renderSizes[frame % renderSizes.size()],outputSizes[frame % outputSizes.size()],dispatch.enableSharpening,finite,maxError);
            if (!finite || maxError > 0.05f) throw std::runtime_error("output failed finite/constant-color check");
        }
    }
    auto reactiveOutput = Texture(device.Get(), DXGI_FORMAT_R32_FLOAT, 4, nullptr, "FSRSmoke::Reactive");
    FfxFsr3GenerateReactiveDescription reactive{};
    reactive.commandList = ffxGetCommandListDX11(commands.Get());
    reactive.colorOpaqueOnly = Resource(inputs[0].Get(), L"FSRSmoke::Color");
    reactive.colorPreUpscale = reactive.colorOpaqueOnly;
    reactive.outReactive = Resource(reactiveOutput.Get(), L"FSRSmoke::Reactive", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    reactive.renderSize = {128, 128};
    reactive.scale = 1.0f;
    CheckFailureReports([&] { return ffxFsr3ContextGenerateReactiveMask(eyes[0].value.get(), &reactive); });
    CheckFfx(ffxFsr3ContextGenerateReactiveMask(eyes[0].value.get(), &reactive), "GenerateReactiveMask recovery");
    commands->ClearState();
    for (auto& eye : eyes) {
        CheckFfx(ffxFsr3ContextDestroy(eye.value.get()), "ContextDestroy");
        eye.active = false;
    }
    ComPtr<ID3D11InfoQueue> info;
    unsigned errors=0;
    if (SUCCEEDED(device.As(&info))) {
        for (UINT64 i=0;i<info->GetNumStoredMessagesAllowedByRetrievalFilter();++i) {
            SIZE_T bytes=0;
            Check(info->GetMessage(i,nullptr,&bytes), "GetMessageSize");
            std::vector<unsigned char> storage(bytes);
            auto* message=reinterpret_cast<D3D11_MESSAGE*>(storage.data());
            Check(info->GetMessage(i,message,&bytes), "GetMessage");
            if (message->Severity <= D3D11_MESSAGE_SEVERITY_ERROR) {
                ++errors;
                std::printf("D3D11 ERROR: %s\n",message->pDescription);
            }
        }
    }
    Check(device->GetDeviceRemovedReason(), "DeviceRemovedReason");
    if (errors) throw std::runtime_error("D3D11 debug errors");
    std::puts("PASS: two contexts, 256 normal GPU dispatches, 6 injected upscale/reactive failure reports, queued uploads and ring wraps, resolution growth/shrink, RCAS, readback, teardown");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr,"FAIL: %s\n",e.what());
    return 1;
}
