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

#ifndef RAYTRACING_HLSL
#define RAYTRACING_HLSL

#define HLSL
#include "RaytracingHlslCompat.h"

RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget : register(u0);
ByteAddressBuffer Indices : register(t1, space0);
StructuredBuffer<Vertex> Vertices : register(t2, space0);

ConstantBuffer<SceneConstantBuffer> g_sceneCB : register(b0);
ConstantBuffer<CubeConstantBuffer> l_cubeCB : register(b1);

struct Ray
{
    float3 origin;
    float3 direction;
};

struct HelloWorldIntersectionAttrs
{
    float attr;
};

// Load three 16 bit indices from a byte addressed buffer.
uint3 Load3x16BitIndices(uint offsetBytes)
{
    uint3 indices;

    // ByteAdressBuffer loads must be aligned at a 4 byte boundary.
    // Since we need to read three 16 bit indices: { 0, 1, 2 } 
    // aligned at a 4 byte boundary as: { 0 1 } { 2 0 } { 1 2 } { 0 1 } ...
    // we will load 8 bytes (~ 4 indices { a b | c d }) to handle two possible index triplet layouts,
    // based on first index's offsetBytes being aligned at the 4 byte boundary or not:
    //  Aligned:     { 0 1 | 2 - }
    //  Not aligned: { - 0 | 1 2 }
    const uint dwordAlignedOffset = offsetBytes & ~3;    
    const uint2 four16BitIndices = Indices.Load2(dwordAlignedOffset);
 
    // Aligned: { 0 1 | 2 - } => retrieve first three 16bit indices
    if (dwordAlignedOffset == offsetBytes)
    {
        indices.x = four16BitIndices.x & 0xffff;
        indices.y = (four16BitIndices.x >> 16) & 0xffff;
        indices.z = four16BitIndices.y & 0xffff;
    }
    else // Not aligned: { - 0 | 1 2 } => retrieve last three 16bit indices
    {
        indices.x = (four16BitIndices.x >> 16) & 0xffff;
        indices.y = four16BitIndices.y & 0xffff;
        indices.z = (four16BitIndices.y >> 16) & 0xffff;
    }

    return indices;
}

typedef BuiltInTriangleIntersectionAttributes MyAttributes;

// Retrieve hit world position.
float3 HitWorldPosition()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

// Retrieve attribute at a hit position interpolated from vertex attributes using the hit's barycentrics.
float3 HitAttribute(float3 vertexAttribute[3], BuiltInTriangleIntersectionAttributes attr)
{
    return vertexAttribute[0] +
        attr.barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]) +
        attr.barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
}

// Generate a ray in world space for a camera pixel corresponding to an index from the dispatched 2D grid.
inline void GenerateCameraRay(uint2 index, out float3 origin, out float3 direction)
{
    float2 xy = index + 0.5f; // center in the middle of the pixel.
    float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0;

    // Invert Y for DirectX-style coordinates.
    screenPos.y = -screenPos.y;

    // Unproject the pixel coordinate into a ray.
    float4 world = mul(float4(screenPos, 0, 1), g_sceneCB.projectionToWorld);

    world.xyz /= world.w;
    origin = g_sceneCB.cameraPosition.xyz;
    direction = normalize(world.xyz - origin);
}

// Diffuse lighting calculation.
float4 CalculateDiffuseLighting(float3 hitPosition, float3 normal)
{
    float3 pixelToLight = normalize(g_sceneCB.lightPosition.xyz - hitPosition);

    // Diffuse contribution.
    float fNDotL = max(0.0f, dot(pixelToLight, normal));

    return l_cubeCB.albedo * fNDotL * 0.7f + l_cubeCB.albedo * 0.3f;
}

float4 TraceRadianceRay(in Ray ray, in UINT currentRayRecursionDepth)
{
    if (currentRayRecursionDepth > 2)
    {
        return float4(0, 0, 0, 0);
    }

    // Set the ray's extents.
    RayDesc rayDesc;
    rayDesc.Origin = ray.origin;
    rayDesc.Direction = ray.direction;
    rayDesc.TMin = 0;
    rayDesc.TMax = 10000;

    RayPayload rayPayload = { float4(0, 0, 0, 0), currentRayRecursionDepth + 1 };
    TraceRay(Scene, RAY_FLAG_NONE, ~0, 0, 1, 0, rayDesc, rayPayload);
    return rayPayload.color;
}

bool TraceShadowRayAndReportIfHit(in Ray ray, in UINT currentRecursionDepth)
{
    if (currentRecursionDepth > 2)
    {
        return false;
    }

    ShadowRayPayload shadowRayPayload = { false };

    // Set the ray's extents.
    RayDesc rayDesc;
    rayDesc.Origin = ray.origin;
    rayDesc.Direction = ray.direction;
    rayDesc.TMin = 0.5;
    rayDesc.TMax = 10000;
    TraceRay(Scene,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
        | RAY_FLAG_FORCE_OPAQUE             // ~skip any hit shaders
          , ~0, 1, 0, 1, rayDesc, shadowRayPayload);


    return shadowRayPayload.hit;
}

