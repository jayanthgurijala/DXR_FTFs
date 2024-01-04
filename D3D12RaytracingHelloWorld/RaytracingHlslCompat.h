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

#ifndef RAYTRACINGHLSLCOMPAT_H
#define RAYTRACINGHLSLCOMPAT_H

#ifdef HLSL
#include "HlslCompat.h"
#else
using namespace DirectX;
typedef UINT16 Index;
#endif

struct Viewport
{
    float left;
    float top;
    float right;
    float bottom;
};

struct RayGenConstantBuffer
{
    Viewport viewport;
    Viewport stencil;
};

struct HelloWorldIntersectionAttrs
{
    float attr;
};

// Attributes per primitive type.
struct CircleAABBConstantBuffer
{
    float radius;
    XMFLOAT3 center;
    XMFLOAT4 color;
};

#endif // RAYTRACINGHLSLCOMPAT_H