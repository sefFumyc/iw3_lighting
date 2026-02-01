#include <Windows.h>
#include <iostream>
#include <vector>
#include <math.h>

// DirectX Headers
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi.h>

// MinHook & ImGui Headers
#include "MinHook/MinHook.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_dx9.h"
#include "ImGui/imgui_impl_dx11.h"
#include "ImGui/imgui_impl_win32.h"

// Link Libraries [Link required libraries(链接所需库)]
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =============================================================
// Section 1: Constants & Structures [常量与结构体]
// =============================================================

// Game Memory Addresses [游戏内存地址]
#define ADDR_LIGHT_COUNT    0x00D0244AC
#define ADDR_LIGHT_ARRAY    0x00D0244B0
#define ADDR_LIGHT_MATERIAL 0x00CC9A2AC
#define ADDR_FUNC_RENDER    0x00634DB0
#define HARDCODED_MAT_PTR   0xF0D43C 

struct LightConfig
{
    bool  enable = false;
    int   type = 3;            // 3=Point, 2=Spot
    float pos[3] = { 0, 0, 0 };
    float color[3] = { 1, 1, 1 };
    float radius = 300.0f;
    float pitch = -90.0f;
    float yaw = 0.0f;
    float cutoff = 45.0f;

    // Flicker Settings [闪烁设置]
    bool  flicker = false;
    int   flickerMode = 0;     // 0=Radius, 1=Intensity
    float flickerAmp = 50.0f;
    float flickerFreq = 2.0f;
};

// =============================================================
// Section 2: Global Variables [全局变量]
// =============================================================

// Application State
LightConfig g_Lights[4];
bool        g_ShowMenu = true;
bool        g_Init = false;
void* d3d9Device[119];   // VTable storage for DX9

// Renderer State
enum RendererType { RENDERER_NONE, RENDERER_DX9, RENDERER_DX11 };
RendererType g_CurrentRenderer = RENDERER_NONE;

// Function Pointers (Hooks)
typedef long(__stdcall* EndScene_t)(LPDIRECT3DDEVICE9);
static EndScene_t oEndScene = NULL;

typedef HRESULT(__stdcall* Present_t)(IDXGISwapChain*, UINT, UINT);
static Present_t oPresent = NULL;

typedef int(__cdecl* RenderLights_t)(void*);
static RenderLights_t oRenderLights = NULL;

// DX11 Globals
ID3D11Device* g_pd3dDevice = NULL;
ID3D11DeviceContext* g_pd3dContext = NULL;
ID3D11RenderTargetView* g_mainRenderTargetView = NULL;

// Window Procedure Hook
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
typedef LRESULT(CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);
WNDPROC oWndProc;

// =============================================================
// Section 3: Helper Functions [辅助函数]
// =============================================================

// Convert angles to direction vector [将角度转换为方向向量]
void AngleVectors(float pitch, float yaw, float* outDir)
{
    float angle, sy, cy, sp, cp;
    angle = yaw * (float)(M_PI / 180.0f);
    sy = sin(angle); cy = cos(angle);
    angle = pitch * (float)(M_PI / 180.0f);
    sp = sin(angle); cp = cos(angle);
    outDir[0] = cp * cy; outDir[1] = cp * sy; outDir[2] = -sp;
}

// =============================================================
// Section 4: Core Logic [核心逻辑]
// =============================================================

