#pragma once
#include <cstdint>

// COD4 Dvar 结构体 (根据内存 Dump 1:1 还原)
// 我们直接根据偏移量定义成员，不再使用嵌套结构，防止对齐错误
struct dvar_s
{
    const char* name;           // 0x00
    const char* description;    // 0x04
    int flags;                  // 0x08
    int type;                   // 0x0C (Dump显示为2，即Integer)

    // --- Current Value 区块 (0x10 - 0x1F) ---
    int current_pad[3];         // 0x10, 0x14, 0x18 (跳过这12个字节的杂项)
    int current_int;            // 0x1C (这里才是真正的当前值！Dump显示为2)

    // --- Latched Value 区块 (0x20 - 0x2F) ---
    int latched_pad[3];         // 0x20, 0x24, 0x28 (跳过)
    int latched_int;            // 0x2C (这里是重启后的值！Dump显示为4)

    // --- Reset Value 区块 ---
    int reset_pad[3];           // 0x30, 0x34, 0x38
    int reset_int;              // 0x3C
};