//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "pch.h"
#include "DXSample.h"
#include "DirectXRaytracingHelper.h"

using namespace Microsoft::WRL;
using namespace std;
using namespace DX;

DXSample::DXSample(UINT width, UINT height, std::wstring name) :
    m_width(width),
    m_height(height),
    m_windowBounds{ 0,0,0,0 },
    m_title(name),
    m_aspectRatio(0.0f),
    m_enableUI(true),
    m_dumpOutput(false),
    m_numFrames(1),
    m_adapterIDoverride(UINT_MAX),
    m_descriptorsAllocated(0),
    m_descriptorSize(0),
    m_raytracingOutputResourceUAVDescriptorHeapIndex(UINT_MAX)
{
    WCHAR assetsPath[512];
    GetAssetsPath(assetsPath, _countof(assetsPath));
    m_assetsPath = assetsPath;

    UpdateForSizeChange(width, height);
}

DXSample::~DXSample()
{
}

// Create 2D output texture for raytracing.
void DXSample::CreateRaytracingOutputResource(ID3D12DescriptorHeap* descHeap)
{
    auto device = m_deviceResources->GetD3DDevice();
    auto backbufferFormat = m_deviceResources->GetBackBufferFormat();

    // Create the output resource. The dimensions and format should match the swap-chain.
    auto uavDesc = CD3DX12_RESOURCE_DESC::Tex2D(backbufferFormat, m_width, m_height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    auto defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &uavDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_raytracingOutput)));
    NAME_D3D12_OBJECT(m_raytracingOutput);

    D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptorHandle;
    m_raytracingOutputResourceUAVDescriptorHeapIndex = AllocateDescriptor(descHeap, &uavDescriptorHandle, m_raytracingOutputResourceUAVDescriptorHeapIndex);
    D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
    UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(m_raytracingOutput.Get(), nullptr, &UAVDesc, uavDescriptorHandle);
    m_raytracingOutputResourceUAVGpuDescriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE(descHeap->GetGPUDescriptorHandleForHeapStart(), m_raytracingOutputResourceUAVDescriptorHeapIndex, m_descriptorSize);
}

// Allocate a descriptor and return its index. 
// If the passed descriptorIndexToUse is valid, it will be used instead of allocating a new one.
UINT DXSample::AllocateDescriptor(ID3D12DescriptorHeap* descriptorHeap, D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor, UINT descriptorIndexToUse)
{
    auto descriptorHeapCpuBase = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    if (descriptorIndexToUse >= descriptorHeap->GetDesc().NumDescriptors)
    {
        descriptorIndexToUse = m_descriptorsAllocated++;
    }
    *cpuDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCpuBase, descriptorIndexToUse, m_descriptorSize);
    return descriptorIndexToUse;
}

void DXSample::OnInit()
{
    m_deviceResources = std::make_unique<DeviceResources>(
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_UNKNOWN,
        FrameCount,
        D3D_FEATURE_LEVEL_11_0,
        // Sample shows handling of use cases with tearing support, which is OS dependent and has been supported since TH2.
        // Since the sample requires build 1809 (RS5) or higher, we don't need to handle non-tearing cases.
        DeviceResources::c_RequireTearingSupport,
        m_adapterIDoverride
        );
    m_deviceResources->RegisterDeviceNotify(this);
    m_deviceResources->SetWindow(Win32Application::GetHwnd(), m_width, m_height);
    m_deviceResources->InitializeDXGIAdapter();

    ThrowIfFalse(IsDirectXRaytracingSupported(m_deviceResources->GetAdapter()),
        L"ERROR: DirectX Raytracing is not supported by your OS, GPU and/or driver.\n\n");

 

    m_deviceResources->CreateDeviceResources();

    auto device = m_deviceResources->GetD3DDevice();
    m_descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_deviceResources->CreateWindowSizeDependentResources();

    // Create an output 2D texture to store the raytracing result to.
    //CreateRaytracingOutputResource(GetOutputDescriptorHeap());
}

