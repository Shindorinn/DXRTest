#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers.

#define internal static // per translation unit
#define global_variable static
#define local_persist static

#include <windows.h>
#include <combaseapi.h>
#include <cstdio>

#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>

#include "include/d3dx12.h"
#include "include/d3d12_1.h"
#include "include/D3D12RaytracingFallback.h"
#include "include/D3D12RaytracingPrototypeHelpers.hpp"
#include "include/d3d12video.h"
#include "include/dxcapi.h"

global_variable bool running;

global_variable constexpr uint32_t width = 800;
global_variable constexpr uint32_t height = 800;
global_variable constexpr uint32_t swapchainBufferCount = 3;

// Main message handler for the sample.
LRESULT CALLBACK windowProcedureCallback(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{

    LRESULT result = 0;

    switch (message)
    {
        //case WM_SIZE :
        //{
        //} break;
 
        case WM_DESTROY:
        case WM_CLOSE :
        {
            PostQuitMessage(0);
        } break;
 
        //case WM_ACTIVATEAPP :
        //{
        //} break;

        case WM_KEYDOWN:
        {
            PostQuitMessage(0);
        } break;

        //case WM_KEYUP:
        //{
        //} break;

        //case WM_PAINT:
        //{
        //} break;

        default :
        {
            result = DefWindowProc(window, message, wParam, lParam);
        } break;
    }
    return result;

    // Handle any messages the switch statement didn't.
}

#include<xinput.h>

#define X_INPUT_GET_STATE(name) DWORD WINAPI name(DWORD dwuserIndex, XINPUT_STATE *pState)
typedef X_INPUT_GET_STATE(x_input_get_state);
X_INPUT_GET_STATE(XInputGetStateStub) { return 0; }
x_input_get_state *XInputGetState_ = XInputGetStateStub;
#define XInputGetState XInputGetState_

#define X_INPUT_SET_STATE(name) DWORD WINAPI name(DWORD dwuserIndex, XINPUT_VIBRATION *pVibration)
typedef X_INPUT_SET_STATE(x_input_set_state);
X_INPUT_SET_STATE(XInputSetStateStub) { return 0; }
x_input_set_state *XInputSetState_ = XInputSetStateStub;
#define XInputSetState XInputSetState_

internal void Win32LoadXInput(void)
{
    HMODULE xInputLibrary = LoadLibrary("xinput1_3.dll");
    if (xInputLibrary)
    {
        XInputGetState = (x_input_get_state *)GetProcAddress(xInputLibrary, "XInputGetState");
        XInputSetState = (x_input_set_state *)GetProcAddress(xInputLibrary, "XInputSetState");
    }
}

struct D3D12Test {
    uint32_t dxgiFactoryFlags;
    uint32_t frameIndex; // TODO(Dustin) : Possibly remove this in favor of using this in the render code

    ID3D12Debug* dxDebug;
    IDXGIFactory5* dxgiFactory;
    IDXGIAdapter* gpuAdapter;
    ID3D12Device* gpuDevice;

    ID3D12CommandAllocator* cmdAlloc;
    D3D12_COMMAND_QUEUE_DESC commandQueueDsc;
    ID3D12CommandQueue* commandQueue;

    DXGI_SWAP_CHAIN_DESC1 swapchainDsc;
    IDXGISwapChain3* swapchain;

    uint32_t rtvDscHeapSize;
    ID3D12DescriptorHeap* rtvDscHeap;
    ID3D12Resource* swapchainFrames[swapchainBufferCount];
};

#define QUERY_INTERFACE(A,B) A->QueryInterface(__uuidof(decltype(A)),(void**)&B)

D3D12Test initD3D12(HWND window)
{
    D3D12Test toReturn = {};

    // TODO(Dustin)  : DEBUG preprocessor flag?
    if ((D3D12GetDebugInterface(IID_PPV_ARGS(&toReturn.dxDebug))))
    {
        toReturn.dxDebug->EnableDebugLayer();
        toReturn.dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }

    IDXGIInfoQueue* dxgiInfoQueue;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiInfoQueue))))
    {
        IDXGIFactory2* tmpFactory;
        CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&tmpFactory));
        //tmpFactory->QueryInterface(__uuidof(IDXGIFactory5), (void**)&toReturn.dxgiFactory);
        QUERY_INTERFACE(tmpFactory, toReturn.dxgiFactory);

        dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
        dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
    }

    // Create d3d12 factory
    //CreateDXGIFactory2(toReturn.dxgiFactoryFlags, IID_PPV_ARGS(&toReturn.dxgiFactory));

    // Retrieve gpu
    toReturn.dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&toReturn.gpuAdapter));

    // Enable Experimental Features
    UUID experimentalFeatures[] = { D3D12ExperimentalShaderModels };
    HRESULT result;
    if (!SUCCEEDED(result = D3D12EnableExperimentalFeatures(1, experimentalFeatures, nullptr, nullptr)))
    {
        exit(-1);
    };

    D3D12CreateDevice(
        toReturn.gpuAdapter,
        D3D_FEATURE_LEVEL_12_1,
        IID_PPV_ARGS(&toReturn.gpuDevice)
    );

    ID3D12RaytracingFallbackDevice* fallbackDevice;
    HRESULT createRaytracingDeviceRes;
    if (!SUCCEEDED(createRaytracingDeviceRes = D3D12CreateRaytracingFallbackDevice(toReturn.gpuDevice, ForceComputeFallback | EnableRootDescriptorsInShaderRecords, 0, IID_PPV_ARGS(&fallbackDevice))))
    {

    }

    // Create the command queue
    toReturn.commandQueueDsc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    toReturn.commandQueueDsc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    toReturn.gpuDevice->CreateCommandQueue(&toReturn.commandQueueDsc, IID_PPV_ARGS(&toReturn.commandQueue));

    // Create the swapchain
    toReturn.swapchainDsc.Width = width;
    toReturn.swapchainDsc.Height = height;
    toReturn.swapchainDsc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    toReturn.swapchainDsc.SampleDesc.Count = 1;
    toReturn.swapchainDsc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    toReturn.swapchainDsc.BufferCount = swapchainBufferCount;
    toReturn.swapchainDsc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    IDXGISwapChain1* tmpSwapchain;
    toReturn.dxgiFactory->CreateSwapChainForHwnd(
        toReturn.commandQueue,
        window,
        &toReturn.swapchainDsc,
        nullptr,
        nullptr,
        &tmpSwapchain
    );
    if (!SUCCEEDED(QUERY_INTERFACE(tmpSwapchain, toReturn.swapchain)))
    {
        OutputDebugStringA("Swapchain casting failed.\n");
        exit(-1);
    };

    toReturn.frameIndex = toReturn.swapchain->GetCurrentBackBufferIndex();

    // Create Descriptor Heap 
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDsc = {};
        heapDsc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDsc.NumDescriptors = swapchainBufferCount;
        heapDsc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        toReturn.gpuDevice->CreateDescriptorHeap(&heapDsc, IID_PPV_ARGS(&toReturn.rtvDscHeap));
        toReturn.rtvDscHeapSize = toReturn.gpuDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvDscHandle(toReturn.rtvDscHeap->GetCPUDescriptorHandleForHeapStart());

        // Create a RTV for each frame.
        for (uint32_t frame = 0; frame < swapchainBufferCount; frame++)
        {
            toReturn.swapchain->GetBuffer(frame, IID_PPV_ARGS(&toReturn.swapchainFrames[frame]));
            toReturn.gpuDevice->CreateRenderTargetView(toReturn.swapchainFrames[frame], nullptr, rtvDscHandle);
            rtvDscHandle.Offset(1, toReturn.rtvDscHeapSize);
        }

    }

    toReturn.gpuDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&toReturn.cmdAlloc));
    return toReturn;
}