// Logic to inject custom lights into game memory [注入自定义灯光到游戏内存的逻辑]
void InjectLights()
{
    int* pCount = (int*)ADDR_LIGHT_COUNT;
    int* pMatHandle = (int*)ADDR_LIGHT_MATERIAL;
    int matValue = (*pMatHandle != 0) ? *pMatHandle : HARDCODED_MAT_PTR;

    float time = (float)GetTickCount() / 1000.0f;

    for (int i = 0; i < 4; i++)
    {
        if (!g_Lights[i].enable || *pCount >= 32) continue;

        // Calculate Flicker [计算闪烁]
        float wave = sin(time * g_Lights[i].flickerFreq);
        float finalRadius = g_Lights[i].radius;
        float finalColor[3] = { g_Lights[i].color[0], g_Lights[i].color[1], g_Lights[i].color[2] };

        if (g_Lights[i].flicker)
        {
            if (g_Lights[i].flickerMode == 0) // Radius Mode
            {
                finalRadius += wave * g_Lights[i].flickerAmp;
                if (finalRadius < 0.0f) finalRadius = 0.0f;
            }
            else // Intensity Mode
            {
                float mult = 1.0f + (wave * (g_Lights[i].flickerAmp / 100.0f));
                if (mult < 0.0f) mult = 0.0f;
                finalColor[0] *= mult; finalColor[1] *= mult; finalColor[2] *= mult;
            }
        }

        // Write to Memory [写入内存]
        uintptr_t slotAddr = ADDR_LIGHT_ARRAY + (*pCount * 64);
        char* pRaw   = (char*)slotAddr;
        float* pFloat = (float*)slotAddr;
        int* pInt   = (int*)slotAddr;

        pRaw[0]   = g_Lights[i].type;
        pFloat[1] = finalColor[0]; 
        pFloat[2] = finalColor[1]; 
        pFloat[3] = finalColor[2];
        pFloat[7] = g_Lights[i].pos[0]; 
        pFloat[8] = g_Lights[i].pos[1]; 
        pFloat[9] = g_Lights[i].pos[2];
        pFloat[10]= finalRadius;

        if (g_Lights[i].type == 2) // Spotlight math
        {
            float dir[3];
            AngleVectors(g_Lights[i].pitch, g_Lights[i].yaw, dir);
            pFloat[4] = dir[0]; pFloat[5] = dir[1]; pFloat[6] = dir[2];
            
            float cosOuter = cos(g_Lights[i].cutoff * (float)(M_PI / 180.0f));
            pFloat[11] = cosOuter;
            pFloat[12] = cosOuter + 0.05f; // Fix inverted cone
            if (pFloat[12] > 1.0f) pFloat[12] = 1.0f;
        }

        pInt[13] = 0; pInt[14] = -1; pInt[15] = matValue;
        (*pCount)++;
    }
}

// =============================================================
// Section 5: UI Rendering [界面绘制]
// =============================================================

