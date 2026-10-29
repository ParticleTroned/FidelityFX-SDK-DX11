// Compile the backend in this test to exercise private upload paths without
// exposing test controls through the SDK's public interface.
#include "../src/backends/dx11/ffx_dx11.cpp"
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
static void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
static void Check(HRESULT result)
{
    Require(SUCCEEDED(result), "D3D11 operation failed");
}
static void CheckDebug(ID3D11InfoQueue* info)
{
    for (UINT64 index = 0; index < info->GetNumStoredMessagesAllowedByRetrievalFilter(); ++index) {
        SIZE_T size = 0;
        Check(info->GetMessage(index, nullptr, &size));
        std::vector<unsigned char> bytes(size);
        auto* message = reinterpret_cast<D3D11_MESSAGE*>(bytes.data());
        Check(info->GetMessage(index, message, &size));
        if (message->Severity <= D3D11_MESSAGE_SEVERITY_ERROR) {
            std::fprintf(stderr, "%s\n", message->pDescription);
            throw std::runtime_error("unexpected D3D11 debug error");
        }
    }
    info->ClearStoredMessages();
}
struct BackendLifetime
{
    FfxInterface& backend;
    FfxUInt32 id;
    ~BackendLifetime() { backend.fpDestroyBackendContext(&backend, id); }
};
static void CheckQueue(FfxInterface& backend, BackendContext_DX11& state)
{
    FfxGpuJobDescription valid{};
    valid.jobType = FFX_GPU_JOB_COMPUTE;
    valid.computeJobDescriptor.pipeline.constCount = 1;
    valid.computeJobDescriptor.cbs[0].num32BitEntries = 1;
    valid.computeJobDescriptor.cbs[0].data[0] = 42;
    for (unsigned invalid = 0; invalid != 4; ++invalid) {
        Require(backend.fpScheduleGpuJob(&backend, &valid) == FFX_OK, "schedule valid job");
        auto bad = valid;
        switch (invalid) {
        case 0: bad.computeJobDescriptor.pipeline.constCount = FFX_MAX_NUM_CONST_BUFFERS + 1; break;
        case 1: bad.computeJobDescriptor.cbs[0].num32BitEntries = FFX_MAX_CONST_SIZE + 1; break;
        case 2: bad.computeJobDescriptor.cbs[0].num32BitEntries = 0; break;
        case 3: bad.computeJobDescriptor.pipeline.constantBufferBindings[0].slotIndex = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; break;
        }
        const auto expected = invalid == 3 ? FFX_ERROR_OUT_OF_RANGE : FFX_ERROR_INVALID_SIZE;
        Require(backend.fpScheduleGpuJob(&backend, &bad) == expected, "invalid constants accepted");
        Require(state.gpuJobCount == 1 && state.pGpuJobs[0].computeJobDescriptor.cbs[0].data[0] == 42,
            "invalid job modified the queue");
        Require(backend.fpScheduleGpuJob(&backend, &valid) == expected, "scheduling lost the batch failure");
        Require(backend.fpExecuteGpuJobs(&backend, nullptr) == expected, "execution lost the scheduling failure");
        Require(state.gpuJobCount == 0, "failed batch was not cleared");
    }
    for (unsigned index = 0; index < FFX_MAX_GPU_JOBS; ++index)
        Require(backend.fpScheduleGpuJob(&backend, &valid) == FFX_OK, "queue filled too early");
    valid.computeJobDescriptor.cbs[0].data[0] = 99;
    Require(state.pGpuJobs[0].computeJobDescriptor.cbs[0].data[0] == 42, "queued constants alias caller data");
    Require(backend.fpScheduleGpuJob(&backend, &valid) == FFX_ERROR_INSUFFICIENT_MEMORY, "queue overflow accepted");
    Require(backend.fpExecuteGpuJobs(&backend, nullptr) == FFX_ERROR_INSUFFICIENT_MEMORY, "queue overflow not propagated");
    Require(backend.fpExecuteGpuJobs(&backend, nullptr) == FFX_OK, "empty queue did not recover");
}
static void CheckUploads(ID3D11Device* device, ID3D11DeviceContext* commands, BackendContext_DX11& state)
{
    constexpr unsigned dispatchCount = 256;
    const char shader[] =
        "cbuffer First:register(b2){uint index;uint value;}"
        "cbuffer Second:register(b5){uint extra;}"
        "RWStructuredBuffer<uint> output:register(u0);"
        "[numthreads(1,1,1)] void main(){output[index]=value+extra;}";
    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    Check(D3DCompile(shader, sizeof(shader)-1, nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &bytecode, &errors));
    ComPtr<ID3D11ComputeShader> pipeline;
    Check(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &pipeline));
    SetNameDX11(pipeline.Get(), L"DX11Test::SparseConstantSlots");
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = dispatchCount * sizeof(uint32_t);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(uint32_t);
    ComPtr<ID3D11Buffer> output;
    Check(device->CreateBuffer(&desc, nullptr, &output));
    SetNameDX11(output.Get(), L"DX11Test::Output");
    ComPtr<ID3D11UnorderedAccessView> uav;
    Check(device->CreateUnorderedAccessView(output.Get(), nullptr, &uav));
    SetNameDX11(uav.Get(), L"DX11Test::Output::UAV");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = desc.StructureByteStride = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> readback;
    Check(device->CreateBuffer(&desc, nullptr, &readback));
    SetNameDX11(readback.Get(), L"DX11Test::Readback");
    commands->CSSetShader(pipeline.Get(), nullptr, 0);
    auto* view = uav.Get();
    commands->CSSetUnorderedAccessViews(0, 1, &view, nullptr);
    const UINT zeros[4]{};
    commands->ClearUnorderedAccessViewUint(view, zeros);

    FfxComputeJobDescription job{};
    job.pipeline.constCount = 2;
    job.pipeline.constantBufferBindings[0].slotIndex = 2;
    job.pipeline.constantBufferBindings[1].slotIndex = 5;
    job.cbs[1].num32BitEntries = FFX_MAX_CONST_SIZE;
    if (state.deviceContext1)
        state.constantBufferOffset[0] = state.constantBufferSize[0] - 256;
    for (unsigned index = 0; index < dispatchCount; ++index) {
        job.cbs[0].num32BitEntries = index % 2 ? FFX_MAX_CONST_SIZE : 2;
        job.cbs[0].data[0] = index;
        job.cbs[0].data[1] = index * 11 + 7;
        job.cbs[1].data[0] = index * 3 + 1;
        Require(uploadConstantBuffers(&state, job) == FFX_OK, "constant upload failed");
        commands->Dispatch(1, 1, 1);
    }
    commands->ClearState();
    commands->CopyResource(readback.Get(), output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(commands->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
    bool correct = true;
    for (unsigned index = 0; index < dispatchCount; ++index)
        correct &= static_cast<const uint32_t*>(mapped.pData)[index] == index * 14 + 8;
    commands->Unmap(readback.Get(), 0);
    Require(correct, "queued constant data or sparse binding slots corrupted GPU output");
}
static void CheckMapFailure(FfxInterface& backend, BackendContext_DX11& state, ID3D11InfoQueue* info)
{
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = 256;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> unmappable;
    Check(state.device->CreateBuffer(&desc, nullptr, &unmappable));
    SetNameDX11(unmappable.Get(), L"DX11Test::UnmappableConstants");
    auto* original = state.constantBufferResource[0];
    state.constantBufferResource[0] = unmappable.Get();
    FfxGpuJobDescription job{};
    job.jobType = FFX_GPU_JOB_COMPUTE;
    job.computeJobDescriptor.pipeline.constCount = 1;
    job.computeJobDescriptor.cbs[0].num32BitEntries = 1;
    const auto scheduled = backend.fpScheduleGpuJob(&backend, &job);
    const auto result = backend.fpExecuteGpuJobs(&backend, nullptr);
    state.constantBufferResource[0] = original;
    Require(scheduled == FFX_OK && result == FFX_ERROR_BACKEND_API_ERROR && state.gpuJobCount == 0,
        "Map failure must abort and clear the batch");
    bool sawMapFailure = false;
    for (UINT64 index = 0; index < info->GetNumStoredMessagesAllowedByRetrievalFilter(); ++index) {
        SIZE_T size = 0;
        Check(info->GetMessage(index, nullptr, &size));
        std::vector<unsigned char> bytes(size);
        auto* message = reinterpret_cast<D3D11_MESSAGE*>(bytes.data());
        Check(info->GetMessage(index, message, &size));
        if (message->Severity <= D3D11_MESSAGE_SEVERITY_ERROR) {
            Require(message->ID == D3D11_MESSAGE_ID_BUFFER_MAP_INVALIDMAPTYPE ||
                message->ID == D3D11_MESSAGE_ID_RESOURCE_MAP_INVALIDMAPTYPE, "unexpected error during Map failure injection");
            sawMapFailure = true;
        }
    }
    Require(sawMapFailure, "Map failure injection did not exercise the D3D11 error");
    info->ClearStoredMessages();
}
int main()
try {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> commands;
    const auto result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_DEBUG,
        nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &commands);
    if (result == DXGI_ERROR_SDK_COMPONENT_MISSING) {
        std::puts("SKIP: D3D11 debug layer is required");
        return 77;
    }
    Check(result);
    ComPtr<ID3D11InfoQueue> info;
    Check(device.As(&info));
    std::vector<unsigned char> scratch(ffxGetScratchMemorySizeDX11(1));
    FfxInterface backend{};
    Require(ffxGetInterfaceDX11(&backend, ffxGetDeviceDX11_Fsr31(device.Get()), scratch.data(), scratch.size(), 1) == FFX_OK,
        "backend interface creation failed");
    FfxUInt32 contextId = 0;
    Require(backend.fpCreateBackendContext(&backend, &contextId) == FFX_OK, "backend context creation failed");
    {
        BackendLifetime lifetime{backend, contextId};
        auto& state = *static_cast<BackendContext_DX11*>(backend.scratchBuffer);
        CheckQueue(backend, state);
        if (state.deviceContext1) {
            CheckUploads(device.Get(), commands.Get(), state);
            CheckDebug(info.Get());
            CheckMapFailure(backend, state, info.Get());
            state.deviceContext1->Release();
            state.deviceContext1 = nullptr;
            state.constantBufferResource[0]->Release();
            state.constantBufferResource[0] = nullptr;
            state.constantBufferSize[0] = state.constantBufferOffset[0] = 0;
        } else {
            std::puts("Ring-buffer route unavailable on this device; testing separate buffers");
        }
        CheckUploads(device.Get(), commands.Get(), state);
        CheckDebug(info.Get());
        CheckMapFailure(backend, state, info.Get());
        CheckUploads(device.Get(), commands.Get(), state);
        CheckDebug(info.Get());
    }
    CheckDebug(info.Get());
    Check(device->GetDeviceRemovedReason());
    std::puts("PASS: queue bounds, owned snapshots, sparse slots, ring wraps, separate buffers, Map failure and recovery, teardown");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
