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

#include "stdafx.h"
#include "D3D12RaytracingHelloWorld.h"
#include "DirectXRaytracingHelper.h"
#include "CompiledShaders\Raytracing.hlsl.h"
#include "RaytracingHlslCompat.h"


using namespace std;
using namespace DX;
using namespace DirectX;

const wchar_t* D3D12RaytracingHelloWorld::c_hitGroupName = L"MyHitGroup";
const wchar_t* D3D12RaytracingHelloWorld::c_hitGroupNameRed = L"MyHitGroupRed";
const wchar_t* D3D12RaytracingHelloWorld::c_hitGroupNameAABB = L"MyHitGroupAABB";
const wchar_t* D3D12RaytracingHelloWorld::c_raygenShaderName = L"MyRaygenShader";
const wchar_t* D3D12RaytracingHelloWorld::c_closestHitShaderName = L"MyClosestHitShader";
const wchar_t* D3D12RaytracingHelloWorld::c_closestHitShaderNameRed = L"MyClosestHitShaderRed";
const wchar_t* D3D12RaytracingHelloWorld::c_intersectionShaderName = L"MyIntersectionShader";
const wchar_t* D3D12RaytracingHelloWorld::c_closestHitIntersectionShaderName = L"MyClosestHitIntersectionShader";
const wchar_t* D3D12RaytracingHelloWorld::c_missShaderName = L"MyMissShader";

D3D12RaytracingHelloWorld::D3D12RaytracingHelloWorld(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    m_raytracingOutputResourceUAVDescriptorHeapIndex(UINT_MAX)
{
    m_rayGenCB.viewport = { -1.0f, -1.0f, 1.0f, 1.0f };
    UpdateForSizeChange(width, height);
}

void D3D12RaytracingHelloWorld::OnInit()
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
    m_deviceResources->CreateWindowSizeDependentResources();

    CreateDeviceDependentResources();
    CreateWindowSizeDependentResources();
}

// Create resources that depend on the device.
void D3D12RaytracingHelloWorld::CreateDeviceDependentResources()
{
    // Initialize raytracing pipeline.

    // Create raytracing interfaces: raytracing device and commandlist.
    CreateRaytracingInterfaces();

    // Create root signatures for the shaders.
    CreateRootSignatures();

    // Create a raytracing pipeline state object which defines the binding of shaders, state and resources to be used during raytracing.
    CreateRaytracingPipelineStateObject();

    // Create a heap for descriptors.
    CreateDescriptorHeap();

    // Build geometry to be used in the sample.
    BuildModelGeometry(&m_vertexBuffer[0], &m_indexBuffer[0], TriangleModel, 0.5, 0, 0, 0.2f);
    BuildModelGeometry(&m_vertexBuffer[1], &m_indexBuffer[1], SquareModel, 0.5, 1.0f, 0, 0.8f);
    BuildModelGeometryAABB(&m_aabbBuffer, 0.5, 0, 1.0f, 1.0f);

    CreateTestCase();

    // Build raytracing acceleration structures from the generated geometry.
    BuildAccelerationStructures();

    // Build shader tables, which define shaders and their local root arguments.
    BuildShaderTables();

    // Create an output 2D texture to store the raytracing result to.
    CreateRaytracingOutputResource();
}

void D3D12RaytracingHelloWorld::SerializeAndCreateRaytracingRootSignature(D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>* rootSig)
{
    auto device = m_deviceResources->GetD3DDevice();
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;

    ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error), error ? static_cast<wchar_t*>(error->GetBufferPointer()) : nullptr);
    ThrowIfFailed(device->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&(*rootSig))));
}

void D3D12RaytracingHelloWorld::CreateRootSignatures()
{
    // Global Root Signature
    // This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
    {
        CD3DX12_DESCRIPTOR_RANGE UAVDescriptor;
        UAVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        CD3DX12_ROOT_PARAMETER rootParameters[GlobalRootSignatureParams::Count];
        rootParameters[GlobalRootSignatureParams::OutputViewSlot].InitAsDescriptorTable(1, &UAVDescriptor);
        rootParameters[GlobalRootSignatureParams::AccelerationStructureSlot].InitAsShaderResourceView(0);
        CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters);
        SerializeAndCreateRaytracingRootSignature(globalRootSignatureDesc, &m_raytracingGlobalRootSignature);
    }

    // Local Root Signature
    // This is a root signature that enables a shader to have unique arguments that come from shader tables.
    {
        CD3DX12_ROOT_PARAMETER rootParameters[LocalRootSignatureParams::Count];
        rootParameters[LocalRootSignatureParams::ViewportConstantSlot].InitAsConstants(SizeOfInUint32(m_rayGenCB), 0, 0);
        CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters);
        localRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
        SerializeAndCreateRaytracingRootSignature(localRootSignatureDesc, &m_raytracingLocalRootSignature);
    }
}

// Create raytracing device and command list.
void D3D12RaytracingHelloWorld::CreateRaytracingInterfaces()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto commandList = m_deviceResources->GetCommandList();

    ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&m_dxrDevice)), L"Couldn't get DirectX Raytracing interface for the device.\n");
    ThrowIfFailed(commandList->QueryInterface(IID_PPV_ARGS(&m_dxrCommandList)), L"Couldn't get DirectX Raytracing interface for the command list.\n");
}