void DrawMenu()
{
    if (!g_ShowMenu) return;

    // Setup Frame [准备帧]
    if (g_CurrentRenderer == RENDERER_DX11) ImGui_ImplDX11_NewFrame();
    else ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowSize(ImVec2(400, 600), ImGuiCond_FirstUseEver);
    const char* title = (g_CurrentRenderer == RENDERER_DX11) ? "IW3 Light (DX11)" : "COD4 Mod (DX9)";
    
    ImGui::Begin(title);

    // Global Info Header
    if (ImGui::CollapsingHeader("Global Settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Active Lights: %d / 32", *(int*)ADDR_LIGHT_COUNT);
        if (g_CurrentRenderer == RENDERER_DX11)
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Running in DX11 Mode");
        else
            ImGui::TextColored(ImVec4(0, 1, 1, 1), "Running in DX9 Mode");
    }
    ImGui::Separator();

    // Lights Loop
    for (int i = 0; i < 4; i++)
    {
        ImGui::PushID(i);
        char headerName[32]; sprintf_s(headerName, "Light #%d", i + 1);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_AllowOverlap;
        
        bool isOpen = ImGui::TreeNodeEx(headerName, flags);
        ImGui::SameLine(); 
        ImGui::Checkbox("Enable", &g_Lights[i].enable);

        if (isOpen) 
        {
            if (g_Lights[i].enable) 
            {
                ImGui::Indent();

                // Light Type
                ImGui::Text("Type:"); ImGui::SameLine();
                ImGui::RadioButton("Point", &g_Lights[i].type, 3); ImGui::SameLine();
                ImGui::RadioButton("Spot", &g_Lights[i].type, 2);
                ImGui::Spacing();

                // Position & Color
                ImGui::DragFloat3("Position", g_Lights[i].pos, 5.0f);
                ImGui::ColorEdit3("Color Picker", g_Lights[i].color);
                ImGui::DragFloat3("HDR RGB Values", g_Lights[i].color, 0.05f, 0.0f, 100.0f);
                ImGui::Spacing();

                // Radius
                ImGui::DragFloat("Radius", &g_Lights[i].radius, 5.0f, 10.0f, 5000.0f);

                // Spot Settings
                if (g_Lights[i].type == 2) {
                    ImGui::TextColored(ImVec4(0, 1, 1, 1), "Spot Settings:");
                    ImGui::SliderFloat("Pitch", &g_Lights[i].pitch, -180.0f, 180.0f);
                    ImGui::SliderFloat("Yaw", &g_Lights[i].yaw, -180.0f, 180.0f);
                    ImGui::SliderFloat("Cutoff", &g_Lights[i].cutoff, 1.0f, 89.0f);
                }

                ImGui::Separator();

                // Flicker Settings
                ImGui::Checkbox("FX: Flicker ", &g_Lights[i].flicker);
                if (g_Lights[i].flicker)
                {
                    ImGui::Indent();
                    ImGui::Text("Target:"); ImGui::SameLine();
                    
                    // Fixed IDs with ## to prevent conflicts
                    ImGui::RadioButton("Radius##Flicker", &g_Lights[i].flickerMode, 0);
                    ImGui::SameLine();
                    ImGui::RadioButton("Intensity##Flicker", &g_Lights[i].flickerMode, 1);

                    ImGui::SliderFloat("Speed", &g_Lights[i].flickerFreq, 0.1f, 20.0f, "%.1f");

                    if (g_Lights[i].flickerMode == 0)
                        ImGui::SliderFloat("Amount", &g_Lights[i].flickerAmp, 0.0f, 300.0f, "+/- %.1f units");
                    else
                        ImGui::SliderFloat("Amount", &g_Lights[i].flickerAmp, 0.0f, 100.0f, "+/- %.0f %%");

                    ImGui::Unindent();
                }

                ImGui::Unindent(); ImGui::Spacing();
            }
            else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "   (Light Disabled)");
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::End();
    ImGui::Render();
}

// =============================================================
// Section 6: Hook Functions [Hook 函数]
// =============================================================

// Window Procedure Hook [窗口消息处理 Hook]
LRESULT __stdcall hkWndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN && wParam == VK_INSERT) {
        g_ShowMenu = !g_ShowMenu;
        if (g_Init) ImGui::GetIO().MouseDrawCursor = g_ShowMenu;
    }
    if (g_ShowMenu && ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam)) 
        return true;
    
    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

// Render Logic Hook [渲染逻辑 Hook]
int __cdecl hkRenderLights(void* a1) {
    InjectLights();
    return oRenderLights(a1);
}

// DX9 EndScene Hook [DX9 渲染结束 Hook]
long __stdcall hkEndScene(LPDIRECT3DDEVICE9 pDevice)
{
    if (!g_Init)
    {
        g_CurrentRenderer = RENDERER_DX9;
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); io.MouseDrawCursor = true;

        D3DDEVICE_CREATION_PARAMETERS params; pDevice->GetCreationParameters(&params);
        ImGui_ImplWin32_Init(params.hFocusWindow);
        ImGui_ImplDX9_Init(pDevice);

        oWndProc = (WNDPROC)SetWindowLongPtr(params.hFocusWindow, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
        g_Init = true;
    }

    if (g_CurrentRenderer == RENDERER_DX9)
    {
        DrawMenu();
        if (g_ShowMenu) ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }
    return oEndScene(pDevice);
}

// DX11 Present Hook [DX11 呈现画面 Hook]
HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
    if (!g_Init)
    {
        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pd3dDevice)))
        {
            g_CurrentRenderer = RENDERER_DX11;
            g_pd3dDevice->GetImmediateContext(&g_pd3dContext);
            DXGI_SWAP_CHAIN_DESC sd; pSwapChain->GetDesc(&sd);

            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO(); io.MouseDrawCursor = true;
            ImGui_ImplWin32_Init(sd.OutputWindow);
            ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dContext);

            oWndProc = (WNDPROC)SetWindowLongPtr(sd.OutputWindow, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
            g_Init = true;
        }
        else return oPresent(pSwapChain, SyncInterval, Flags);
    }

    if (g_CurrentRenderer == RENDERER_DX11)
    {
        ID3D11Texture2D* pBackBuffer;
        pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
        pBackBuffer->Release();

        DrawMenu();
        if (g_ShowMenu)
        {
            g_pd3dContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
        if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = NULL; }
    }
    return oPresent(pSwapChain, SyncInterval, Flags);
}

