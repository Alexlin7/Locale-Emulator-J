#ifndef _DIAGLOG_H_5b0e3c9a_6f1d_4c27_9a3e_2d8f4b7c1e60_
#define _DIAGLOG_H_5b0e3c9a_6f1d_4c27_9a3e_2d8f4b7c1e60_

// Diagnostic log switched on at run time by the LE_DIAG_LOG environment variable
// (plan: docs/superpowers/plans/2026-11-13-diagnostic-log.md).
//
//   unset, empty or "0"  off (default)
//   "1"                  %TEMP%\LocaleEmulator\<exe>.<pid>.log
//   anything else        <value>\<exe>.<pid>.log
//
// Uses ntdll only (NtCreateFile / NtWriteFile / _vsnwprintf), so it works before
// kernel32 is loaded and inside syscall filters. Every line is one NtWriteFile on
// an append-only handle, so threads need no lock. Lines are UTF-16LE and never go
// through the Rtl* code page conversions that LE hooks.

extern HANDLE g_LeDiagLogFile;

VOID LeDiagLogOpen();
VOID LeDiagLogClose();
VOID __cdecl LeDiagLogWrite(PCWSTR Format, ...);

ForceInline BOOL LeDiagLogEnabled()
{
    return g_LeDiagLogFile != nullptr;
}

#define WriteLog(...) do { if (LeDiagLogEnabled()) LeDiagLogWrite(__VA_ARGS__); } while (0)

#endif // _DIAGLOG_H_5b0e3c9a_6f1d_4c27_9a3e_2d8f4b7c1e60_