// Local root signature and shader association
// This is a root signature that enables a shader to have unique arguments that come from shader tables.
void D3D12RaytracingHelloWorld::CreateLocalRootSignatureSubobjects(CD3DX12_STATE_OBJECT_DESC* raytracingPipeline)
{
    // Hit group and miss shaders in this sample are not using a local root signature and thus one is not associated with them.

    // Local root signature to be used in a ray gen shader.
    {
        auto localRootSignature = raytracingPipeline->CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
        localRootSignature->SetRootSignature(m_raytracingLocalRootSignature.Get());
        // Shader association
        auto rootSignatureAssociation = raytracingPipeline->CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
        rootSignatureAssociation->SetSubobjectToAssociate(*localRootSignature);
        rootSignatureAssociation->AddExport(c_raygenShaderName);
    }
}

// Create a raytracing pipeline state object (RTPSO).
// An RTPSO represents a full set of shaders reachable by a DispatchRays() call,
// with all configuration options resolved, such as local signatures and other state.
void D3D12RaytracingHelloWorld::CreateRaytracingPipelineStateObject()
{
    // Create 7 subobjects that combine into a RTPSO:
    // Subobjects need to be associated with DXIL exports (i.e. shaders) either by way of default or explicit associations.
    // Default association applies to every exported shader entrypoint that doesn't have any of the same type of subobject associated with it.
    // This simple sample utilizes default shader association except for local root signature subobject
    // which has an explicit association specified purely for demonstration purposes.
    // 1 - DXIL library
    // 1 - Triangle hit group
    // 1 - Shader config
    // 2 - Local root signature and association
    // 1 - Global root signature
    // 1 - Pipeline config
    CD3DX12_STATE_OBJECT_DESC raytracingPipeline{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };


    // DXIL library
    // This contains the shaders and their entrypoints for the state object.
    // Since shaders are not considered a subobject, they need to be passed in via DXIL library subobjects.
    auto lib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE((void *)g_pRaytracing, ARRAYSIZE(g_pRaytracing));
    lib->SetDXILLibrary(&libdxil);
    // Define which shader exports to surface from the library.
    // If no shader exports are defined for a DXIL library subobject, all shaders will be surfaced.
    // In this sample, this could be omitted for convenience since the sample uses all shaders in the library. 
    {
        lib->DefineExport(c_raygenShaderName);
        lib->DefineExport(c_closestHitShaderName);
        lib->DefineExport(c_missShaderName);
        lib->DefineExport(c_closestHitShaderNameRed);
        lib->DefineExport(c_intersectionShaderName);
        lib->DefineExport(c_closestHitIntersectionShaderName);
    }
    
    // Triangle hit group
    // A hit group specifies closest hit, any hit and intersection shaders to be executed when a ray intersects the geometry's triangle/AABB.
    // In this sample, we only use triangle geometry with a closest hit shader, so others are not set.
    {
        auto hitGroup = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
        hitGroup->SetClosestHitShaderImport(c_closestHitShaderName);
        hitGroup->SetHitGroupExport(c_hitGroupName);
        hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
    }

    {
        auto hitGroup = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
        hitGroup->SetClosestHitShaderImport(c_closestHitShaderNameRed);
        hitGroup->SetHitGroupExport(c_hitGroupNameRed);
        hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

    }

    {
        auto hitGroup = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
        hitGroup->SetClosestHitShaderImport(c_closestHitIntersectionShaderName);
        hitGroup->SetIntersectionShaderImport(c_intersectionShaderName);
        hitGroup->SetHitGroupExport(c_hitGroupNameAABB);
        hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE);
    }
    
    // Shader config
    // Defines the maximum sizes in bytes for the ray payload and attribute structure.
    auto shaderConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    UINT payloadSize = 4 * sizeof(float);   // float4 color
    UINT attributeSize = 2 * sizeof(float); // float2 barycentrics
    shaderConfig->Config(payloadSize, attributeSize);

    // Local root signature and shader association
    CreateLocalRootSignatureSubobjects(&raytracingPipeline);
    // This is a root signature that enables a shader to have unique arguments that come from shader tables.

    // Global root signature
    // This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
    auto globalRootSignature = raytracingPipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    globalRootSignature->SetRootSignature(m_raytracingGlobalRootSignature.Get());

    // Pipeline config
    // Defines the maximum TraceRay() recursion depth.
    auto pipelineConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    // PERFOMANCE TIP: Set max recursion depth as low as needed 
    // as drivers may apply optimization strategies for low recursion depths. 
    UINT maxRecursionDepth = 1; // ~ primary rays only. 
    pipelineConfig->Config(maxRecursionDepth);

#if _DEBUG
    PrintStateObjectDesc(raytracingPipeline);
#endif

    // Create the state object.
    ThrowIfFailed(m_dxrDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_dxrStateObject)), L"Couldn't create DirectX Raytracing state object.\n");
}

