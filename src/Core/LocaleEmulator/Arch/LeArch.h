#ifndef _LEARCH_H_
#define _LEARCH_H_

// Architecture-specific TEB / PEB offsets used by Locale Emulator.
// The values are locked against ml.h's TEB_BASE / PEB_BASE at compile time by
// Arch\LayoutAsserts.cpp, so a wrong structure definition fails the build.

#if defined(_M_X64)
    #define LE_TEB_SELF_OFFSET      0x30
    #define LE_TEB_PEB_OFFSET       0x60
    #define LE_PEB_ANSI_CP_OFFSET   0x0A0
    #define LE_PEB_OEM_CP_OFFSET    0x0A8
    #define LE_PEB_CASE_OFFSET      0x0B0
#elif defined(_M_IX86)
    #define LE_TEB_SELF_OFFSET      0x18
    #define LE_TEB_PEB_OFFSET       0x30
    #define LE_PEB_ANSI_CP_OFFSET   0x058
    #define LE_PEB_OEM_CP_OFFSET    0x05C
    #define LE_PEB_CASE_OFFSET      0x060
#else
    #error "Locale Emulator Core supports x86 and x64 only"
#endif

// Offsets inside a 32-bit / 64-bit PEB, independent of the current build.
// Used when patching the PEB of another process.
#define LE_PEB32_ANSI_CP_OFFSET     0x058
#define LE_PEB32_OEM_CP_OFFSET      0x05C
#define LE_PEB32_CASE_OFFSET        0x060
#define LE_PEB64_ANSI_CP_OFFSET     0x0A0
#define LE_PEB64_OEM_CP_OFFSET      0x0A8
#define LE_PEB64_CASE_OFFSET        0x0B0

#endif // _LEARCH_H_