void DXSample::GetTransform3x4Matrix(XMFLOAT3X4* transformMatrix,
    float scaleX,
    float scaleY,
    float scaleZ,
    float transformX,
    float transformY,
    float transformZ)
{
    const XMFLOAT3 a = XMFLOAT3(transformX, transformY, transformZ);
    const XMVECTOR vBasePosition = XMLoadFloat3(&a);
    XMMATRIX mScale = XMMatrixScaling(scaleX, scaleY, scaleZ);
    XMMATRIX mTranslation = XMMatrixTranslationFromVector(vBasePosition);
    XMMATRIX mTransform = mScale * mTranslation;
    XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(transformMatrix), mTransform);
}

void DXSample::UpdateForSizeChange(UINT clientWidth, UINT clientHeight)
{
    m_width = clientWidth;
    m_height = clientHeight;
    m_aspectRatio = static_cast<float>(clientWidth) / static_cast<float>(clientHeight);
}

// Helper function for resolving the full path of assets.
std::wstring DXSample::GetAssetFullPath(LPCWSTR assetName)
{
    return m_assetsPath + assetName;
}


// Helper function for setting the window's title text.
void DXSample::SetCustomWindowText(LPCWSTR text)
{
    std::wstring windowText = m_title + L": " + text;
    SetWindowText(Win32Application::GetHwnd(), windowText.c_str());
}

// Copy the raytracing output to the backbuffer.
void DXSample::CopyRaytracingOutputToBackbuffer()
{
    auto commandList = m_deviceResources->GetCommandList();
    auto renderTarget = m_deviceResources->GetRenderTarget();

    D3D12_RESOURCE_BARRIER preCopyBarriers[2];
    preCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
    preCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutput.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->ResourceBarrier(ARRAYSIZE(preCopyBarriers), preCopyBarriers);

    commandList->CopyResource(renderTarget, m_raytracingOutput.Get());

    if (m_dumpOutput == true)
    {
#ifdef ROUGH_COPY
        DxScreenShotBufferInfo* scBufInfo = m_deviceResources->GetScreenShotBufferInfo();
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = scBufInfo->m_stagingOutputResource.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = scBufInfo->footPrint;

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = m_raytracingOutput.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;

        commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
#endif // ROUGH_COPY
    }

    

    D3D12_RESOURCE_BARRIER postCopyBarriers[2];
    postCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(renderTarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    postCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutput.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    commandList->ResourceBarrier(ARRAYSIZE(postCopyBarriers), postCopyBarriers);
}

void DXSample::OnRender()
{
    static UINT frameNum = 0;
    frameNum++;
    if (!m_deviceResources->IsWindowVisible())
    {
        return;
    }

    m_deviceResources->Prepare();
    DoRaytracing();
    CopyRaytracingOutputToBackbuffer();

    m_deviceResources->Present(D3D12_RESOURCE_STATE_PRESENT);

    if (m_dumpOutput == true)
    {
#define COPY_Q
#ifdef COPY_Q
        ComPtr<ID3D12CommandAllocator> cmdAllocator;
        auto device = m_deviceResources->GetD3DDevice();
        auto cmdQueue = m_deviceResources->GetCommandQueue();
        ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAllocator)));

        ComPtr<ID3D12GraphicsCommandList> cmdList;
        ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAllocator.Get(), nullptr, IID_PPV_ARGS(&cmdList)));

        ComPtr<ID3D12Fence> fence;
        ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

        D3D12_RESOURCE_BARRIER preBarrier = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutput.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->ResourceBarrier(1, &preBarrier);

        DxScreenShotBufferInfo* scBufInfo = m_deviceResources->GetScreenShotBufferInfo();
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = scBufInfo->m_stagingOutputResource.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = scBufInfo->footPrint;

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = m_raytracingOutput.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;

        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        D3D12_RESOURCE_BARRIER postBarrier = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutput.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->ResourceBarrier(1, &postBarrier);

        cmdList->Close();

        ID3D12CommandList* ppCommandLists[] = { cmdList.Get() };
        cmdQueue->ExecuteCommandLists(1, ppCommandLists);

        cmdQueue->Signal(fence.Get(), 1);

        while (fence->GetCompletedValue() < 1)
        {
            SwitchToThread();
        }