// Create 2D output texture for raytracing.
void D3D12RaytracingHelloWorld::CreateRaytracingOutputResource()
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
    m_raytracingOutputResourceUAVDescriptorHeapIndex = AllocateDescriptor(&uavDescriptorHandle, m_raytracingOutputResourceUAVDescriptorHeapIndex);
    D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
    UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(m_raytracingOutput.Get(), nullptr, &UAVDesc, uavDescriptorHandle);
    m_raytracingOutputResourceUAVGpuDescriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_descriptorHeap->GetGPUDescriptorHandleForHeapStart(), m_raytracingOutputResourceUAVDescriptorHeapIndex, m_descriptorSize);
}

void D3D12RaytracingHelloWorld::CreateDescriptorHeap()
{
    auto device = m_deviceResources->GetD3DDevice();

    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
    // Allocate a heap for a single descriptor:
    // 1 - raytracing output texture UAV
    descriptorHeapDesc.NumDescriptors = 1; 
    descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    descriptorHeapDesc.NodeMask = 0;
    device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&m_descriptorHeap));
    NAME_D3D12_OBJECT(m_descriptorHeap);

    m_descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void D3D12RaytracingHelloWorld::GetAABBBoundingBox(D3D12_RAYTRACING_AABB &aabbBox, FLOAT scale, FLOAT indexX, FLOAT indexY)
{
    float translateX = -1 + scale + indexX * scale * 2;
    float translateY = -1 + scale + indexY * scale * 2;
    float border = 0.03f;

    float depthValue = 1.0;

    float offset = scale - border;

    aabbBox.MinX = -offset + translateX;
    aabbBox.MinY = -offset + translateY;
    aabbBox.MinZ = depthValue;
    aabbBox.MaxX = offset + translateX;
    aabbBox.MaxY = offset + translateY;
    aabbBox.MaxZ = depthValue;
}

void D3D12RaytracingHelloWorld::GetGeometryIndicesAndVertices(ModelGeometry geometry,
                                                              UINT* outNumVertices,
                                                              UINT* outNumIndices,
                                                              Vertex** outVertices,
                                                              Index** outIndices,
                                                              FLOAT scale,
                                                              FLOAT indexX,
                                                              FLOAT indexY,
                                                              FLOAT zPos)
{
    float translateX = -1 + scale + indexX * scale * 2;
    float translateY = -1 + scale + indexY * scale * 2;
    float border = 0.03f;

    float depthValue = zPos;

    float offset = scale - border;

    int   triNumVertices = 3;
    int   triNumIndices = 3;
    Index triIndices[] =
    {
        0, 1, 2
    };

    Vertex triVertices[] =
    {
        // The sample raytraces in screen space coordinates.
        // Since DirectX screen space coordinates are right handed (i.e. Y axis points down).
        // Define the vertices in counter clockwise order ~ clockwise in left handed.
        { 0, -offset, depthValue },
        { -offset, offset, depthValue },
        { offset, offset, depthValue }
    };

    int   sqNumVertices = 4;
    int   sqNumIndices = 6;
    Index sqIndices[] =
    {
        0, 1, 2, 3, 0, 2
    };

    Vertex sqVertices[] =
    {
        {-offset, -offset, depthValue},
        {-offset, offset, depthValue},
        {offset, offset, depthValue},
        {offset, -offset, depthValue}
    };

    int numVertices = 0;
    int numIndices = 0;
    Vertex* _vertices = nullptr;
    Index* _indices = nullptr;


    switch (geometry)
    {
        case TriangleModel:
            numVertices = triNumVertices;
            numIndices = triNumIndices;
            _vertices = triVertices;
            _indices = triIndices;
            break;
        case SquareModel:
        case AABBModel:
        default:
            numVertices = sqNumVertices;
            numIndices = sqNumIndices;
            _vertices = sqVertices;
            _indices = sqIndices;
    }

    Vertex *vertices = (Vertex*)(malloc(numVertices * sizeof(Vertex)));
    Index *indices = (Index*)(malloc(numIndices * sizeof(Index)));

    for (int i = 0; i < numIndices; i++)
    {
        indices[i] = _indices[i];
    }


    for (int i = 0; i < numVertices; i++)
    {
        vertices[i].v1 = _vertices[i].v1 + translateX;
        vertices[i].v2 = _vertices[i].v2 + translateY;
        vertices[i].v3 = _vertices[i].v3;
    }


    *outNumVertices = numVertices;
    *outNumIndices  = numIndices;
    *outVertices    = vertices;
    *outIndices     = indices;
}

void D3D12RaytracingHelloWorld::BuildModelGeometryAABB(ComPtr<ID3D12Resource>* aabbBuffer,
    FLOAT scale,
    FLOAT indexX,
    FLOAT indexY,
    FLOAT zPos)
{
    auto device = m_deviceResources->GetD3DDevice();

    D3D12_RAYTRACING_AABB aabbBox;
    GetAABBBoundingBox(aabbBox, scale, indexX, indexY);

    AllocateUploadBuffer(device, &aabbBox, sizeof(D3D12_RAYTRACING_AABB), aabbBuffer->GetAddressOf());
}

