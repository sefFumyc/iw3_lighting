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

// Link Libraries
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =============================================================
// Section 1: Constants & Structures
// =============================================================

#define ADDR_LIGHT_COUNT    0x00D0244AC
#define ADDR_LIGHT_ARRAY    0x00D0244B0
#define ADDR_LIGHT_MATERIAL 0x00CC9A2AC
#define ADDR_FUNC_RENDER    0x00634DB0
#define HARDCODED_MAT_PTR   0xF0D43C 

struct LightConfig
{
    bool  enable = false;
    int   type = 3;
    float pos[3] = { 0, 0, 0 };
    float color[3] = { 1, 1, 1 };
    float radius = 300.0f;
    float pitch = -90.0f;
    float yaw = 0.0f;
    float cutoff = 45.0f;

    bool  flicker = false;
    int   flickerMode = 0;
    float flickerAmp = 50.0f;
    float flickerFreq = 2.0f;
};

// =============================================================
// Section 2: Global Variables
// =============================================================

LightConfig g_Lights[4];

// [FIX 2] Default to FALSE so mouse isn't hijacked on startup
// 默认关闭菜单，防止进游戏鼠标就乱飘
bool        g_ShowMenu = false;
bool        g_Init = false;

ImGuiContext* g_MyContext = NULL;

enum RendererType { RENDERER_NONE, RENDERER_DX9, RENDERER_DX11 };
RendererType g_CurrentRenderer = RENDERER_NONE;

typedef long(__stdcall* EndScene_t)(LPDIRECT3DDEVICE9);
static EndScene_t oEndScene = NULL;

typedef HRESULT(__stdcall* Present_t)(IDXGISwapChain*, UINT, UINT);
static Present_t oPresent = NULL;

typedef int(__cdecl* RenderLights_t)(void*);
static RenderLights_t oRenderLights = NULL;

ID3D11Device* g_pd3dDevice = NULL;
ID3D11DeviceContext* g_pd3dContext = NULL;
ID3D11RenderTargetView* g_mainRenderTargetView = NULL;

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
typedef LRESULT(CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);
WNDPROC oWndProc;

// =============================================================
// Section 3: Helper Functions
// =============================================================

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
// Section 4: Core Logic
// =============================================================

void InjectLights()
{
    int* pCount = (int*)ADDR_LIGHT_COUNT;
    int* pMatHandle = (int*)ADDR_LIGHT_MATERIAL;
    int matValue = (*pMatHandle != 0) ? *pMatHandle : HARDCODED_MAT_PTR;

    float time = (float)GetTickCount() / 1000.0f;

    for (int i = 0; i < 4; i++)
    {
        if (!g_Lights[i].enable || *pCount >= 32) continue;

        float wave = sin(time * g_Lights[i].flickerFreq);
        float finalRadius = g_Lights[i].radius;
        float finalColor[3] = { g_Lights[i].color[0], g_Lights[i].color[1], g_Lights[i].color[2] };

        if (g_Lights[i].flicker)
        {
            if (g_Lights[i].flickerMode == 0)
            {
                finalRadius += wave * g_Lights[i].flickerAmp;
                if (finalRadius < 0.0f) finalRadius = 0.0f;
            }
            else
            {
                float mult = 1.0f + (wave * (g_Lights[i].flickerAmp / 100.0f));
                if (mult < 0.0f) mult = 0.0f;
                finalColor[0] *= mult; finalColor[1] *= mult; finalColor[2] *= mult;
            }
        }

        uintptr_t slotAddr = ADDR_LIGHT_ARRAY + (*pCount * 64);
        char* pRaw = (char*)slotAddr;
        float* pFloat = (float*)slotAddr;
        int* pInt = (int*)slotAddr;

        pRaw[0] = g_Lights[i].type;
        pFloat[1] = finalColor[0]; pFloat[2] = finalColor[1]; pFloat[3] = finalColor[2];
        pFloat[7] = g_Lights[i].pos[0]; pFloat[8] = g_Lights[i].pos[1]; pFloat[9] = g_Lights[i].pos[2];
        pFloat[10] = finalRadius;

        if (g_Lights[i].type == 2)
        {
            float dir[3];
            AngleVectors(g_Lights[i].pitch, g_Lights[i].yaw, dir);
            pFloat[4] = dir[0]; pFloat[5] = dir[1]; pFloat[6] = dir[2];
            float cosOuter = cos(g_Lights[i].cutoff * (float)(M_PI / 180.0f));
            pFloat[11] = cosOuter;
            pFloat[12] = cosOuter + 0.05f;
            if (pFloat[12] > 1.0f) pFloat[12] = 1.0f;
        }

        pInt[13] = 0; pInt[14] = -1; pInt[15] = matValue;
        (*pCount)++;
    }
}

