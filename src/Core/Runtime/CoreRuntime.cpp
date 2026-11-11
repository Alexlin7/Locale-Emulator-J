// CoreRuntime.cpp - minimal runtime support for the CRT-less Core binaries.
//
// Replaces the prebuilt MyLib.lib that shipped with the original Locale-Emulator-Core:
//   - __delayLoadHelper2: delay-load helper built on ntdll's loader APIs only, so the
//     injected DLL never needs kernel32's LoadLibrary/GetProcAddress (or Delayimp.lib,
//     which requires the CRT's load config) to resolve delay-loaded imports.
//   - _except_handler4: SEH frame handler emitted by the compiler for __try/__except,
//     forwarded to ntdll's _except_handler4_common.
//
// The SEH handlers and _alloca_probe_16 are x86 only. On x64, __try/__except uses
// table-based SEH (.pdata/.xdata) dispatched through ntdll's __C_specific_handler
// (ImportLibs\x64\ntdll_ex.def), and _alloca expands to __chkstk, so nothing
// architecture specific is implemented here for x64.
//
// All Core sources compile with /Gr (__fastcall default), so every function here
// states its calling convention explicitly.

#include <windows.h>
#include <winternl.h>
#include <delayimp.h>

#ifndef STATUS_INVALID_IMAGE_FORMAT
#define STATUS_INVALID_IMAGE_FORMAT ((NTSTATUS)0xC000007BL)
#endif

extern "C" IMAGE_DOS_HEADER __ImageBase;

template<typename T>
static T* RvaToPtr(RVA Rva)
{
    return (T*)((PBYTE)&__ImageBase + Rva);
}

