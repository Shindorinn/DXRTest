#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers.

#define global_variable static
#define local_persist static

#include <windows.h>
#include <combaseapi.h>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <vector>
#include <array>

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

static void Win32LoadXInput(void)
{
    HMODULE xInputLibrary = LoadLibrary("xinput1_3.dll");
    if (xInputLibrary)
    {
        XInputGetState = (x_input_get_state *)GetProcAddress(xInputLibrary, "XInputGetState");
        XInputSetState = (x_input_set_state *)GetProcAddress(xInputLibrary, "XInputSetState");
    }
}

// Originally from DirectXRaytracingHelper.h from the DirectXSamples collection
// Pretty-print a state object tree.
inline void PrintStateObjectDesc(const D3D12_STATE_OBJECT_DESC* desc)
{
    std::wstringstream wstr;
    wstr << L"\n";
    wstr << L"--------------------------------------------------------------------\n";
    wstr << L"| D3D12 State Object 0x" << static_cast<const void*>(desc) << L": ";
    if (desc->Type == D3D12_STATE_OBJECT_TYPE_COLLECTION) wstr << L"Collection\n";
    if (desc->Type == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE) wstr << L"Raytracing Pipeline\n";

    auto ExportTree = [](UINT depth, UINT numExports, const D3D12_EXPORT_DESC* exports)
    {
        std::wostringstream woss;
        for (UINT i = 0; i < numExports; i++)
        {
            woss << L"|";
            if (depth > 0)
            {
                for (UINT j = 0; j < 2 * depth - 1; j++) woss << L" ";
            }
            woss << L" [" << i << L"]: ";
            if (exports[i].ExportToRename) woss << exports[i].ExportToRename << L" --> ";
            woss << exports[i].Name << L"\n";
        }
        return woss.str();
    };

    for (UINT i = 0; i < desc->NumSubobjects; i++)
    {
        wstr << L"| [" << i << L"]: ";
        switch (desc->pSubobjects[i].Type)
        {
        case D3D12_STATE_SUBOBJECT_TYPE_FLAGS:
            wstr << L"Flags (not yet defined)\n";
            break;
        case D3D12_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE:
            wstr << L"Root Signature 0x" << desc->pSubobjects[i].pDesc << L"\n";
            break;
        case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
            wstr << L"Local Root Signature 0x" << desc->pSubobjects[i].pDesc << L"\n";
            break;
        case D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK:
            wstr << L"Node Mask: 0x" << std::hex << std::setfill(L'0') << std::setw(8) << *static_cast<const UINT*>(desc->pSubobjects[i].pDesc) << std::setw(0) << std::dec << L"\n";
            break;
        case D3D12_STATE_SUBOBJECT_TYPE_CACHED_STATE_OBJECT:
            wstr << L"Cached State Object (not yet defined)\n";
            break;
        case D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY:
        {
            wstr << L"DXIL Library 0x";
            auto lib = static_cast<const D3D12_DXIL_LIBRARY_DESC*>(desc->pSubobjects[i].pDesc);
            wstr << lib->DXILLibrary.pShaderBytecode << L", " << lib->DXILLibrary.BytecodeLength << L" bytes\n";
            wstr << ExportTree(1, lib->NumExports, lib->pExports);
            break;
        }
        case D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION:
        {
            wstr << L"Existing Library 0x";
            auto collection = static_cast<const D3D12_EXISTING_COLLECTION_DESC*>(desc->pSubobjects[i].pDesc);
            wstr << collection->pExistingCollection << L"\n";
            wstr << ExportTree(1, collection->NumExports, collection->pExports);
            break;
        }
        case D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
        {
            wstr << L"Subobject to Exports Association (Subobject [";
            auto association = static_cast<const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(desc->pSubobjects[i].pDesc);
            UINT index = static_cast<UINT>(association->pSubobjectToAssociate - desc->pSubobjects);
            wstr << index << L"])\n";
            for (UINT j = 0; j < association->NumExports; j++)
            {
                wstr << L"|  [" << j << L"]: " << association->pExports[j] << L"\n";
            }
            break;
        }
        case D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
        {
            wstr << L"DXIL Subobjects to Exports Association (";
            auto association = static_cast<const D3D12_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(desc->pSubobjects[i].pDesc);
            wstr << association->SubobjectToAssociate << L")\n";
            for (UINT j = 0; j < association->NumExports; j++)
            {
                wstr << L"|  [" << j << L"]: " << association->pExports[j] << L"\n";
            }
            break;
        }
        case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG:
        {
            wstr << L"Raytracing Shader Config\n";
            auto config = static_cast<const D3D12_RAYTRACING_SHADER_CONFIG*>(desc->pSubobjects[i].pDesc);
            wstr << L"|  [0]: Max Payload Size: " << config->MaxPayloadSizeInBytes << L" bytes\n";
            wstr << L"|  [1]: Max Attribute Size: " << config->MaxAttributeSizeInBytes << L" bytes\n";
            break;
        }
        case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:
        {
            wstr << L"Raytracing Pipeline Config\n";
            auto config = static_cast<const D3D12_RAYTRACING_PIPELINE_CONFIG*>(desc->pSubobjects[i].pDesc);
            wstr << L"|  [0]: Max Recursion Depth: " << config->MaxTraceRecursionDepth << L"\n";
            break;
        }
        case D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP:
        {
            wstr << L"Hit Group (";
            auto hitGroup = static_cast<const D3D12_HIT_GROUP_DESC*>(desc->pSubobjects[i].pDesc);
            wstr << (hitGroup->HitGroupExport ? hitGroup->HitGroupExport : L"[none]") << L")\n";
            wstr << L"|  [0]: Any Hit Import: " << (hitGroup->AnyHitShaderImport ? hitGroup->AnyHitShaderImport : L"[none]") << L"\n";
            wstr << L"|  [1]: Closest Hit Import: " << (hitGroup->ClosestHitShaderImport ? hitGroup->ClosestHitShaderImport : L"[none]") << L"\n";
            wstr << L"|  [2]: Intersection Import: " << (hitGroup->IntersectionShaderImport ? hitGroup->IntersectionShaderImport : L"[none]") << L"\n";
            break;
        }
        }
        wstr << L"|--------------------------------------------------------------------\n";
    }
    wstr << L"\n";
    OutputDebugStringW(wstr.str().c_str());
}

