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
ConstantBuffer<RayGenConstantBuffer> g_rayGenCB : register(b0);
ConstantBuffer<CircleAABBConstantBuffer> l_aabbCircleCB : register (b1);

typedef BuiltInTriangleIntersectionAttributes MyAttributes;
struct RayPayload
{
    float4 color;
};

bool IsInsideViewport(float2 p, Viewport viewport)
{
    return (p.x >= viewport.left && p.x <= viewport.right)
        && (p.y >= viewport.top && p.y <= viewport.bottom);
}

[shader("raygeneration")]
void MyRaygenShader()
{
    float2 lerpValues = (float2)DispatchRaysIndex() / (float2)DispatchRaysDimensions();

    // Orthographic projection since we're raytracing in screen space.
    float3 rayDir = float3(0, 0, 1);
    float3 origin = float3(
        lerp(g_rayGenCB.viewport.left, g_rayGenCB.viewport.right, lerpValues.x),
        lerp(g_rayGenCB.viewport.top, g_rayGenCB.viewport.bottom, lerpValues.y),
        0.0f);

    if (IsInsideViewport(origin.xy, g_rayGenCB.stencil))
    {
        // Trace the ray.
        // Set the ray's extents.
        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = rayDir;
        // Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
        // TMin should be kept small to prevent missing geometry at close contact areas.
        ray.TMin = 0.001;
        ray.TMax = 10000.0;
        RayPayload payload = { float4(0, 0, 0, 0) };
        TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);

        // Write the raytraced color to the output texture.
        RenderTarget[DispatchRaysIndex().xy] = payload.color;
    }
    else
    {
        // Render interpolated DispatchRaysIndex outside the stencil window
        RenderTarget[DispatchRaysIndex().xy] = float4(lerpValues, 0, 1);
    }
}

[shader("closesthit")]
void MyClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    payload.color = float4(1, 1, 0, 1);
}

[shader("closesthit")]
void MyClosestHitShaderRed(inout RayPayload payload, in MyAttributes attr)
{
    payload.color = float4(1, 0, 0, 1);
}

[shader("closesthit")]
void MyClosestHitIntersectionShader(inout RayPayload payload, in MyAttributes attr)
{
    uint hitKind = HitKind();
    if (hitKind == 0)
    {
        payload.color = l_aabbCircleCB.color;
    }
    else
    {
        payload.color = float4(0.1f, 0.2f, 0.4f, 1);
    }
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
    payload.color = float4(0.0f, 0.0f, 0.3f, 1);
}

[shader("intersection")]
void MyIntersectionShader()
{
    HelloWorldIntersectionAttrs attr = (HelloWorldIntersectionAttrs)0;

    //Center is 0,0
    float3 worldRayOrigin = WorldRayOrigin() + float3(l_aabbCircleCB.center[0], l_aabbCircleCB.center[1], l_aabbCircleCB.center[2]);
    //float3 worldRayOrigin = WorldRayOrigin() + float3(0.5f, -0.5f, -1.0f);

    // hard coded values need to be passed via constant buffer.
    float Radius = l_aabbCircleCB.radius;

    float sqRadius = Radius * Radius; 
    float sqX = worldRayOrigin.x * worldRayOrigin.x;
    float sqY = worldRayOrigin.y * worldRayOrigin.y;
    if (sqX + sqY < sqRadius )
    {
        ReportHit(0.1f, /*hitKind*/ 0, attr);
    }
    else
    {
        ReportHit(0.1f, /*hitKind*/ 1, attr);
    }
}

#endif // RAYTRACING_HLSL