// =============================================================
// Section 5: UI Rendering
// =============================================================

void DrawMenu()
{
    if (!g_ShowMenu) return;

    if (g_CurrentRenderer == RENDERER_DX11) ImGui_ImplDX11_NewFrame();
    else ImGui_ImplDX9_NewFrame();

    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowSize(ImVec2(400, 600), ImGuiCond_FirstUseEver);
    const char* title = (g_CurrentRenderer == RENDERER_DX11) ? "IW3 Light (DX11)" : "COD4 Mod (DX9)";

    ImGui::Begin(title);

    if (ImGui::CollapsingHeader("Global Settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Active Lights: %d / 32", *(int*)ADDR_LIGHT_COUNT);
        if (g_CurrentRenderer == RENDERER_DX11)
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Running in DX11 Mode");
        else
            ImGui::TextColored(ImVec4(0, 1, 1, 1), "Running in DX9 Mode");
    }
    ImGui::Separator();

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
                ImGui::Text("Type:"); ImGui::SameLine();
                ImGui::RadioButton("Point", &g_Lights[i].type, 3); ImGui::SameLine();
                ImGui::RadioButton("Spot", &g_Lights[i].type, 2);
                ImGui::Spacing();

                ImGui::DragFloat3("Position", g_Lights[i].pos, 5.0f);
                ImGui::ColorEdit3("Color Picker", g_Lights[i].color);
                ImGui::DragFloat3("HDR RGB Values", g_Lights[i].color, 0.05f, 0.0f, 100.0f);
                ImGui::Spacing();

                ImGui::DragFloat("Radius", &g_Lights[i].radius, 5.0f, 10.0f, 5000.0f);

                if (g_Lights[i].type == 2) {
                    ImGui::TextColored(ImVec4(0, 1, 1, 1), "Spot Settings:");
                    ImGui::SliderFloat("Pitch", &g_Lights[i].pitch, -180.0f, 180.0f);
                    ImGui::SliderFloat("Yaw", &g_Lights[i].yaw, -180.0f, 180.0f);
                    ImGui::SliderFloat("Cutoff", &g_Lights[i].cutoff, 1.0f, 89.0f);
                }

                ImGui::Separator();
                ImGui::Checkbox("FX: Flicker ", &g_Lights[i].flicker);
                if (g_Lights[i].flicker)
                {
                    ImGui::Indent();
                    ImGui::Text("Target:"); ImGui::SameLine();
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
// Section 6: Hook Functions (Input & Render)
// =============================================================

LRESULT __stdcall hkWndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // [FIX 1] Changed back to INSERT key for toggling
    if (uMsg == WM_KEYDOWN && wParam == VK_INSERT) {
        g_ShowMenu = !g_ShowMenu;
        if (g_Init && g_MyContext) ImGui::GetIO().MouseDrawCursor = g_ShowMenu;
    }

    // Only process ImGui if menu is OPEN
    if (g_ShowMenu && g_Init && g_MyContext) {
        ImGuiContext* oldCtx = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(g_MyContext);

        // [FIX 3] Mouse Passthrough Logic
        // ImGui_ImplWin32_WndProcHandler handles inputs.
        // We only return true (block input from game) if ImGui actually wants it.
        if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam)) {
            // Check if mouse is hovering our window. If not, let it pass to game/IWXMVM
            // io.WantCaptureMouse is true if mouse is over any ImGui window
            if (ImGui::GetIO().WantCaptureMouse) {
                ImGui::SetCurrentContext(oldCtx);
                return true; // Block input
            }
        }
        ImGui::SetCurrentContext(oldCtx);
    }

    // Always call original WndProc to let IWXMVM handle its own stuff
    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