struct D3DBuffer
{
    ID3D12Resource* resource;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptorHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptorHandle;
};

struct AccelerationStructureBuffers
{
    ID3D12Resource* scratchBuffer;
    ID3D12Resource* accelerationStructure;
    ID3D12Resource* instanceDsc; // Only used for top level Acceleration Structure
    uint32_t        maxSizeInBytes;
};

struct D3D12Test {
    uint32_t dxgiFactoryFlags;
    uint32_t frameIndex; // TODO(Dustin) : Possibly remove this in favor of using this in the render code


    ID3D12Debug* dxDebug;
    IDXGIFactory5* dxgiFactory;
    IDXGIAdapter* gpuAdapter;
    ID3D12Device* gpuDevice;

    ID3D12CommandAllocator* cmdAlloc[swapchainBufferCount];
    D3D12_COMMAND_QUEUE_DESC cmdQDsc;
    ID3D12CommandQueue* cmdQ;
    ID3D12GraphicsCommandList* cmdList;

    ID3D12RaytracingFallbackDevice* fallbackDevice;
    ID3D12RaytracingFallbackCommandList* fallbackCmdList;
    ID3D12RaytracingFallbackStateObject* fallbackStateObj;

    DXGI_SWAP_CHAIN_DESC1 swapchainDsc;
    IDXGISwapChain3* swapchain;

    uint32_t rtvDscHeapSize;
    uint32_t dsvDscHeapSize;
    uint32_t rayDscHeapSize;
    ID3D12DescriptorHeap* rtvDscHeap;
    ID3D12DescriptorHeap* dsvDscHeap;
    ID3D12DescriptorHeap* rayDscHeap;
    uint32_t rayNumDscAllocated;

    ID3D12Resource* swapchainFrames[swapchainBufferCount];
    ID3D12Resource* depthStencil;

    uint64_t fenceValues[swapchainBufferCount];
    ID3D12Fence* fence;

    D3D12_VIEWPORT viewport;
    D3D12_RECT scissor;

    ID3D12RootSignature* globalRootSignature;
    ID3D12RootSignature* localRootSignature;
    
    D3DBuffer vertexBuffer;
    D3DBuffer indexBuffer;
    D3DBuffer AABBBuffer;

};

#define QUERY_INTERFACE(A,B) A->QueryInterface(__uuidof(decltype(A)),(void**)&B)

