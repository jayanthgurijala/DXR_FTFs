#include "pch.h"
#include "DXSample.h"
#include "FrameworkMain.h"

_Use_decl_annotations_
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
	DXSample* pSample =  CreateTestFunc(1024, 1024);
	int result = Win32Application::Run(pSample, hInstance, nCmdShow);
	delete pSample;
	return result;
}