int __cdecl hkRenderLights(void* a1) {
    InjectLights();
    return oRenderLights(a1);
}

long __stdcall hkEndScene(LPDIRECT3DDEVICE9 pDevice)
{
    ImGuiContext* oldCtx = ImGui::GetCurrentContext();

    if (!g_Init)
    {
        g_MyContext = ImGui::CreateContext();
        ImGui::SetCurrentContext(g_MyContext);

        ImGuiIO& io = ImGui::GetIO();
        // [FIX 2] Default False (Hidden)
        io.MouseDrawCursor = false;
        io.IniFilename = "light_mod_config.ini";

        D3DDEVICE_CREATION_PARAMETERS params; pDevice->GetCreationParameters(&params);
        ImGui_ImplWin32_Init(params.hFocusWindow);
        ImGui_ImplDX9_Init(pDevice);

        oWndProc = (WNDPROC)SetWindowLongPtr(params.hFocusWindow, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
        g_Init = true;
        g_CurrentRenderer = RENDERER_DX9;
    }

    if (g_MyContext) ImGui::SetCurrentContext(g_MyContext);

    // Update Mouse Cursor Visibility based on Menu State
    if (g_MyContext) ImGui::GetIO().MouseDrawCursor = g_ShowMenu;

    if (g_CurrentRenderer == RENDERER_DX9)
    {
        DrawMenu();
        if (g_ShowMenu) ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }

    if (oldCtx) ImGui::SetCurrentContext(oldCtx);

    return oEndScene(pDevice);
}

HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
    ImGuiContext* oldCtx = ImGui::GetCurrentContext();

    if (!g_Init)
    {
        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pd3dDevice)))
        {
            g_MyContext = ImGui::CreateContext();
            ImGui::SetCurrentContext(g_MyContext);

            ImGuiIO& io = ImGui::GetIO();
            // [FIX 2] Default False (Hidden)
            io.MouseDrawCursor = false;
            io.IniFilename = "light_mod_config.ini";

            g_pd3dDevice->GetImmediateContext(&g_pd3dContext);
            DXGI_SWAP_CHAIN_DESC sd; pSwapChain->GetDesc(&sd);

            ImGui_ImplWin32_Init(sd.OutputWindow);
            ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dContext);

            oWndProc = (WNDPROC)SetWindowLongPtr(sd.OutputWindow, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
            g_Init = true;
            g_CurrentRenderer = RENDERER_DX11;
        }
        else return oPresent(pSwapChain, SyncInterval, Flags);
    }

    if (g_MyContext) ImGui::SetCurrentContext(g_MyContext);

    // Update Mouse Cursor Visibility based on Menu State
    if (g_MyContext) ImGui::GetIO().MouseDrawCursor = g_ShowMenu;

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

    if (oldCtx) ImGui::SetCurrentContext(oldCtx);

    return oPresent(pSwapChain, SyncInterval, Flags);
}

// =============================================================
// Section 7: Stealth Initialization
// =============================================================

uintptr_t GetD3D9EndSceneOffset() {
    char path[MAX_PATH]; GetSystemDirectoryA(path, MAX_PATH); strcat_s(path, "\\d3d9.dll");
    char tempPath[MAX_PATH]; GetTempPathA(MAX_PATH, tempPath); strcat_s(tempPath, "d3d9_bypass.dll");
    CopyFileA(path, tempPath, FALSE);
    HMODULE hMod = LoadLibraryA(tempPath);
    if (!hMod) return 0;
    typedef IDirect3D9* (WINAPI* Direct3DCreate9_t)(UINT);
    Direct3DCreate9_t d3dCreate = (Direct3DCreate9_t)GetProcAddress(hMod, "Direct3DCreate9");
    if (!d3dCreate) { FreeLibrary(hMod); return 0; }
    IDirect3D9* pD3D = d3dCreate(D3D_SDK_VERSION);
    if (!pD3D) { FreeLibrary(hMod); return 0; }
    WNDCLASSEXA wc = { sizeof(WNDCLASSEXA), CS_CLASSDC, DefWindowProcA, 0L, 0L, hMod, NULL, NULL, NULL, NULL, "TempD3D", NULL };
    RegisterClassExA(&wc);
    HWND hWnd = CreateWindowA("TempD3D", NULL, WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
    D3DPRESENT_PARAMETERS d3dpp = {}; d3dpp.Windowed = TRUE; d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD; d3dpp.hDeviceWindow = hWnd;
    IDirect3DDevice9* pDevice = nullptr;
    pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDevice);
    uintptr_t offset = 0;
    if (pDevice) {
        void** vTable = *reinterpret_cast<void***>(pDevice);
        offset = (uintptr_t)vTable[42] - (uintptr_t)hMod;
        pDevice->Release();
    }
    pD3D->Release(); DestroyWindow(hWnd); UnregisterClassA("TempD3D", wc.hInstance); FreeLibrary(hMod); DeleteFileA(tempPath);
    return offset;
}