D3D12Test createD3D12Resources(HWND window) {
    
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
        QUERY_INTERFACE(tmpFactory, toReturn.dxgiFactory);

        dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
        dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
    }

    // Retrieve adapter handle
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

    // Create the Raytracing Fallback Device
    HRESULT createRaytracingDeviceRes;
    if (!SUCCEEDED(createRaytracingDeviceRes = D3D12CreateRaytracingFallbackDevice(toReturn.gpuDevice, ForceComputeFallback | EnableRootDescriptorsInShaderRecords, 0, IID_PPV_ARGS(&toReturn.fallbackDevice))))
    {

    }

    // Create the command queue
    toReturn.cmdQDsc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    toReturn.cmdQDsc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    toReturn.gpuDevice->CreateCommandQueue(&toReturn.cmdQDsc, IID_PPV_ARGS(&toReturn.cmdQ));

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
        toReturn.cmdQ,
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

    // Create Descriptor Heap for the Render Target Views
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
        // Also create a CommandAllocator for each backbuffer
        for (uint32_t frame = 0; frame < swapchainBufferCount; frame++)
        {
            toReturn.swapchain->GetBuffer(frame, IID_PPV_ARGS(&toReturn.swapchainFrames[frame]));
            toReturn.gpuDevice->CreateRenderTargetView(toReturn.swapchainFrames[frame], nullptr, rtvDscHandle);

            wchar_t debugName[25] = {};
            swprintf_s(debugName, L"RenderTarget %u", frame);
            toReturn.swapchainFrames[frame]->SetName(debugName);

            rtvDscHandle.Offset(1, toReturn.rtvDscHeapSize);

            toReturn.gpuDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&toReturn.cmdAlloc[frame]));
        }

        // Create Command List for recording graphics commands
        toReturn.gpuDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, toReturn.cmdAlloc[0], nullptr, IID_PPV_ARGS(&toReturn.cmdList));

        // Create Fallback commandList
        toReturn.fallbackDevice->QueryRaytracingCommandList(toReturn.cmdList, IID_PPV_ARGS(&toReturn.fallbackCmdList));
        toReturn.cmdList->Close();

        // Create Depth Stencil
        // Create Descriptor Heap for the Depth Stencil View
        {
            D3D12_DESCRIPTOR_HEAP_DESC heapDsc = {};
            heapDsc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            heapDsc.NumDescriptors = 1;
            heapDsc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            toReturn.gpuDevice->CreateDescriptorHeap(&heapDsc, IID_PPV_ARGS(&toReturn.dsvDscHeap));
            toReturn.dsvDscHeapSize = toReturn.gpuDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        }
        CD3DX12_HEAP_PROPERTIES depthStencilHeapProperties(D3D12_HEAP_TYPE_DEFAULT);

        DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D32_FLOAT;
        D3D12_RESOURCE_DESC depthStencilDsc = CD3DX12_RESOURCE_DESC::Tex2D(
            depthStencilFormat,
            width,
            height,
            1, // This depth stencil view has only one texture.
            1  // Use a single mipmap level.
        );
        depthStencilDsc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE depthStencilClearValue = {};
        depthStencilClearValue.Format = depthStencilFormat;
        depthStencilClearValue.DepthStencil.Depth = 1.0f;
        depthStencilClearValue.DepthStencil.Stencil = 0;

        toReturn.gpuDevice->CreateCommittedResource(&depthStencilHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &depthStencilDsc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &depthStencilClearValue,
            IID_PPV_ARGS(&toReturn.depthStencil)
        );

        toReturn.depthStencil->SetName(L"DepthStencil");

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDsc = {};
        dsvDsc.Format = depthStencilFormat;
        dsvDsc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

        toReturn.gpuDevice->CreateDepthStencilView(toReturn.depthStencil, &dsvDsc, toReturn.dsvDscHeap->GetCPUDescriptorHandleForHeapStart());

        // Initialize viewport
        toReturn.viewport.TopLeftX = 0.f;
        toReturn.viewport.TopLeftY = 0.f;
        toReturn.viewport.Width = width;
        toReturn.viewport.Height = height;
        toReturn.viewport.MinDepth = D3D12_MIN_DEPTH;
        toReturn.viewport.MaxDepth = D3D12_MAX_DEPTH;

        // Initialize scissor rect
        toReturn.scissor.left = 0.f;
        toReturn.scissor.right = width;
        toReturn.scissor.top = 0.f;
        toReturn.scissor.bottom = height;

        // Create Fence
        toReturn.gpuDevice->CreateFence(toReturn.fenceValues[toReturn.frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&toReturn.fence));

    }

    return toReturn;
}

enum GeometryType {
    Triangles = 0,
    AABB,
    GeometryTypeCount
};

enum RayType {
    Radiance = 0,
    Shadow,
    RayTypeCount
};

enum GlobalRootSignature {
    OutputView = 0,
    AccelerationStructure,
    SceneConstant,
    AABBAttributeBuffer,
    VertexBuffers,
    GlobalRootSignatureCount
};

enum LocalRootSignature {
    Triangle = 0
};


