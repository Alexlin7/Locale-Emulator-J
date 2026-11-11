#include "winstub.h"

namespace Le
{
    // A project-defined function that merely shares a name: not a kernel32 call.
    UINT GetACP() { return 932; }
}

void Violations(const char *Ansi, wchar_t *Unicode)
{
    // Direct call.
    MultiByteToWideChar(0, 0, Ansi, -1, Unicode, 16); // $ Alert

    // Run-time lookup through the KERNEL32_API macro (User32Hook.cpp style).
    KERNEL32_API(MultiByteToWideChar)(0, 0, Ansi, -1, Unicode, 16); // $ Alert

    // Run-time lookup by export hash.
    PVOID p = LookupExportTable(GetKernel32Handle(), KERNEL32_GetACP); // $ Alert

    // Run-time lookup by export name.
    PVOID q = GetRoutineAddress(GetKernel32Handle(), "WideCharToMultiByte"); // $ Alert

    (void)p; (void)q;
}

void Allowed(const char *Ansi, wchar_t *Unicode, HANDLE Memory)
{
    unsigned long Bytes;

    // ntdll conversion: the sanctioned way.
    RtlMultiByteToUnicodeN(Unicode, 32, &Bytes, Ansi, 16);

    // kernel32, but not a code page / locale API.
    KERNEL32_API(GlobalLock)(Memory);

    // Project function with the same name.
    Le::GetACP();

    // Explicit, reviewed exemption on the previous line.
    // LE-ALLOW-KERNELBASE: test of the documented exemption.
    KERNEL32_API(GetACP)();

    KERNEL32_API(GetACP)(); // LE-ALLOW-KERNELBASE: exemption on the same line.
}

void NotExempt()
{
    // LE-ALLOW-KERNELBASE: only applies to the next line.

    GetACP(); // $ Alert
}
