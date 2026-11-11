// HookPortX64Stub.cpp - temporary stand-in for HookPort.cpp in the x64 build.
//
// HookPort's syscall hook engine relies on KiFastSystemCall and x86 inline
// assembly, neither of which exists on x64. Until the x64 HookEngine lands
// (Phase 3 of docs/superpowers/specs/2026-11-11-core-modularization-and-x64-design.md),
// the x64 build links these stubs so it compiles; every entry point reports
// STATUS_NOT_SUPPORTED and no syscall is ever hooked.
//
// Only compiled for x64 (see LocaleEmulator.vcxproj).

#include "HookPort.h"

#if !defined(_M_X64)
#error "HookPortX64Stub.cpp is for the x64 build only; x86 uses HookPort.cpp"
#endif

NTSTATUS InstallHookPort(PLDR_MODULE SysCallModule, ULONG Flags)
{
    UNREFERENCED_PARAMETER(SysCallModule);
    UNREFERENCED_PARAMETER(Flags);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS HpInitializeWin32kPort(VOID)
{
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS HpAddSystemServiceTable(PHP_SYSTEM_SERVICE_TABLE ServiceTable)
{
    UNREFERENCED_PARAMETER(ServiceTable);
    return STATUS_NOT_SUPPORTED;
}

PVOID HpDuplicateHookPort(PVOID NtBase)
{
    UNREFERENCED_PARAMETER(NtBase);
    return NULL;
}

NTSTATUS UnInstallHookPort(VOID)
{
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS HpAddSystemCallByRoutine(PVOID Routine, ULONG RoutineHash)
{
    UNREFERENCED_PARAMETER(Routine);
    UNREFERENCED_PARAMETER(RoutineHash);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS HpAddSystemCallByRoutineRange(PVOID* Routine, PULONG RoutineHash, ULONG_PTR Count)
{
    UNREFERENCED_PARAMETER(Routine);
    UNREFERENCED_PARAMETER(RoutineHash);
    UNREFERENCED_PARAMETER(Count);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS HpAddSystemCall(PSYSCALL_INFO SystemCall, ULONG_PTR Count)
{
    UNREFERENCED_PARAMETER(SystemCall);
    UNREFERENCED_PARAMETER(Count);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS HpAddSystemCallFilter(ULONG RoutineHash, PVOID Routine, PVOID Context)
{
    UNREFERENCED_PARAMETER(RoutineHash);
    UNREFERENCED_PARAMETER(Routine);
    UNREFERENCED_PARAMETER(Context);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS HpRemoveSystemCallFilter(ULONG RoutineHash, PVOID Routine)
{
    UNREFERENCED_PARAMETER(RoutineHash);
    UNREFERENCED_PARAMETER(Routine);
    return STATUS_NOT_SUPPORTED;
}

PSYSCALL_INFO HpLookupSystemCall(ULONG_PTR SystemCallHash)
{
    UNREFERENCED_PARAMETER(SystemCallHash);
    return NULL;
}

NTSTATUS HpSetGlobalFilter(PHP_GLOBAL_FILTER NewFilter, PHP_GLOBAL_FILTER OldFilter)
{
    UNREFERENCED_PARAMETER(NewFilter);
    UNREFERENCED_PARAMETER(OldFilter);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS HpQueryValue(HpValueType Type, PVOID* Value)
{
    UNREFERENCED_PARAMETER(Type);
    UNREFERENCED_PARAMETER(Value);
    return STATUS_NOT_SUPPORTED;
}