// Shader entry points.
const wchar_t* raygenShaderName = L"RayGenShader";
const wchar_t* intersectionShaderNames[] =
{
    L"intersectionShader_AnalyticPrimitive",
    L"intersectionShader_VolumetricPrimitive",
    L"intersectionShader_SignedDistancePrimitive",
};
const wchar_t* closestHitShaderNames[] =
{
    L"closestHitShader_Triangle",
    L"closestHitShader_AABB",
};
const wchar_t* missShaderNames[] =
{
    L"MyMissShader", L"MyMissShader_ShadowRay"
};

// Hit groups.
const wchar_t* triangleHitgroupNames[] =
{
    L"hitgroup_Triangle_Radiance", L"hitgroup_Triangle_Shadow"
};
const wchar_t* AABBHitGroupNames[][RayType::RayTypeCount] =
{
    { L"hitGroup_AABB_AnalyticPrimitive"        , L"hitGroup_AABB_AnalyticPrimitive_ShadowRay" },
    { L"hitGroup_AABB_VolumetricPrimitive"      , L"hitGroup_AABB_VolumetricPrimitive_ShadowRay" },
    { L"hitGroup_AABB_SignedDistancePrimitive"  , L"hitGroup_AABB_SignedDistancePrimitive_ShadowRay" },
};

struct RayPayLoad {};
struct ShadowRayPayload {};
struct PrimitiveAttributes {};

uint32_t const maxRecursionDepth = 16;

ID3D12RootSignature* serializeRootSignature(ID3D12Device * const gpuDevice, ID3D12RaytracingFallbackDevice * const fallbackDevice, const CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDsc)
{
     ID3DBlob* blob;
     ID3DBlob* error;

     ID3D12RootSignature* serializedRootSignature;

     fallbackDevice->D3D12SerializeRootSignature(&rootSignatureDsc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
     gpuDevice->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&serializedRootSignature));

     return serializedRootSignature;
}

void createRootSignatures(D3D12Test resources)
{
     CD3DX12_DESCRIPTOR_RANGE dscRanges[2];
     dscRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // Output Texture
     dscRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1); // Static index and Vertex Buffers

     CD3DX12_ROOT_PARAMETER rootParameters[GlobalRootSignature::GlobalRootSignatureCount];
     rootParameters[GlobalRootSignature::OutputView].InitAsDescriptorTable(1, &dscRanges[0]);
     rootParameters[GlobalRootSignature::AccelerationStructure].InitAsShaderResourceView(0);
     rootParameters[GlobalRootSignature::SceneConstant].InitAsConstantBufferView(0);
     rootParameters[GlobalRootSignature::AABBAttributeBuffer].InitAsShaderResourceView(3);
     rootParameters[GlobalRootSignature::VertexBuffers].InitAsDescriptorTable(1, &dscRanges[1]);

     CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDsc(ARRAYSIZE(rootParameters), rootParameters);

     // TODO (Dustin) : Create Local Root signatures for different type of geometries
     // See : D3D12RaytracingProceduralGeometry::CreateRootSignatures()

}