#endif
        DxScreenShotBufferInfo* scBufferInfo = m_deviceResources->GetScreenShotBufferInfo();
        ID3D12Resource* pScreenShotRes = scBufferInfo->m_stagingOutputResource.Get();
        const UINT imgWidthInPixels = scBufferInfo->width;
        const UINT imgHeightInPixels = scBufferInfo->height;
        const UINT numRowsInFootprint = scBufferInfo->numRows;
        const UINT64 rowSizeInBuffer = scBufferInfo->rowSize;

        BYTE* pMappedData = NULL;
        HRESULT mapResult = pScreenShotRes->Map(0, nullptr, (void**)&pMappedData);

        // Remove the padding in the buffer. Can eliminate this step and combine it with BGRA to BMP Buffer step.
        if (mapResult == S_OK)
        {
            BYTE* bgraBuffer = (BYTE*)malloc(numRowsInFootprint * rowSizeInBuffer);

            for (UINT i = 0; i < numRowsInFootprint; i++)
            {
                memcpy(bgraBuffer + i * rowSizeInBuffer, pMappedData + i * scBufferInfo->footPrint.Footprint.RowPitch, rowSizeInBuffer);
            }

            UINT padding = 0;
            UINT scanlinebytes = imgWidthInPixels * 3;
            UINT newSize = 0;
            while ((scanlinebytes + padding) % 4 != 0)
            {
                padding++;
            }

            UINT psw = scanlinebytes + padding;
            newSize = psw * imgHeightInPixels;

            BYTE* bmpBuffer = (BYTE*)malloc(newSize);

            memset(bmpBuffer, 0, newSize);

            LONG bufpos = 0;
            LONG bmppos = 0;
            const UINT bgraBpp = 4;
            for (UINT y = 0; y < imgHeightInPixels; y++)
            {
                UINT x2 = 0;
                for (UINT x = 0; x < imgWidthInPixels; x++)
                {
                    bufpos = y * bgraBpp * imgWidthInPixels + x * bgraBpp;
                    bmppos = (imgHeightInPixels - y - 1) * psw + x2;
                    x2 += 3;

                    bmpBuffer[bmppos + 2] = bgraBuffer[bufpos];
                    bmpBuffer[bmppos + 1] = bgraBuffer[bufpos + 1];
                    bmpBuffer[bmppos + 0] = bgraBuffer[bufpos + 2];
                }
            }

            CHAR fullBmpPath[512];
            sprintf_s(fullBmpPath, 512, "D:\\Grinphx_Labs\\DXR_Screenshots\\Test_%u.bmp", frameNum);

            DX::DeviceResources::SaveImage(fullBmpPath, bmpBuffer, scBufferInfo->width, scBufferInfo->height);
            pScreenShotRes->Unmap(0, nullptr);
            free(bmpBuffer);
            free(bgraBuffer);
        }
    }
}

// Helper function for parsing any supplied command line args.
_Use_decl_annotations_
void DXSample::ParseCommandLineArgs(WCHAR* argv[], int argc)
{
    auto CheckCommandLineArg = [&](const wchar_t* arg, const wchar_t* option)->bool
    {
        return ((_wcsnicmp(arg, option, wcslen(option)) == 0 ||
                 _wcsnicmp(arg, option, wcslen(option)) == 0));
    };

    for (int i = 1; i < argc; ++i)
    {

        if (CheckCommandLineArg(argv[i], L"-disableUI"))
        {
            m_enableUI = false;
        }
        else if (CheckCommandLineArg(argv[i], L"-forceAdapter"))
        {
            ThrowIfFalse(i + 1 < argc, L"Incorrect argument format passed in.");

            m_adapterIDoverride = _wtoi(argv[i + 1]);
            i++;
        }
        else if (CheckCommandLineArg(argv[i], L"-f"))
        {
            m_numFrames = _wtoi(argv[i + 1]);
            i++;
        }
        else if (CheckCommandLineArg(argv[i], L"-image"))
        {
            m_dumpOutput = true;
        }
    }

}

void DXSample::SetWindowBounds(int left, int top, int right, int bottom)
{
    m_windowBounds.left = static_cast<LONG>(left);
    m_windowBounds.top = static_cast<LONG>(top);
    m_windowBounds.right = static_cast<LONG>(right);
    m_windowBounds.bottom = static_cast<LONG>(bottom);
}