bool HookDX9()
{
    uintptr_t offset = GetD3D9EndSceneOffset();
    if (offset == 0) return false;
    HMODULE hRealD3D9 = GetModuleHandleA("d3d9.dll");
    void* pRealEndScene = (void*)((uintptr_t)hRealD3D9 + offset);
    return (MH_CreateHook(pRealEndScene, &hkEndScene, reinterpret_cast<void**>(&oEndScene)) == MH_OK) &&
        (MH_EnableHook(pRealEndScene) == MH_OK);
}

bool HookDX11()
{
    if (!GetModuleHandleA("d3d11.dll")) return false;
    const char* dummyName = "LightMod_DX11_Dummy";
    WNDCLASSEXA wc = { sizeof(WNDCLASSEXA), CS_CLASSDC, DefWindowProcA, 0L, 0L, GetModuleHandleA(NULL), NULL, NULL, NULL, NULL, dummyName, NULL };
    RegisterClassExA(&wc);
    HWND hWnd = CreateWindowA(dummyName, NULL, WS_OVERLAPPEDWINDOW, 100, 100, 300, 300, NULL, NULL, wc.hInstance, NULL);

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
    D3D_FEATURE_LEVEL level;
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1; scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hWnd; scd.SampleDesc.Count = 1; scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD; scd.Windowed = TRUE;

    ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr; IDXGISwapChain* swap = nullptr;
    if (FAILED(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, levels, 2, D3D11_SDK_VERSION, &scd, &swap, &dev, &level, &ctx))) {
        DestroyWindow(hWnd); UnregisterClassA(dummyName, wc.hInstance); return false;
    }
    void** vTable = *reinterpret_cast<void***>(swap);
    void* presentAddr = vTable[8];
    swap->Release(); dev->Release(); ctx->Release();
    DestroyWindow(hWnd); UnregisterClassA(dummyName, wc.hInstance);

    return (MH_CreateHook(presentAddr, &hkPresent, reinterpret_cast<void**>(&oPresent)) == MH_OK) &&
        (MH_EnableHook(presentAddr) == MH_OK);
}

void MainThread(HMODULE hModule) {
    AllocConsole(); FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);
    std::cout << "[Init] Universal Stealth Mod Loading..." << std::endl;
    std::cout << "[Init] Waiting for other mods to stabilize (3s)..." << std::endl;
    Sleep(3000);

    MH_Initialize();

    if (HookDX9())  std::cout << "[Init] DX9 Hook Attached (STEALTH MODE)." << std::endl;
    if (HookDX11()) std::cout << "[Init] DX11 Hook Attached." << std::endl;

    if (MH_CreateHook((void*)ADDR_FUNC_RENDER, &hkRenderLights, (void**)&oRenderLights) == MH_OK) {
        MH_EnableHook((void*)ADDR_FUNC_RENDER);
        std::cout << "[Init] Logic Hook Attached." << std::endl;
    }

    std::cout << "[Ready] Waiting for game render..." << std::endl;
    while (!GetAsyncKeyState(VK_END)) Sleep(100);
    MH_DisableHook(MH_ALL_HOOKS); MH_Uninitialize(); FreeConsole(); FreeLibraryAndExitThread(hModule, 0);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)MainThread, hModule, 0, nullptr);
    return TRUE;
}