void createRaytracingPipelineStateObject(D3D12Test resources)
{
    CD3D12_STATE_OBJECT_DESC raytracingPipeline{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };
    CD3D12_DXIL_LIBRARY_SUBOBJECT* dxilLib = raytracingPipeline.CreateSubobject<CD3D12_DXIL_LIBRARY_SUBOBJECT>();

    // TODO (Dustin) : Create compiled HLSL Bytecode to load in as a dxil library
    uint8_t g_pRaytracing[2];

    D3D12_SHADER_BYTECODE dxilByteCode = CD3DX12_SHADER_BYTECODE((void*)g_pRaytracing, ARRAYSIZE(g_pRaytracing)); // Requires the compiled hlsl bytecode
    dxilLib->SetDXILLibrary(&dxilByteCode);

    // Hit groups for radiance and shadow rays
    CD3D12_HIT_GROUP_SUBOBJECT* triangleRadianceHitgroup = raytracingPipeline.CreateSubobject<CD3D12_HIT_GROUP_SUBOBJECT>();
    triangleRadianceHitgroup->SetClosestHitShaderImport(closestHitShaderNames[GeometryType::Triangles]);
    triangleRadianceHitgroup->SetHitGroupExport(triangleHitgroupNames[RayType::Radiance]);

    CD3D12_HIT_GROUP_SUBOBJECT* triangleShadowHitgroup = raytracingPipeline.CreateSubobject<CD3D12_HIT_GROUP_SUBOBJECT>();
    triangleShadowHitgroup->SetClosestHitShaderImport(closestHitShaderNames[GeometryType::Triangles]);
    triangleShadowHitgroup->SetHitGroupExport(triangleHitgroupNames[RayType::Shadow]);

    // Shader configuration
    CD3D12_RAYTRACING_SHADER_CONFIG_SUBOBJECT* shaderConfiguration = raytracingPipeline.CreateSubobject<CD3D12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    int payloadSize = max(sizeof(RayPayLoad), sizeof(ShadowRayPayload)); // TODO(Dustin) : Create these structs?
    int attributeSize = sizeof(PrimitiveAttributes);
    shaderConfiguration->Config(payloadSize, attributeSize);

    // Local root signature and shader association
    CD3D12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* localRootSignature = raytracingPipeline.CreateSubobject<CD3D12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    localRootSignature->SetRootSignature(&resources.localRootSignature[LocalRootSignature::Triangle]);
    // Shader association
    CD3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* rootSignatureAssociation = raytracingPipeline.CreateSubobject<CD3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    rootSignatureAssociation->SetSubobjectToAssociate(*localRootSignature);
    rootSignatureAssociation->AddExports(triangleHitgroupNames);

    // Global root signature that is shared between all raytracing shaders during a DispatchRays() call
    CD3D12_ROOT_SIGNATURE_SUBOBJECT* globalRootSignature = raytracingPipeline.CreateSubobject<CD3D12_ROOT_SIGNATURE_SUBOBJECT>();
    globalRootSignature->SetRootSignature(resources.globalRootSignature);

    // Pipeline Configuration
    // Maximum recursion depth for TraceRay()
    CD3D12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT* pipelineConfiguration = raytracingPipeline.CreateSubobject<CD3D12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    pipelineConfiguration->Config(maxRecursionDepth);

    // Debug print out the current state 
    PrintStateObjectDesc(raytracingPipeline);

    // Create the actual state object
    resources.fallbackDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&resources.fallbackStateObj));

}

void createRaytracingDescriptorHeap(D3D12Test resources) {

    ID3D12Device* device = resources.gpuDevice;

    D3D12_DESCRIPTOR_HEAP_DESC dscHeapDsc = {};
    dscHeapDsc.NumDescriptors = 6;
    dscHeapDsc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    dscHeapDsc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    dscHeapDsc.NodeMask = 0;

    device->CreateDescriptorHeap(&dscHeapDsc, IID_PPV_ARGS(&resources.rayDscHeap));
    
    resources.rayDscHeapSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

}

// Allocate a descriptor and return its index. 
// If the passed descriptorIndexToUse is valid, it will be used instead of allocating a new one.
uint32_t AllocateDescriptor(ID3D12DescriptorHeap* descriptorHeap, D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor, UINT descriptorIndexToUse)
{
    auto descriptorHeapCpuBase = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    if (descriptorIndexToUse >= descriptorHeap->GetDesc().NumDescriptors)
    {
        descriptorIndexToUse = m_descriptorsAllocated++;
        descriptorIndexToUse = m_descriptorsAllocated++;
    }
    *cpuDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCpuBase, descriptorIndexToUse, m_descriptorSize);
    return descriptorIndexToUse;
}

// Create a wrapped pointer for the Fallback Layer path.
WRAPPED_GPU_POINTER CreateFallbackWrappedPointer(D3D12Test resources, ID3D12Resource* resource, UINT bufferNumElements)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC rawBufferUavDesc = {};
    rawBufferUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    rawBufferUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    rawBufferUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    rawBufferUavDesc.Buffer.NumElements = bufferNumElements;

    D3D12_CPU_DESCRIPTOR_HANDLE bottomLevelDescriptor;

    // Only compute fallback requires a valid descriptor index when creating a wrapped pointer.
    UINT descriptorHeapIndex = 0;
    if (!resources.fallbackDevice->UsingRaytracingDriver())
    {
        descriptorHeapIndex = AllocateDescriptor(&bottomLevelDescriptor);
        resources.gpuDevice->CreateUnorderedAccessView(resource, nullptr, &rawBufferUavDesc, bottomLevelDescriptor);
    }
    return resources.fallbackDevice->GetWrappedPointerSimple(descriptorHeapIndex, resource->GetGPUVirtualAddress());
}

inline void allocateUploadBuffer(D3D12Test resources, void* data, uint32_t dataSize, ID3D12Resource ** resource, const wchar_t* resourceName = nullptr)
{
    CD3DX12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC bufferDsc = CD3DX12_RESOURCE_DESC::Buffer(dataSize);
    resources.gpuDevice->CreateCommittedResource(
        &uploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferDsc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(resource)
    );

    if (resourceName)
    {
        (*resource)->SetName(resourceName);
    }

    void* mappedData;
    (*resource)->Map(0, nullptr, &mappedData);
    memcpy(mappedData, data, dataSize);
    (*resource)->Unmap(0, nullptr);
}

