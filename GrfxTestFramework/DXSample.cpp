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

using namespace Microsoft::WRL;
using namespace std;

DXSample::DXSample(UINT width, UINT height, std::wstring name) :
    m_width(width),
    m_height(height),
    m_windowBounds{ 0,0,0,0 },
    m_title(name),
    m_aspectRatio(0.0f),
    m_enableUI(true),
    m_numFrames(1),
    m_adapterIDoverride(UINT_MAX)
{
    WCHAR assetsPath[512];
    GetAssetsPath(assetsPath, _countof(assetsPath));
    m_assetsPath = assetsPath;

    UpdateForSizeChange(width, height);
}

DXSample::~DXSample()
{
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
    }

}

void DXSample::SetWindowBounds(int left, int top, int right, int bottom)
{
    m_windowBounds.left = static_cast<LONG>(left);
    m_windowBounds.top = static_cast<LONG>(top);
    m_windowBounds.right = static_cast<LONG>(right);
    m_windowBounds.bottom = static_cast<LONG>(bottom);
}