// Build geometry used in the sample.
void D3D12RaytracingHelloWorld::BuildModelGeometry(ComPtr<ID3D12Resource> *vertexBuffer,
                                                   ComPtr<ID3D12Resource> *indexBuffer,
                                                   ModelGeometry modelType,
                                                   FLOAT scale,
                                                   FLOAT indexX,
                                                   FLOAT indexY,
                                                   FLOAT zPos)
{
    auto device = m_deviceResources->GetD3DDevice();

    UINT numIndices = 0;
    UINT numVertices = 0;
    Vertex* vertices;
    Index* indices;
    GetGeometryIndicesAndVertices(modelType, &numVertices, &numIndices, &vertices, &indices, scale, indexX, indexY, zPos);

    AllocateUploadBuffer(device, vertices, numVertices * sizeof(Vertex), vertexBuffer->GetAddressOf());
    AllocateUploadBuffer(device, indices, numIndices * sizeof(Index), indexBuffer->GetAddressOf());

    free(vertices);
    free(indices);
}

void D3D12RaytracingHelloWorld::GetTransform3x4Matrix(XMFLOAT3X4* transformMatrix,
                                                      float scale,
                                                      float transformX,
                                                      float transformY,
                                                      float transformZ)
{
    const XMFLOAT3 a = XMFLOAT3(transformX, transformY, transformZ);
    const XMVECTOR vBasePosition = XMLoadFloat3(&a);
    XMMATRIX mScale = XMMatrixScaling(scale, scale, scale);
    XMMATRIX mTranslation = XMMatrixTranslationFromVector(vBasePosition);
    XMMATRIX mTransform = mScale * mTranslation;
    XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(transformMatrix), mTransform);
}

void D3D12RaytracingHelloWorld::CreateTestCase()
{

    auto AddGeometryDesc = [&](UINT index,
                               D3D12_RAYTRACING_GEOMETRY_TYPE geomType = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
                               D3D12_RAYTRACING_GEOMETRY_FLAGS flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE)
    {
        GeomDesc geomDesc;
        geomDesc.geomIndex = index;
        geomDesc.geomType = geomType;
        geomDesc.flags = flags;
        m_geomDescs.push_back(geomDesc);
    };

    auto AddBlasDesc = [&](const std::initializer_list<UINT>& geomIndices, UINT numIndices = 1, D3D12_RAYTRACING_GEOMETRY_TYPE type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES)
    {
        DxBlasDesc blasDesc;
        blasDesc.geomType = type;
        for (UINT x : geomIndices)
        {
            blasDesc.geomIndices.push_back(x);
        }
        m_listOfBlasDesc.push_back(blasDesc);
    };

    auto AddTlasDesc = [&](UINT blasIndex,
                           UINT hitIndexContribution = 0,
                           FLOAT scale = 1.0f,
                           FLOAT translateX = 0.0f,
                           FLOAT translateY = 0.0f,
                           FLOAT translateZ = 0.0f)
    {
        DxTlasDesc tlasDesc;
        tlasDesc.blasIndex = blasIndex;
        tlasDesc.instanceContributionToHitIndex = hitIndexContribution;
        GetTransform3x4Matrix(&tlasDesc.transformMatrix, scale, translateX, translateY, translateZ);
        m_listOfTlasDesc.push_back(tlasDesc);
    };

    AddGeometryDesc(0);
    AddGeometryDesc(1);
    AddGeometryDesc(0, D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS);

    AddBlasDesc({ 0 });
    AddBlasDesc({ 1 });
    AddBlasDesc({ 2 }, D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS);

    AddTlasDesc(0);
    AddTlasDesc(1, 1);
    AddTlasDesc(2, 2);
}