int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    Win32LoadXInput();
    // Initialize the window class.
    WNDCLASS windowClass = { 0 };
    windowClass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProcedureCallback;
    windowClass.hInstance = hInstance;
    windowClass.lpszClassName = "TestDXWindowClass";
    RegisterClass(&windowClass);

    // Create the window and store a handle to it.
    HWND windowHandle = CreateWindowEx(
        0,
        windowClass.lpszClassName,
        "directx12test",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        width,
        height,
        nullptr,		// We have no parent window.
        nullptr,		// We aren't using menus.
        hInstance,
        0);

    D3D12Test test = initD3D12(windowHandle);

    if (windowHandle)
    {
        HDC DeviceContext = GetDC(windowHandle);
        running = true;
        while (running)
        {
            // event loop
            MSG message;
            while(PeekMessage(&message, 0, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT) running = false;
                TranslateMessage(&message);
                DispatchMessageA(&message);
            }

            // Polling controllers
            for (uint32_t controllerIndex = 0;
                controllerIndex < XUSER_MAX_COUNT;
                ++controllerIndex)
            {
                XINPUT_STATE controllerState;
                if (XInputGetState(controllerIndex, &controllerState) == ERROR_SUCCESS)
                {
                    XINPUT_GAMEPAD *controller = &controllerState.Gamepad;
                    bool buttonUp           =     (controller->wButtons & XINPUT_GAMEPAD_DPAD_UP);
                    bool buttonDown         =     (controller->wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
                    bool buttonLeft         =     (controller->wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
                    bool buttonRight        =     (controller->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
                    bool buttonStart        =     (controller->wButtons & XINPUT_GAMEPAD_START);
                    bool buttonBack         =     (controller->wButtons & XINPUT_GAMEPAD_BACK);
                    bool buttonLThumb       =     (controller->wButtons & XINPUT_GAMEPAD_LEFT_THUMB);
                    bool buttonRThumb       =     (controller->wButtons & XINPUT_GAMEPAD_RIGHT_THUMB);
                    bool buttonLShoulder    =     (controller->wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER);
                    bool buttonRShoulder    =     (controller->wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
                    bool buttonA            =     (controller->wButtons & XINPUT_GAMEPAD_A);
                    bool buttonB            =     (controller->wButtons & XINPUT_GAMEPAD_B);
                    bool buttonX            =     (controller->wButtons & XINPUT_GAMEPAD_X);
                    bool buttonY            =     (controller->wButtons & XINPUT_GAMEPAD_Y);

                    int16_t lStickX = controller->sThumbLX;
                    int16_t lStickY = controller->sThumbLY;
                    int16_t rStickX = controller->sThumbRX;
                    int16_t rStickY = controller->sThumbRY;

                    if(buttonUp) OutputDebugStringA("buttonUp\n");
                    if(buttonDown) OutputDebugStringA("buttonDown\n");
                    if(buttonLeft) OutputDebugStringA("buttonLeft\n");
                    if(buttonRight) OutputDebugStringA("buttonRight\n");

                }
                else
                {
                }
            }
            
            // rest of the frame
            // ....
        }
    }
    else {
        //printf("FAIL");
    }

}