// =============================================================
// Section 7: Initialization [初始化]
// =============================================================

bool HookDX9() 
{
    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return false;
    
    D3DPRESENT_PARAMETERS d3dpp = {}; 
    d3dpp.Windowed = TRUE; d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD; d3dpp.hDeviceWindow = GetForegroundWindow();
    
    IDirect3DDevice9* pDevice = nullptr;
    if (FAILED(pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp.hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDevice))) {
        pD3D->Release(); return false;
    }
    
    void** vTable = *reinterpret_cast<void***>(pDevice);
    d3d9Device[42] = vTable[42]; // EndScene index
    pDevice->Release(); pD3D->Release();

    return (MH_CreateHook(d3d9Device[42], &hkEndScene, reinterpret_cast<void**>(&oEndScene)) == MH_OK) && 
           (MH_EnableHook(d3d9Device[42]) == MH_OK);
}

bool HookDX11() 
{
    if (!GetModuleHandleA("d3d11.dll")) return false;

    // Create Dummy Device for VTable [创建虚拟设备以获取虚表]
    WNDCLASSEXA wc = { sizeof(WNDCLASSEXA), CS_CLASSDC, DefWindowProcA, 0L, 0L, GetModuleHandleA(NULL), NULL, NULL, NULL, NULL, "DX11Dummy", NULL };
    RegisterClassExA(&wc);
    HWND hWnd = CreateWindowA("DX11Dummy", NULL, WS_OVERLAPPEDWINDOW, 100, 100, 300, 300, NULL, NULL, wc.hInstance, NULL);

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
    D3D_FEATURE_LEVEL level;
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1; scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hWnd; scd.SampleDesc.Count = 1; scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD; scd.Windowed = TRUE;

    ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr; IDXGISwapChain* swap = nullptr;
    if (FAILED(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, levels, 2, D3D11_SDK_VERSION, &scd, &swap, &dev, &level, &ctx))) {
        DestroyWindow(hWnd); UnregisterClassA("DX11Dummy", wc.hInstance); return false;
    }

    void** vTable = *reinterpret_cast<void***>(swap);
    void* presentAddr = vTable[8]; // Present index

    swap->Release(); dev->Release(); ctx->Release();
    DestroyWindow(hWnd); UnregisterClassA("DX11Dummy", wc.hInstance);

    return (MH_CreateHook(presentAddr, &hkPresent, reinterpret_cast<void**>(&oPresent)) == MH_OK) && 
           (MH_EnableHook(presentAddr) == MH_OK);
}

// Entry Point Thread [主线程入口]
void MainThread(HMODULE hModule) {
    AllocConsole(); FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);
    std::cout << "[Init] Universal Mod Loading..." << std::endl;

    MH_Initialize();

    if (HookDX9())  std::cout << "[Init] DX9 Hook Attached." << std::endl;
    if (HookDX11()) std::cout << "[Init] DX11 Hook Attached." << std::endl;

    if (MH_CreateHook((void*)ADDR_FUNC_RENDER, &hkRenderLights, (void**)&oRenderLights) == MH_OK) {
        MH_EnableHook((void*)ADDR_FUNC_RENDER);
        std::cout << "[Init] Logic Hook Attached." << std::endl;
    }

    std::cout << "[Ready] Waiting for game render..." << std::endl;

    while (!GetAsyncKeyState(VK_END)) Sleep(100);
    
    MH_DisableHook(MH_ALL_HOOKS); 
    MH_Uninitialize(); 
    FreeConsole(); 
    FreeLibraryAndExitThread(hModule, 0);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) 
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)MainThread, hModule, 0, nullptr);
    return TRUE;
}