// Build acceleration structures needed for raytracing.
void D3D12RaytracingHelloWorld::BuildAccelerationStructures()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto commandList = m_deviceResources->GetCommandList();
    auto commandQueue = m_deviceResources->GetCommandQueue();
    auto commandAllocator = m_deviceResources->GetCommandAllocator();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    // Reset the command list for the acceleration structure construction.
    commandList->Reset(commandAllocator, nullptr);

    //D3D12_RAYTRACING_GEOMETRY_DESC* geometryDesc = (D3D12_RAYTRACING_GEOMETRY_DESC*)(malloc(m_geomDescs.capacity() * sizeof(D3D12_RAYTRACING_GEOMETRY_DESC)));
    vector<D3D12_RAYTRACING_GEOMETRY_DESC *> geometryDesc;
    geometryDesc.resize(0);
    D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;

    for (GeomDesc g : m_geomDescs)
    {
        D3D12_RAYTRACING_GEOMETRY_DESC* geomDesc = (D3D12_RAYTRACING_GEOMETRY_DESC*)malloc(sizeof(D3D12_RAYTRACING_GEOMETRY_DESC));
        geomDesc->Type = g.geomType;
        
        if (g.geomType == D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES)
        {
            ComPtr<ID3D12Resource> indexBuffer = m_indexBuffer[g.geomIndex];
            ComPtr<ID3D12Resource> vertexBuffer = m_vertexBuffer[g.geomIndex];
            geomDesc->Triangles.IndexBuffer = indexBuffer->GetGPUVirtualAddress();
            geomDesc->Triangles.IndexCount = static_cast<UINT>(indexBuffer->GetDesc().Width) / sizeof(Index);
            geomDesc->Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
            geomDesc->Triangles.Transform3x4 = 0;
            geomDesc->Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            geomDesc->Triangles.VertexCount = static_cast<UINT>(vertexBuffer->GetDesc().Width) / sizeof(Vertex);
            geomDesc->Triangles.VertexBuffer.StartAddress = vertexBuffer->GetGPUVirtualAddress();
            geomDesc->Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);
        }
        else //Type_AABB
        {
            geomDesc->AABBs.AABBCount = 1;
            geomDesc->AABBs.AABBs.StartAddress = m_aabbBuffer->GetGPUVirtualAddress();
            geomDesc->AABBs.AABBs.StrideInBytes = sizeof(D3D12_RAYTRACING_AABB);
        }

        // Mark the geometry as opaque. 
        // PERFORMANCE TIP: mark geometry as opaque whenever applicable as it can enable important ray processing optimizations.
        // Note: When rays encounter opaque geometry an any hit shader will not be executed whether it is present or not.
        geomDesc->Flags = g.flags;
        geometryDesc.push_back(geomDesc);
    }

    D3D12_RAYTRACING_GEOMETRY_DESC** geomDescPtrs = (D3D12_RAYTRACING_GEOMETRY_DESC**)malloc(sizeof(D3D12_RAYTRACING_GEOMETRY_DESC*) * m_geomDescs.capacity());

    //@todo Allocate one single buffer and chunk it.
    UINT64 totalBLASScratchSize = 0;
    UINT64 totalBLASResultSize = 0;

    m_listofBlasBuffersInfo.resize(m_listOfBlasDesc.capacity());

    UINT count = 0;
    for (DxBlasDesc blas : m_listOfBlasDesc)
    {
        AccelerationStructureBuffers &blasBufferInfo = m_listofBlasBuffersInfo[count];
        count++;
        for (UINT i = 0; i < blas.geomIndices.capacity(); i++)
        {
            const UINT geometryIndex = blas.geomIndices[i];
            geomDescPtrs[i] = geometryDesc.at(geometryIndex);
        }
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfo = {};
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottomLevelInputs;
        bottomLevelInputs.Flags = buildFlags;
        bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY_OF_POINTERS;
        bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        bottomLevelInputs.ppGeometryDescs = geomDescPtrs;
        bottomLevelInputs.NumDescs = blas.geomIndices.capacity();
        m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &bottomLevelPrebuildInfo);
        ThrowIfFalse(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

        AllocateUAVBuffer(device, bottomLevelPrebuildInfo.ScratchDataSizeInBytes, &blasBufferInfo.scratch, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        AllocateUAVBuffer(device, bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes, &blasBufferInfo.accelerationStructure, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

        totalBLASScratchSize += bottomLevelPrebuildInfo.ScratchDataSizeInBytes;
        totalBLASResultSize  += bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes;

        // Bottom Level Acceleration Structure desc
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc = {};
        bottomLevelBuildDesc.Inputs = bottomLevelInputs;
        bottomLevelBuildDesc.ScratchAccelerationStructureData = blasBufferInfo.scratch->GetGPUVirtualAddress();
        bottomLevelBuildDesc.DestAccelerationStructureData = blasBufferInfo.accelerationStructure->GetGPUVirtualAddress();
        m_dxrCommandList->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc, 0, nullptr);
    }

    count = 0;
    // Batch all resource barriers for bottom-level AS builds.
    UINT numResourceBarriers = m_listofBlasBuffersInfo.capacity();
    D3D12_RESOURCE_BARRIER* resourceBarriers = (D3D12_RESOURCE_BARRIER*)(malloc(numResourceBarriers * sizeof(D3D12_RESOURCE_BARRIER)));
    for (AccelerationStructureBuffers a : m_listofBlasBuffersInfo)
    {
        
        resourceBarriers[count] = CD3DX12_RESOURCE_BARRIER::UAV(a.accelerationStructure.Get());
        count++;
    }
    commandList->ResourceBarrier(numResourceBarriers, resourceBarriers);

 
    // Get required sizes for an acceleration structure.
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS topLevelInputs = {};
    topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    topLevelInputs.Flags = buildFlags;
    topLevelInputs.NumDescs = m_listOfTlasDesc.capacity();
    topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo = {};
    m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &topLevelPrebuildInfo);
    ThrowIfFalse(topLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

    ComPtr<ID3D12Resource> scratchResource;
    AllocateUAVBuffer(device, topLevelPrebuildInfo.ScratchDataSizeInBytes, &scratchResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"ScratchResource");

    // Allocate resources for acceleration structures.
    // Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent). 
    // Default heap is OK since the application doesn’t need CPU read/write access to them. 
    // The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
    // and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both: 
    //  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
    //  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.
    {
        D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
        AllocateUAVBuffer(device, topLevelPrebuildInfo.ResultDataMaxSizeInBytes, &m_topLevelAccelerationStructure, initialResourceState, L"TopLevelAccelerationStructure");
    }

    UINT numTlasInstances         = m_listOfTlasDesc.capacity();
    UINT sizeOfInstanceDescBuffer = numTlasInstances * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
    count                    = 0;
    ComPtr<ID3D12Resource> instanceDescs;
    D3D12_RAYTRACING_INSTANCE_DESC* listOfInstanceDesc = (D3D12_RAYTRACING_INSTANCE_DESC*)malloc(sizeOfInstanceDescBuffer);

    for (DxTlasDesc tlas : m_listOfTlasDesc)
    {
        D3D12_RAYTRACING_INSTANCE_DESC &instanceDesc = listOfInstanceDesc[count];
        memset(&instanceDesc, 0, sizeof(D3D12_RAYTRACING_INSTANCE_DESC));

        // Create an instance desc for the bottom-level acceleration structure.
        memcpy(&instanceDesc.Transform, &tlas.transformMatrix, sizeof(tlas.transformMatrix));

        instanceDesc.InstanceMask = ~0;
        instanceDesc.InstanceContributionToHitGroupIndex = tlas.instanceContributionToHitIndex;
        instanceDesc.AccelerationStructure = m_listofBlasBuffersInfo[tlas.blasIndex].accelerationStructure->GetGPUVirtualAddress();
        count++;
    }

    AllocateUploadBuffer(device, listOfInstanceDesc, sizeOfInstanceDescBuffer, &instanceDescs, L"InstanceDescs");

    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(m_listofBlasBuffersInfo[0].accelerationStructure.Get()));

    // Top Level Acceleration Structure desc
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelBuildDesc = {};
    {
        topLevelInputs.InstanceDescs = instanceDescs->GetGPUVirtualAddress();
        topLevelBuildDesc.Inputs = topLevelInputs;
        topLevelBuildDesc.DestAccelerationStructureData = m_topLevelAccelerationStructure->GetGPUVirtualAddress();
        topLevelBuildDesc.ScratchAccelerationStructureData = scratchResource->GetGPUVirtualAddress();
        m_dxrCommandList->BuildRaytracingAccelerationStructure(&topLevelBuildDesc, 0, nullptr);
    }
    
    // Kick off acceleration structure construction.
    m_deviceResources->ExecuteCommandList();

    // Wait for GPU to finish as the locally created temporary GPU resources will get released once we go out of scope.
    m_deviceResources->WaitForGpu();
}