[shader("raygeneration")]
void MyRaygenShader()
{
    float3 rayDir;
    float3 origin;
    
    // Generate a ray for a camera pixel corresponding to an index from the dispatched 2D grid.
    GenerateCameraRay(DispatchRaysIndex().xy, origin, rayDir);

    // Trace the ray.
    // Set the ray's extents.
    Ray ray;
    ray.origin = origin;
    ray.direction = rayDir;

    UINT currentRecusrionDepth = 0;

    // Write the raytraced color to the output texture.
    RenderTarget[DispatchRaysIndex().xy] = TraceRadianceRay(ray, currentRecusrionDepth);
}

[shader("closesthit")]
void FloorClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    float3 hitPosition = HitWorldPosition();

    // Get the base index of the triangle's first 16 bit index.
    uint indexSizeInBytes = 2;
    uint indicesPerTriangle = 3;
    uint triangleIndexStride = indicesPerTriangle * indexSizeInBytes;
    uint baseIndex = PrimitiveIndex() * triangleIndexStride;

    // Load up 3 16 bit indices for the triangle.
    const uint3 indices = Load3x16BitIndices(baseIndex);

    // Retrieve corresponding vertex normals for the triangle vertices.
    float3 vertexNormals[3] = { 
        Vertices[indices[0]].normal, 
        Vertices[indices[1]].normal, 
        Vertices[indices[2]].normal 
    };

    // Compute the triangle's normal.
    // This is redundant and done for illustration purposes 
    // as all the per-vertex normals are the same and match triangle's normal in this sample. 
    float3 triangleNormal = HitAttribute(vertexNormals, attr);

    float4 diffuseColor = CalculateDiffuseLighting(hitPosition, triangleNormal);
    float4 color = diffuseColor;

    Ray shadowRay = { hitPosition, normalize(g_sceneCB.lightPosition.xyz - hitPosition) };
    
    // true if a geometry is hit and light is obstruced = Shadow
    bool hitgeom = TraceShadowRayAndReportIfHit(shadowRay, payload.recursionDepth);
    payload.color = color - color * hitgeom * 0.4f;
}

[shader("closesthit")]
void CubeClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    float3 hitPosition = HitWorldPosition();

    // Get the base index of the triangle's first 16 bit index.
    uint indexSizeInBytes = 2;
    uint indicesPerTriangle = 3;
    uint triangleIndexStride = indicesPerTriangle * indexSizeInBytes;
    uint baseIndex = PrimitiveIndex() * triangleIndexStride;

    // Load up 3 16 bit indices for the triangle.
    const uint3 indices = Load3x16BitIndices(baseIndex);

    // Retrieve corresponding vertex normals for the triangle vertices.
    float3 vertexNormals[3] = {
        Vertices[indices[0]].normal,
        Vertices[indices[1]].normal,
        Vertices[indices[2]].normal
    };

    // Compute the triangle's normal.
    // This is redundant and done for illustration purposes 
    // as all the per-vertex normals are the same and match triangle's normal in this sample. 
    float3 triangleNormal = HitAttribute(vertexNormals, attr);

    float4 diffuseColor = CalculateDiffuseLighting(hitPosition, triangleNormal);
    float4 color = diffuseColor;

    Ray shadowRay = { hitPosition, normalize(g_sceneCB.lightPosition.xyz - hitPosition) };

    // true if a geometry is hit and light is obstruced = Shadow
    bool hitgeom = TraceShadowRayAndReportIfHit(shadowRay, payload.recursionDepth);
    payload.color = color - color * hitgeom * 0.1f;
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
    float4 background = float4(0.0f, 0.2f, 0.4f, 1.0f);
    payload.color = background;
}

[shader("miss")]
void MyMissShader_Shadow(inout ShadowRayPayload payload)
{
    payload.hit = false;
}

[shader("closesthit")]
void FloorClosestHitShader_Shadow(inout ShadowRayPayload payload, in MyAttributes attr)
{
    payload.hit = true;
}

[shader("closesthit")]
void CubeClosestHitShader_Shadow(inout ShadowRayPayload payload, in MyAttributes attr)
{
    payload.hit = true;
}

[shader("intersection")]
void SphereIntersectionShader()
{
    HelloWorldIntersectionAttrs attr = (HelloWorldIntersectionAttrs)0;
    float3 rayOrigin = ObjectRayOrigin();
    float3 rayDirection = ObjectRayDirection();
    float sphereRadius = 1.0f;

    float3 oc = rayOrigin - float3(0, 0, 0);// sphere.center;
    float a = dot(rayDirection, rayDirection);
    float b = 2.0 * dot(oc, rayDirection);
    float c = dot(oc, oc) - (sphereRadius * sphereRadius);
    float discriminant = b * b - 4 * a * c;
    // Find the nearest intersection point along the ray
    float t0 = (-b - sqrt(discriminant)) / (2.0 * a);
    float t1 = (-b + sqrt(discriminant)) / (2.0 * a);
    float t = (t0 < t1) ? t0 : t1;
    float3 normal = normalize(rayOrigin + t * rayDirection); //- sphereCenter
    float3 lightDirection = float3(0, 0, -1);
    float diffuseLight = dot(normal, lightDirection);
    attr.attr = diffuseLight;
    ReportHit(t, 0, attr);
}


[shader("closesthit")]
void SphereClosestHitShader(inout RayPayload payload, HelloWorldIntersectionAttrs attr)
{
    payload.color = float4(0.8f, 0.3f, 0.3f, 1.0f) * attr.attr + float4(0.2f, 0.1f, 0.2f, 1.0f);
}

#endif // RAYTRACING_HLSL