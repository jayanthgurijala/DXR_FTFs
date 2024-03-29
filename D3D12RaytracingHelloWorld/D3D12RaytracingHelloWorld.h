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

#pragma once

#include "DXSample.h"
#include "StepTimer.h"
#include "RaytracingHlslCompat.h"
#include "DirectXRaytracingHelper.h"

using namespace std;
using namespace DirectX;

namespace GlobalRootSignatureParams {
    enum Value { 
        OutputViewSlot = 0,
        AccelerationStructureSlot,
        Count 
    };
}

namespace LocalRootSignatureParams {
    enum Value {
        ViewportConstantSlot = 0,
        Count 
    };
}

namespace LocalRootSignatureParamsAABB {
    enum Value {
        CircleConstantsSlot = 0,
        Count
    };
}

class D3D12RaytracingHelloWorld : public DXSample
{
public:
    D3D12RaytracingHelloWorld(UINT width, UINT height, std::wstring name);

    // IDeviceNotify
    virtual void OnDeviceLost() override;
    virtual void OnDeviceRestored() override;

    // Messages
    virtual void OnInit();
    virtual void OnUpdate();
    virtual void DoRaytracing();
    virtual void OnSizeChanged(UINT width, UINT height, bool minimized);
    virtual void OnDestroy();
    virtual IDXGISwapChain* GetSwapchain() { return m_deviceResources->GetSwapChain(); }
    virtual ID3D12DescriptorHeap* GetOutputDescriptorHeap();

private:
    // DirectX Raytracing (DXR) attributes
    ComPtr<ID3D12Device5> m_dxrDevice;
    ComPtr<ID3D12GraphicsCommandList4> m_dxrCommandList;
    ComPtr<ID3D12StateObject> m_dxrStateObject;

    // Root signatures
    ComPtr<ID3D12RootSignature> m_raytracingGlobalRootSignature;
    ComPtr<ID3D12RootSignature> m_raytracingLocalRootSignature;
    ComPtr<ID3D12RootSignature> m_raytracingLocalRootSigAABB_1;
    ComPtr<ID3D12RootSignature> m_raytracingLocalRootSigAABB_2;

    // Descriptors
    ComPtr<ID3D12DescriptorHeap> m_descriptorHeap;
    UINT m_descriptorSize;
    vector<DxTlasDesc> m_listOfTlasDesc;
    vector<DxBlasDesc> m_listOfBlasDesc;
    vector<GeomDesc> m_geomDescs;
    
    // Raytracing scene
    RayGenConstantBuffer m_rayGenCB;
    CircleAABBConstantBuffer m_aabbCircleCB;

    // Geometry
    typedef UINT16 Index;
    struct Vertex { float v1, v2, v3; };


    ComPtr<ID3D12Resource> m_indexBuffer[3];
    ComPtr<ID3D12Resource> m_vertexBuffer[3];
    ComPtr<ID3D12Resource> m_aabbBuffer;

    // Acceleration structure
    ComPtr<ID3D12Resource> m_accelerationStructure;
    vector<AccelerationStructureBuffers> m_listofBlasBuffersInfo;
    ComPtr<ID3D12Resource> m_topLevelAccelerationStructure;

    // Shader tables
    static const wchar_t* c_hitGroupName;
    static const wchar_t* c_hitGroupNameRed;
    static const wchar_t* c_raygenShaderName;
    static const wchar_t* c_closestHitShaderName;
    static const wchar_t* c_closestHitShaderNameRed;
    static const wchar_t* c_missShaderName;
    static const wchar_t* c_intersectionShaderName;
    static const wchar_t* c_hitGroupNameAABB_1;
    static const wchar_t* c_hitGroupNameAABB_2;
    static const wchar_t* c_closestHitIntersectionShaderName;

    ComPtr<ID3D12Resource> m_missShaderTable;
    ComPtr<ID3D12Resource> m_hitGroupShaderTable;
    ComPtr<ID3D12Resource> m_rayGenShaderTable;

    UINT64 m_hitGroupShaderStrideInBytes;
    
    // Application state
    StepTimer m_timer;

    void RecreateD3D();
    void CreateDeviceDependentResources();
    void CreateWindowSizeDependentResources();
    void ReleaseDeviceDependentResources();
    void ReleaseWindowSizeDependentResources();
    void CreateRaytracingInterfaces();
    void SerializeAndCreateRaytracingRootSignature(D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>* rootSig);
    void CreateRootSignatures();
    void CreateLocalRootSignatureSubobjects(CD3DX12_STATE_OBJECT_DESC* raytracingPipeline);
    void CreateRaytracingPipelineStateObject();
    void CreateDescriptorHeap();
    void BuildModelGeometry(ComPtr<ID3D12Resource> *vertexBuffer,
                            ComPtr<ID3D12Resource> *indexBuffer,
                            ModelGeometry geometry,
                            FLOAT scale,
                            FLOAT indexX,
                            FLOAT indexY,
                            FLOAT zPos);

    void BuildModelGeometryAABB(ComPtr<ID3D12Resource> *aabbBuffer,
                                FLOAT scale,
                                FLOAT indexX,
                                FLOAT indexY,
                                FLOAT zPos);
    
    void GetGeometryIndicesAndVertices(ModelGeometry geometry,
                                       UINT*         numVertices,
                                       UINT*         numIndices,
                                       Vertex**      vertices,
                                       Index**       indices,
                                       FLOAT         scale,
                                       FLOAT         indexX,
                                       FLOAT         indexY,
                                       FLOAT         zPos);
    
    void GetAABBBoundingBox(D3D12_RAYTRACING_AABB& aabbBox, FLOAT scale, FLOAT indexX, FLOAT indexY);
 
    void BuildAccelerationStructures();
    void BuildShaderTables();
    void UpdateForSizeChange(UINT clientWidth, UINT clientHeight);
    void CopyRaytracingOutputToBackbuffer();
    void CalculateFrameStats();
    void CreateTestCase();
    void CreateGeometry(FLOAT scale, FLOAT indexX, FLOAT indexY, FLOAT depth, BOOL autoIncrIndex);
};