// Build shader tables.
// This encapsulates all shader records - shaders and the arguments for their local root signatures.
void D3D12RaytracingHelloWorld::BuildShaderTables()
{
    auto device = m_deviceResources->GetD3DDevice();

    void* rayGenShaderIdentifier;
    void* missShaderIdentifier;
    void* hitGroupShaderIdentifier;
    void* hitGroupShaderIdentifierRed;
    void* hitGroupShaderIdentifierAABB;


    auto GetShaderIdentifiers = [&](auto* stateObjectProperties)
    {
        rayGenShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_raygenShaderName);
        missShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_missShaderName);
        hitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_hitGroupName);
        hitGroupShaderIdentifierRed = stateObjectProperties->GetShaderIdentifier(c_hitGroupNameRed);
        hitGroupShaderIdentifierAABB = stateObjectProperties->GetShaderIdentifier(c_hitGroupNameAABB);
    };

    // Get shader identifiers.
    UINT shaderIdentifierSize;
    {
        ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
        ThrowIfFailed(m_dxrStateObject.As(&stateObjectProperties));
        GetShaderIdentifiers(stateObjectProperties.Get());
        shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    }

    // Ray gen shader table
    {
        struct RootArguments {
            RayGenConstantBuffer cb;
        } rootArguments;
        rootArguments.cb = m_rayGenCB;

        UINT numShaderRecords = 1;
        UINT shaderRecordSize = shaderIdentifierSize + sizeof(rootArguments);
        ShaderTable rayGenShaderTable(device, numShaderRecords, shaderRecordSize, L"RayGenShaderTable");
        rayGenShaderTable.push_back(ShaderRecord(rayGenShaderIdentifier, shaderIdentifierSize, &rootArguments, sizeof(rootArguments)));
        m_rayGenShaderTable = rayGenShaderTable.GetResource();
    }

    // Miss shader table
    {
        UINT numShaderRecords = 1;
        UINT shaderRecordSize = shaderIdentifierSize;
        ShaderTable missShaderTable(device, numShaderRecords, shaderRecordSize, L"MissShaderTable");
        missShaderTable.push_back(ShaderRecord(missShaderIdentifier, shaderIdentifierSize));
        m_missShaderTable = missShaderTable.GetResource();
    }

    // Hit group shader table
    {
        UINT numShaderRecords = 3;
        UINT shaderRecordSize = shaderIdentifierSize;
        ShaderTable hitGroupShaderTable(device, numShaderRecords, shaderRecordSize, L"HitGroupShaderTable");
        hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderIdentifier, shaderIdentifierSize));
        hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderIdentifierRed, shaderIdentifierSize));
        hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderIdentifierAABB, shaderIdentifierSize));
        m_hitGroupShaderTable = hitGroupShaderTable.GetResource();
    }


}

