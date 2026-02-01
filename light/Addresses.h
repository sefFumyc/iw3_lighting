#pragma once
#include <cstdint>

// COD4 1.7 (IW3) Memory Addresses & Offsets
namespace Addresses
{
    // ==========================================
    // 核心函数 (Functions)
    // ==========================================

    constexpr uintptr_t Dvar_RegisterNew = 0x00629D70;
    constexpr uintptr_t RB_DrawScene = 0x005F98E0;
    constexpr uintptr_t R_AddDynamicLightToScene = 0x006368B0;

    // ==========================================
    // 数据指针 (Data Pointers)
    // ==========================================

    constexpr uintptr_t r_dlightLimit_Ptr = 0x0D5696B0;
    constexpr uintptr_t Asset_LightDynamic_Str = 0x0CC9A2AC;
}