extern "C" {

NTSYSAPI NTSTATUS NTAPI LdrLoadDll(PWSTR SearchPath, PULONG DllCharacteristics, PUNICODE_STRING DllName, PVOID *BaseAddress);
NTSYSAPI NTSTATUS NTAPI LdrUnloadDll(PVOID BaseAddress);
NTSYSAPI NTSTATUS NTAPI LdrGetProcedureAddress(PVOID BaseAddress, PANSI_STRING Name, ULONG Ordinal, PVOID *ProcedureAddress);
NTSYSAPI VOID     NTAPI RtlRaiseStatus(NTSTATUS Status);

/************************************************************************
  delay-load helper
************************************************************************/

static PVOID LoadDelayDll(PCImgDelayDescr Descriptor)
{
    HMODULE*        ModuleHandle = RvaToPtr<HMODULE>(Descriptor->rvaHmod);
    PVOID           Module = *ModuleHandle;
    ANSI_STRING     AnsiName;
    UNICODE_STRING  UnicodeName;
    NTSTATUS        Status;

    if (Module != NULL)
        return Module;

    RtlInitAnsiString(&AnsiName, RvaToPtr<CHAR>(Descriptor->rvaDLLName));
    Status = RtlAnsiStringToUnicodeString(&UnicodeName, &AnsiName, TRUE);
    if (!NT_SUCCESS(Status))
        RtlRaiseStatus(Status);

    Status = LdrLoadDll(NULL, NULL, &UnicodeName, &Module);
    RtlFreeUnicodeString(&UnicodeName);
    if (!NT_SUCCESS(Status))
        RtlRaiseStatus(Status);

    // Another thread may have resolved the same DLL concurrently; keep one reference.
    PVOID Previous = InterlockedCompareExchangePointer((PVOID volatile *)ModuleHandle, Module, NULL);
    if (Previous != NULL)
    {
        LdrUnloadDll(Module);
        Module = Previous;
    }

    return Module;
}

FARPROC WINAPI __delayLoadHelper2(PCImgDelayDescr Descriptor, FARPROC *ImportAddress)
{
    // Only the RVA-based descriptor format (dlattrRva) is produced by any linker
    // since VC 7; the old VA-based format is not supported.
    if ((Descriptor->grAttrs & dlattrRva) == 0)
        RtlRaiseStatus(STATUS_INVALID_IMAGE_FORMAT);

    PVOID Module = LoadDelayDll(Descriptor);

    PIMAGE_THUNK_DATA   AddressTable = RvaToPtr<IMAGE_THUNK_DATA>(Descriptor->rvaIAT);
    PIMAGE_THUNK_DATA   NameTable = RvaToPtr<IMAGE_THUNK_DATA>(Descriptor->rvaINT);
    ULONG_PTR           Index = (PIMAGE_THUNK_DATA)ImportAddress - AddressTable;
    PIMAGE_THUNK_DATA   Thunk = &NameTable[Index];
    PVOID               Procedure = NULL;
    NTSTATUS            Status;

    if (IMAGE_SNAP_BY_ORDINAL(Thunk->u1.Ordinal))
    {
        Status = LdrGetProcedureAddress(Module, NULL, (ULONG)IMAGE_ORDINAL(Thunk->u1.Ordinal), &Procedure);
    }
    else
    {
        ANSI_STRING ProcedureName;
        // For an import by name AddressOfData holds an RVA; on x64 the field is
        // 64 bits wide, so the narrowing cast to RVA is intentional.
        PIMAGE_IMPORT_BY_NAME ImportByName = RvaToPtr<IMAGE_IMPORT_BY_NAME>((RVA)Thunk->u1.AddressOfData);

        RtlInitAnsiString(&ProcedureName, ImportByName->Name);
        Status = LdrGetProcedureAddress(Module, &ProcedureName, 0, &Procedure);
    }

    if (!NT_SUCCESS(Status))
        RtlRaiseStatus(Status);

    *ImportAddress = (FARPROC)Procedure;
    return (FARPROC)Procedure;
}

/************************************************************************
  compiler support symbols
************************************************************************/

// Referenced by any object file that uses floating point.
int _fltused = 0x9875;

#if defined(_M_IX86)

void __cdecl _alloca_probe(void);   // ntdll export, see ImportLibs\x86\ntdll_ex.def

// Stack probe for 16-byte aligned _alloca: EAX = requested size on entry.
// Rounds the size up so that the new ESP is 16-byte aligned, then probes.
__declspec(naked) void __cdecl _alloca_probe_16(void)
{
    __asm
    {
        push    ecx
        lea     ecx, [esp + 8]      // ESP of the caller before the call
        sub     ecx, eax            // new ESP
        and     ecx, 15             // misalignment
        add     eax, ecx            // grow the allocation by the misalignment
        sbb     ecx, ecx            // ECX = -1 on overflow
        or      eax, ecx            // saturate the size on overflow
        pop     ecx
        jmp     _alloca_probe
    }
}

/************************************************************************
  SEH (__try / __except) support
************************************************************************/

// Referenced by every function that uses __try: the compiler XORs the frame's
// scope table pointer with this value. Core is built with /GS-, so there are no
// stack cookies to check and the value only has to stay constant.
UINT_PTR __security_cookie = 0xBB40E64E;

/*
  _except_handler3: the frame handler the compiler uses for __try when /GS- is
  in effect (with /GS it uses _except_handler4 below). ntdll does not export
  it, so it is implemented here.

  Frame layout set up by the function prologue (all EBP-relative):
      [ebp-18h]   ESP saved after the prologue (restored by the __except block)
      [ebp-14h]   PEXCEPTION_POINTERS for GetExceptionInformation/GetExceptionCode
      [ebp-10h]   EH3_REGISTRATION (linked into fs:[0])
      [ebp+00h]   saved EBP

  Filters and __finally blocks are funclets that expect EBP to be the frame's
  EBP and return with RET; an __except block is jumped to and restores ESP itself.
*/

typedef struct EH3_SCOPETABLE_ENTRY
{
    LONG    PreviousTryLevel;
    PVOID   Filter;     // NULL for __finally
    PVOID   Handler;    // __except block, or the __finally funclet
} EH3_SCOPETABLE_ENTRY;

typedef struct EH3_REGISTRATION
{
    struct EH3_REGISTRATION*    Next;
    PVOID                       ExceptionHandler;
    EH3_SCOPETABLE_ENTRY*       ScopeTable;
    LONG                        TryLevel;
} EH3_REGISTRATION;

#define EH3_TRYLEVEL_NONE           (-1)
#define EH3_FRAME_EBP(Registration) ((ULONG_PTR)(Registration) + 0x10)
#define EH3_XPOINTERS(Registration) (((PEXCEPTION_POINTERS *)(Registration))[-1])

// Calls a filter or __finally funclet with EBP set to the frame's EBP; returns its EAX.
static __declspec(naked) LONG __stdcall Eh3CallFunclet(PVOID Funclet, ULONG_PTR FrameEbp)
{
    __asm
    {
        push    ebp
        push    ebx
        push    esi
        push    edi
        mov     eax, [esp + 20]     // Funclet
        mov     ebp, [esp + 24]     // FrameEbp
        call    eax
        pop     edi
        pop     esi
        pop     ebx
        pop     ebp
        ret     8
    }
}

// Transfers control to an __except block. Does not return.
static __declspec(naked) void __stdcall Eh3JumpToHandler(PVOID Handler, ULONG_PTR FrameEbp)
{
    __asm
    {
        mov     eax, [esp + 4]      // Handler
        mov     ebp, [esp + 8]      // FrameEbp
        jmp     eax
    }
}

// Unwinds every frame registered above TargetFrame (running their termination
// handlers). RtlUnwind does not preserve EBX/ESI/EDI/EBP on x86, hence the asm.
static __declspec(naked) void __stdcall Eh3GlobalUnwind(PVOID TargetFrame, PVOID RtlUnwindRoutine)
{
    __asm
    {
        push    ebp
        push    ebx
        push    esi
        push    edi
        push    0                   // ReturnValue
        push    0                   // ExceptionRecord
        push    offset Unwound      // TargetIp
        push    [esp + 32]          // TargetFrame
        call    [esp + 40]          // RtlUnwind(TargetFrame, TargetIp, NULL, 0)
Unwound:
        pop     edi
        pop     esi
        pop     ebx
        pop     ebp
        ret     8
    }
}

// Runs the __finally blocks of Registration's frame down to (excluding) StopLevel.
static void Eh3LocalUnwind(EH3_REGISTRATION *Registration, LONG StopLevel)
{
    while (Registration->TryLevel != EH3_TRYLEVEL_NONE && Registration->TryLevel != StopLevel)
    {
        EH3_SCOPETABLE_ENTRY *Entry = &Registration->ScopeTable[Registration->TryLevel];

        Registration->TryLevel = Entry->PreviousTryLevel;

        if (Entry->Filter == NULL)
            Eh3CallFunclet(Entry->Handler, EH3_FRAME_EBP(Registration));
    }
}

EXCEPTION_DISPOSITION __cdecl _except_handler3(
    PEXCEPTION_RECORD   ExceptionRecord,
    PVOID               EstablisherFrame,
    PCONTEXT            ContextRecord,
    PVOID               DispatcherContext
)
{
    EH3_REGISTRATION *Registration = (EH3_REGISTRATION *)EstablisherFrame;

    UNREFERENCED_PARAMETER(DispatcherContext);

    if (ExceptionRecord->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
    {
        Eh3LocalUnwind(Registration, EH3_TRYLEVEL_NONE);
        return ExceptionContinueSearch;
    }

    EXCEPTION_POINTERS ExceptionPointers = { ExceptionRecord, ContextRecord };
    EH3_XPOINTERS(Registration) = &ExceptionPointers;

    for (LONG TryLevel = Registration->TryLevel; TryLevel != EH3_TRYLEVEL_NONE; )
    {
        EH3_SCOPETABLE_ENTRY *Entry = &Registration->ScopeTable[TryLevel];

        if (Entry->Filter != NULL)
        {
            LONG Disposition = Eh3CallFunclet(Entry->Filter, EH3_FRAME_EBP(Registration));

            if (Disposition < 0)            // EXCEPTION_CONTINUE_EXECUTION
                return ExceptionContinueExecution;

            if (Disposition > 0)            // EXCEPTION_EXECUTE_HANDLER
            {
                Eh3GlobalUnwind(Registration, (PVOID)&RtlUnwind);
                Eh3LocalUnwind(Registration, TryLevel);
                Registration->TryLevel = Entry->PreviousTryLevel;
                Eh3JumpToHandler(Entry->Handler, EH3_FRAME_EBP(Registration));
            }
        }

        TryLevel = Entry->PreviousTryLevel;
    }

    return ExceptionContinueSearch;
}

static void __fastcall NoCookieCheck(UINT_PTR)
{
}

EXCEPTION_DISPOSITION __cdecl _except_handler4_common(
    PUINT_PTR           CookiePointer,
    void (__fastcall    *CookieCheckFunction)(UINT_PTR),
    PEXCEPTION_RECORD   ExceptionRecord,
    PVOID               EstablisherFrame,
    PCONTEXT            ContextRecord,
    PVOID               DispatcherContext
);

EXCEPTION_DISPOSITION __cdecl _except_handler4(
    PEXCEPTION_RECORD   ExceptionRecord,
    PVOID               EstablisherFrame,
    PCONTEXT            ContextRecord,
    PVOID               DispatcherContext
)
{
    return _except_handler4_common(&__security_cookie, NoCookieCheck, ExceptionRecord, EstablisherFrame, ContextRecord, DispatcherContext);
}

#elif defined(_M_X64)

// x64: SEH frames are described by .pdata/.xdata and dispatched through ntdll's
// __C_specific_handler. Core builds with /GS-, so __security_cookie and
// __GSHandlerCheck are not referenced. _alloca is expanded inline with __chkstk.

#else
#error "Core supports x86 and x64 only"
#endif

} // extern "C"