// Update frame-based values.
void D3D12RaytracingHelloWorld::OnUpdate()
{
    m_timer.Tick();
    CalculateFrameStats();
}

void D3D12RaytracingHelloWorld::DoRaytracing()
{
    auto commandList = m_deviceResources->GetCommandList();
    
    auto DispatchRays = [&](auto* commandList, auto* stateObject, auto* dispatchDesc)
    {
        // Since each shader table has only one shader record, the stride is same as the size.
        dispatchDesc->HitGroupTable.StartAddress = m_hitGroupShaderTable->GetGPUVirtualAddress();
        dispatchDesc->HitGroupTable.SizeInBytes = m_hitGroupShaderTable->GetDesc().Width;
        dispatchDesc->HitGroupTable.StrideInBytes = dispatchDesc->HitGroupTable.SizeInBytes / 3;
        dispatchDesc->MissShaderTable.StartAddress = m_missShaderTable->GetGPUVirtualAddress();
        dispatchDesc->MissShaderTable.SizeInBytes = m_missShaderTable->GetDesc().Width;
        dispatchDesc->MissShaderTable.StrideInBytes = dispatchDesc->MissShaderTable.SizeInBytes;
        dispatchDesc->RayGenerationShaderRecord.StartAddress = m_rayGenShaderTable->GetGPUVirtualAddress();
        dispatchDesc->RayGenerationShaderRecord.SizeInBytes = m_rayGenShaderTable->GetDesc().Width;
        dispatchDesc->Width = m_width;
        dispatchDesc->Height = m_height;
        dispatchDesc->Depth = 1;
        commandList->SetPipelineState1(stateObject);
        commandList->DispatchRays(dispatchDesc);
    };

    commandList->SetComputeRootSignature(m_raytracingGlobalRootSignature.Get());

    // Bind the heaps, acceleration structure and dispatch rays.    
    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
    commandList->SetDescriptorHeaps(1, m_descriptorHeap.GetAddressOf());
    commandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::OutputViewSlot, m_raytracingOutputResourceUAVGpuDescriptor);
    commandList->SetComputeRootShaderResourceView(GlobalRootSignatureParams::AccelerationStructureSlot, m_topLevelAccelerationStructure->GetGPUVirtualAddress());
    DispatchRays(m_dxrCommandList.Get(), m_dxrStateObject.Get(), &dispatchDesc);
}

// Update the application state with the new resolution.
void D3D12RaytracingHelloWorld::UpdateForSizeChange(UINT width, UINT height)
{
    DXSample::UpdateForSizeChange(width, height);
    FLOAT border = 0.1f;
    if (m_width <= m_height)
    {
        m_rayGenCB.stencil =
        {
            -1 + border, -1 + border * m_aspectRatio,
            1.0f - border, 1 - border * m_aspectRatio
        };
    }
    else
    {
        m_rayGenCB.stencil =
        {
            -1 + border / m_aspectRatio, -1 + border,
             1 - border / m_aspectRatio, 1.0f - border
        };

    }
}