inline void AllocateUAVBuffer(ID3D12Device* pDevice, uint32_t bufferSize, ID3D12Resource **ppResource, D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_COMMON, const wchar_t* resourceName = nullptr)
{
    CD3DX12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    pDevice->CreateCommittedResource(
        &uploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        initialResourceState,
        nullptr,
        IID_PPV_ARGS(ppResource));
    if (resourceName)
    {
        (*ppResource)->SetName(resourceName);
    }
}

void buildTestGeometry(D3D12Test resources)
{
    // TODO(Dustin): Implement to test so acceleration structures and uploadBuffer can be tested
}

struct TestVertex {
    float x, y, z;
};
struct TestIndex {
    uint32_t index;
};

AccelerationStructureBuffers buildBottomLevelAS(D3D12Test resources, std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDscs, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE)
{
    AccelerationStructureBuffers bottomLevelASBuffers = {};

    ID3D12Device* device = resources.gpuDevice;
    ID3D12GraphicsCommandList* cmdList = resources.cmdList;

    // Get the size requirements for the scratch and acceleration structure buffers
    D3D12_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_DESC prebuildInfoDsc = {};
    prebuildInfoDsc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    prebuildInfoDsc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    prebuildInfoDsc.Flags = buildFlags;
    prebuildInfoDsc.NumDescs = geometryDscs.size();
    prebuildInfoDsc.pGeometryDescs = geometryDscs.data();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
    resources.fallbackDevice->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfoDsc, &prebuildInfo);

    // Create scratch buffer
    AllocateUAVBuffer(device, prebuildInfo.ScratchDataSizeInBytes, &bottomLevelASBuffers.scratchBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"ScratchResource");

    // Allocate resources for acceleration structures.
    // Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent). 
    // Default heap is OK since the application doesn’t need CPU read/write access to them. 
    // The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
    // and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both: 
    //  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
    //  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.
    {
        D3D12_RESOURCE_STATES initialResourceState = resources.fallbackDevice->GetAccelerationStructureResourceState();
        AllocateUAVBuffer(device, prebuildInfo.ResultDataMaxSizeInBytes, &bottomLevelASBuffers.accelerationStructure, initialResourceState, L"BottomLevelAccelerationStructure");
    }

    // bottom-level AS desc.
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc = {};
    {
        bottomLevelBuildDesc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        bottomLevelBuildDesc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        bottomLevelBuildDesc.Flags = buildFlags;
        bottomLevelBuildDesc.ScratchAccelerationStructureData = { bottomLevelASBuffers.scratchBuffer->GetGPUVirtualAddress(), bottomLevelASBuffers.scratchBuffer->GetDesc().Width };
        bottomLevelBuildDesc.DestAccelerationStructureData = { bottomLevelASBuffers.accelerationStructure->GetGPUVirtualAddress(), prebuildInfo.ResultDataMaxSizeInBytes };
        bottomLevelBuildDesc.NumDescs = static_cast<UINT>(geometryDscs.size());
        bottomLevelBuildDesc.pGeometryDescs = geometryDscs.data();
    }

    // Set the descriptor heaps to be used during acceleration structure build for the Fallback Layer.
    ID3D12DescriptorHeap *pDescriptorHeaps[] = { resources.rtvDscHeap };
    resources.fallbackCmdList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);
    resources.fallbackCmdList->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc);

    return bottomLevelASBuffers;
}

AccelerationStructureBuffers BuildTopLevelAS(D3D12Test resources, AccelerationStructureBuffers bottomLevelAS[GeometryType::GeometryTypeCount], D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags)
{
    AccelerationStructureBuffers toplevelASBuffers = {};

    ID3D12Device* device = resources.gpuDevice;
    ID3D12GraphicsCommandList* cmdList = resources.cmdList;
    ID3D12RaytracingFallbackDevice* fallbackDevice = resources.fallbackDevice;

    // Get the size requirements for the scratch and acceleration structure buffers
    D3D12_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_DESC prebuildInfoDsc = {};
    prebuildInfoDsc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    prebuildInfoDsc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    prebuildInfoDsc.Flags = buildFlags;
    prebuildInfoDsc.NumDescs = NUM_BLAS;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
    resources.fallbackDevice->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfoDsc, &prebuildInfo);

    // Create scratch buffer
    AllocateUAVBuffer(device, prebuildInfo.ScratchDataSizeInBytes, &toplevelASBuffers.scratchBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"ScratchResource");

    // Allocate resources for acceleration structures.
    // Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent). 
    // Default heap is OK since the application doesn’t need CPU read/write access to them. 
    // The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
    // and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both: 
    //  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
    //  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.
    {
        D3D12_RESOURCE_STATES initialResourceState = fallbackDevice->GetAccelerationStructureResourceState();

        AllocateUAVBuffer(device, prebuildInfo.ResultDataMaxSizeInBytes, &toplevelASBuffers.accelerationStructure, initialResourceState, L"TopLevelAccelerationStructure");
    }

    // Note on Emulated GPU pointers (AKA Wrapped pointers) requirement in Fallback Layer:
    // The primary point of divergence between the DXR API and the compute-based Fallback layer is the handling of GPU pointers. 
    // DXR fundamentally requires that GPUs be able to dynamically read from arbitrary addresses in GPU memory. 
    // The existing Direct Compute API today is more rigid than DXR and requires apps to explicitly inform the GPU what blocks of memory it will access with SRVs/UAVs.
    // In order to handle the requirements of DXR, the Fallback Layer uses the concept of Emulated GPU pointers, 
    // which requires apps to create views around all memory they will access for raytracing, 
    // but retains the DXR-like flexibility of only needing to bind the top level acceleration structure at DispatchRays.
    //
    // The Fallback Layer interface uses WRAPPED_GPU_POINTER to encapsulate the underlying pointer
    // which will either be an emulated GPU pointer for the compute - based path or a GPU_VIRTUAL_ADDRESS for the DXR path.

    // Create instance descs for the bottom-level acceleration structures.
    ID3D12Resource* instanceDscsResource;
    {
        D3D12_RAYTRACING_FALLBACK_INSTANCE_DESC instanceDescs[GeometryType::GeometryTypeCount] = {};
        WRAPPED_GPU_POINTER bottomLevelASaddresses[GeometryType::GeometryTypeCount] =
        {
            CreateFallbackWrappedPointer(bottomLevelAS[0].accelerationStructure, static_cast<uint32_t>(bottomLevelAS[0].ResultDataMaxSizeInBytes) / sizeof(uint32_t)),
            CreateFallbackWrappedPointer(bottomLevelAS[1].accelerationStructure, static_cast<uint32_t>(bottomLevelAS[1].ResultDataMaxSizeInBytes) / sizeof(uint32_t))
        };
        BuildBotomLevelASInstanceDescs<D3D12_RAYTRACING_FALLBACK_INSTANCE_DESC>(bottomLevelASaddresses, &instanceDescsResource);
    }

    // Create a wrapped pointer to the acceleration structure.
    if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
    {
        UINT numBufferElements = static_cast<UINT>(topLevelPrebuildInfo.ResultDataMaxSizeInBytes) / sizeof(UINT32);
        m_fallbackTopLevelAccelerationStructurePointer = CreateFallbackWrappedPointer(topLevelAS.Get(), numBufferElements);
    }

    // Top-level AS desc
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelBuildDesc = {};
    {
        topLevelBuildDesc.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        topLevelBuildDesc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        topLevelBuildDesc.Flags = buildFlags;
        topLevelBuildDesc.DestAccelerationStructureData = { topLevelAS->GetGPUVirtualAddress(), topLevelPrebuildInfo.ResultDataMaxSizeInBytes };
        topLevelBuildDesc.NumDescs = NUM_BLAS;
        topLevelBuildDesc.pGeometryDescs = nullptr;
        topLevelBuildDesc.InstanceDescs = instanceDescsResource->GetGPUVirtualAddress();
        topLevelBuildDesc.ScratchAccelerationStructureData = { scratch->GetGPUVirtualAddress(), scratch->GetDesc().Width };
    }

    // Build acceleration structure.
    if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
    {
        // Set the descriptor heaps to be used during acceleration structure build for the Fallback Layer.
        ID3D12DescriptorHeap *pDescriptorHeaps[] = { m_descriptorHeap.Get() };
        m_fallbackCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);
        m_fallbackCommandList->BuildRaytracingAccelerationStructure(&topLevelBuildDesc);
    }
    else // DirectX Raytracing
    {
        m_dxrCommandList->BuildRaytracingAccelerationStructure(&topLevelBuildDesc);
    }

    AccelerationStructureBuffers topLevelASBuffers;
    topLevelASBuffers.accelerationStructure = topLevelAS;
    topLevelASBuffers.instanceDesc = instanceDescsResource;
    topLevelASBuffers.scratch = scratch;
    topLevelASBuffers.ResultDataMaxSizeInBytes = topLevelPrebuildInfo.ResultDataMaxSizeInBytes;
    return topLevelASBuffers;
}
void buildAccelerationStructures(D3D12Test resources)
{
    ID3D12Device*               gpuDevice   = resources.gpuDevice;
    ID3D12GraphicsCommandList*  cmdList     = resources.cmdList;
    ID3D12CommandAllocator**    cmdAlloc    = resources.cmdAlloc;
    ID3D12CommandQueue*         cmdQ        = resources.cmdQ;

    D3DBuffer indexBuffer   = resources.indexBuffer;
    D3DBuffer vertexBuffer  = resources.vertexBuffer;
    D3DBuffer AABBBuffer    = resources.AABBBuffer;

    // Reset command list
    cmdList->Reset(*cmdAlloc, nullptr);

    AccelerationStructureBuffers bottomLevelAS[GeometryType::GeometryTypeCount];
    std::array<std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>, GeometryType::GeometryTypeCount> geometryDscs;
    {
        // Mark the geometry as opaque. 
        // PERFORMANCE TIP: mark geometry as opaque whenever applicable as it can enable important ray processing optimizations.
        // Note: When rays encounter opaque geometry an any hit shader will not be executed whether it is present or not.
        D3D12_RAYTRACING_GEOMETRY_FLAGS geometryFlags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

        // Triangle geometry dsc
        {
            // Triangle bottom-level AS contains a single plane geometry.
            geometryDscs[GeometryType::Triangles].resize(1);

            // Plane geometry
            auto& geometryDesc = geometryDscs[GeometryType::Triangles][0];
            geometryDesc = {};
            geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            geometryDesc.Triangles.IndexBuffer = indexBuffer.resource->GetGPUVirtualAddress();
            geometryDesc.Triangles.IndexCount = static_cast<UINT>(indexBuffer.resource->GetDesc().Width) / sizeof(TestIndex);
            geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
            geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            geometryDesc.Triangles.VertexCount = static_cast<UINT>(vertexBuffer.resource->GetDesc().Width) / sizeof(TestVertex);
            geometryDesc.Triangles.VertexBuffer.StartAddress = vertexBuffer.resource->GetGPUVirtualAddress();
            geometryDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(TestVertex);
            geometryDesc.Flags = geometryFlags;
        }
                
        // AABB geometry dsc
        {
            D3D12_RAYTRACING_GEOMETRY_DESC aabbDescTemplate = {};
            aabbDescTemplate.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
            aabbDescTemplate.AABBs.AABBCount = 1;
            aabbDescTemplate.AABBs.AABBs.StrideInBytes = sizeof(D3D12_RAYTRACING_AABB);
            aabbDescTemplate.Flags = geometryFlags;

            // One AABB primitive per geometry.
            geometryDscs[GeometryType::AABB].resize(1);//IntersectionShaderType::TotalPrimitiveCount, aabbDescTemplate);

            // Create AABB geometries. 
            // Having separate geometries allows of separate shader record binding per geometry.
            // In this sample, this lets us specify custom hit groups per AABB geometry.
            for (UINT i = 0; i < 1; i++) //IntersectionShaderType::TotalPrimitiveCount; i++)
            {
                auto& geometryDesc = geometryDscs[GeometryType::AABB][i];
                geometryDesc.AABBs.AABBs.StartAddress = AABBBuffer.resource->GetGPUVirtualAddress() + i * sizeof(D3D12_RAYTRACING_AABB);
            }
        }

        // Build bottom-level Acceleration Structures
        for (uint32_t i = 0; i < GeometryType::GeometryTypeCount; i++)
        {
            bottomLevelAS[i] = buildBottomLevelAS(resources, geometryDscs[i]);
        }

        // Batch all resource barriers for bottom-level AS builds
        D3D12_RESOURCE_BARRIER resourcesBarriers[GeometryType::GeometryTypeCount];
        for (uint32_t i = 0; i < GeometryType::GeometryTypeCount; i++)
        {   
            resourcesBarriers[i] = CD3DX12_RESOURCE_BARRIER::UAV(bottomLevelAS[i].accelerationStructure);
        }
        
        // Build top-level AS 
        AccelerationStructureBuffers topLevelAS = buildTopLevelAS(bottomLevelAS);

    }
}

D3D12Test initD3D12(HWND window)
{
    D3D12Test resources = createD3D12Resources(window);
    createRootSignatures(resources);
    createRaytracingPipelineStateObject(resources);
    createRaytracingDescriptorHeap(resources);

    buildAccelerationStructures(resources);

    return resources;
}



int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    // Load x_input dll to allow access to the Xbox Controller
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
        "DXRTest",
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
            // Note: Normally you would poll more than once per frame if this was for a game
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