// Copy the raytracing output to the backbuffer.
void D3D12RaytracingHelloWorld::CopyRaytracingOutputToBackbuffer()
{
    auto commandList= m_deviceResources->GetCommandList();
    auto renderTarget = m_deviceResources->GetRenderTarget();

    D3D12_RESOURCE_BARRIER preCopyBarriers[2];
    preCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
    preCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutput.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->ResourceBarrier(ARRAYSIZE(preCopyBarriers), preCopyBarriers);

    commandList->CopyResource(renderTarget, m_raytracingOutput.Get());

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

    D3D12_RESOURCE_BARRIER postCopyBarriers[2];
    postCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(renderTarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    postCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutput.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    commandList->ResourceBarrier(ARRAYSIZE(postCopyBarriers), postCopyBarriers);
}

// Create resources that are dependent on the size of the main window.
void D3D12RaytracingHelloWorld::CreateWindowSizeDependentResources()
{
    CreateRaytracingOutputResource(); 

    // For simplicity, we will rebuild the shader tables.
    BuildShaderTables();
}

// Release resources that are dependent on the size of the main window.
void D3D12RaytracingHelloWorld::ReleaseWindowSizeDependentResources()
{
    m_rayGenShaderTable.Reset();
    m_missShaderTable.Reset();
    m_hitGroupShaderTable.Reset();
    m_raytracingOutput.Reset();
}

// Release all resources that depend on the device.
void D3D12RaytracingHelloWorld::ReleaseDeviceDependentResources()
{
    m_raytracingGlobalRootSignature.Reset();
    m_raytracingLocalRootSignature.Reset();

    m_dxrDevice.Reset();
    m_dxrCommandList.Reset();
    m_dxrStateObject.Reset();

    m_descriptorHeap.Reset();
    m_descriptorsAllocated = 0;
    m_raytracingOutputResourceUAVDescriptorHeapIndex = UINT_MAX;


    m_indexBuffer[0].Reset();
    m_vertexBuffer[0].Reset();

    m_indexBuffer[1].Reset();
    m_vertexBuffer[1].Reset();

    m_indexBuffer[2].Reset();
    m_vertexBuffer[2].Reset();

    m_accelerationStructure.Reset();
    m_topLevelAccelerationStructure.Reset();

    m_listOfTlasDesc.clear();
    m_listOfBlasDesc.clear();
}

void D3D12RaytracingHelloWorld::RecreateD3D()
{
    // Give GPU a chance to finish its execution in progress.
    try
    {
        m_deviceResources->WaitForGpu();
    }
    catch (HrException&)
    {
        // Do nothing, currently attached adapter is unresponsive.
    }
    m_deviceResources->HandleDeviceLost();
}

// Render the scene.
void D3D12RaytracingHelloWorld::OnRender()
{
    if (!m_deviceResources->IsWindowVisible())
    {
        return;
    }

    m_deviceResources->Prepare();
    DoRaytracing();
    CopyRaytracingOutputToBackbuffer();

    m_deviceResources->Present(D3D12_RESOURCE_STATE_PRESENT);

    DxScreenShotBufferInfo* scBufferInfo = m_deviceResources->GetScreenShotBufferInfo();
    ID3D12Resource* pScreenShotRes       = scBufferInfo->m_stagingOutputResource.Get();
    const UINT imgWidthInPixels          = scBufferInfo->width;
    const UINT imgHeightInPixels         = scBufferInfo->height;
    const UINT numRowsInFootprint        = scBufferInfo->numRows;
    const UINT64 rowSizeInBuffer         = scBufferInfo->rowSize;

    BYTE* pMappedData = NULL;
    HRESULT mapResult = pScreenShotRes->Map(0, nullptr, (void **)&pMappedData);

    // Remove the padding in the buffer. Can eliminate this step and combine it with BGRA to BMP Buffer step.
    if (mapResult == S_OK)
    {
        BYTE* bgraBuffer = (BYTE*)malloc(numRowsInFootprint * rowSizeInBuffer);

        for (UINT i = 0; i < numRowsInFootprint; i++)
        {
            memcpy(bgraBuffer + i * rowSizeInBuffer, pMappedData + i * scBufferInfo->footPrint.Footprint.RowPitch, rowSizeInBuffer);
        }

        UINT padding       = 0;
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
        
        DeviceResources::SaveImage("D:\\LearnDx12\\RayTracingFTFs\\Test.bmp", bmpBuffer, scBufferInfo->width, scBufferInfo->height);
        pScreenShotRes->Unmap(0, nullptr);
        free(bmpBuffer);
        free(bgraBuffer);
    }
    
}

void D3D12RaytracingHelloWorld::OnDestroy()
{
    // Let GPU finish before releasing D3D resources.
    m_deviceResources->WaitForGpu();
    OnDeviceLost();
}

// Release all device dependent resouces when a device is lost.
void D3D12RaytracingHelloWorld::OnDeviceLost()
{
    ReleaseWindowSizeDependentResources();
    ReleaseDeviceDependentResources();
}

// Create all device dependent resources when a device is restored.
void D3D12RaytracingHelloWorld::OnDeviceRestored()
{
    CreateDeviceDependentResources();
    CreateWindowSizeDependentResources();
}

// Compute the average frames per second and million rays per second.
void D3D12RaytracingHelloWorld::CalculateFrameStats()
{
    static int frameCnt = 0;
    static double elapsedTime = 0.0f;
    double totalTime = m_timer.GetTotalSeconds();
    frameCnt++;

    // Compute averages over one second period.
    if ((totalTime - elapsedTime) >= 1.0f)
    {
        float diff = static_cast<float>(totalTime - elapsedTime);
        float fps = static_cast<float>(frameCnt) / diff; // Normalize to an exact second.

        frameCnt = 0;
        elapsedTime = totalTime;

        float MRaysPerSecond = (m_width * m_height * fps) / static_cast<float>(1e6);

        wstringstream windowText;

        windowText << setprecision(2) << fixed
            << L"    fps: " << fps << L"     ~Million Primary Rays/s: " << MRaysPerSecond
            << L"    GPU[" << m_deviceResources->GetAdapterID() << L"]: " << m_deviceResources->GetAdapterDescription();
        SetCustomWindowText(windowText.str().c_str());
    }
}

// Handle OnSizeChanged message event.
void D3D12RaytracingHelloWorld::OnSizeChanged(UINT width, UINT height, bool minimized)
{
    if (!m_deviceResources->WindowSizeChanged(width, height, minimized))
    {
        return;
    }

    UpdateForSizeChange(width, height);

    ReleaseWindowSizeDependentResources();
    CreateWindowSizeDependentResources();
}

// Allocate a descriptor and return its index. 
// If the passed descriptorIndexToUse is valid, it will be used instead of allocating a new one.
UINT D3D12RaytracingHelloWorld::AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor, UINT descriptorIndexToUse)
{
    auto descriptorHeapCpuBase = m_descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    if (descriptorIndexToUse >= m_descriptorHeap->GetDesc().NumDescriptors)
    {
        descriptorIndexToUse = m_descriptorsAllocated++;
    }
    *cpuDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCpuBase, descriptorIndexToUse, m_descriptorSize);
    return descriptorIndexToUse;
}
