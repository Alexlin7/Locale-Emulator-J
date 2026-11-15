#include "stdafx.h"

HANDLE g_LeDiagLogFile;

static LARGE_INTEGER g_LeDiagLogStart, g_LeDiagLogFrequency;

#define LE_DIAG_LOG_VARIABLE    L"LE_DIAG_LOG"
#define LE_DIAG_LOG_SUBDIR      L"LocaleEmulator"
#define LE_DIAG_LOG_LINE_MAX    1024

// No %S: converting an ANSI argument would go through the Rtl* conversions LE hooks.
#define LE_DIAG_WIDE2(s)        L##s
#define LE_DIAG_WIDE(s)         LE_DIAG_WIDE2(s)

// Copies an environment variable of this process into Buffer (null terminated).
static NTSTATUS LeDiagQueryVariable(PCWSTR Name, PWSTR Buffer, ULONG_PTR Count)
{
    NTSTATUS        Status;
    UNICODE_STRING  VariableName, Value;

    RtlInitUnicodeString(&VariableName, Name);

    Value.Buffer        = Buffer;
    Value.Length        = 0;
    Value.MaximumLength = (USHORT)((Count - 1) * sizeof(WCHAR));

    Status = RtlQueryEnvironmentVariable_U(nullptr, &VariableName, &Value);
    if (NT_FAILED(Status))
        return Status;

    Buffer[Value.Length / sizeof(WCHAR)] = 0;

    return Value.Length != 0 ? STATUS_SUCCESS : STATUS_VARIABLE_NOT_FOUND;
}

static NTSTATUS LeDiagCreateFile(PHANDLE Handle, PCWSTR DosPath, ACCESS_MASK Access, ULONG Disposition, ULONG Options)
{
    NTSTATUS            Status;
    UNICODE_STRING      NtPath;
    OBJECT_ATTRIBUTES   ObjectAttributes;
    IO_STATUS_BLOCK     IoStatus;

    if (!RtlDosPathNameToNtPathName_U(DosPath, &NtPath, nullptr, nullptr))
        return STATUS_OBJECT_PATH_NOT_FOUND;

    InitializeObjectAttributes(&ObjectAttributes, &NtPath, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

    Status = NtCreateFile(
                Handle,
                Access | SYNCHRONIZE,
                &ObjectAttributes,
                &IoStatus,
                nullptr,
                FILE_ATTRIBUTE_NORMAL,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                Disposition,
                Options | FILE_SYNCHRONOUS_IO_NONALERT,
                nullptr,
                0
            );

    RtlFreeUnicodeString(&NtPath);

    return Status;
}

static VOID LeDiagWriteRaw(HANDLE File, PVOID Buffer, ULONG Size)
{
    IO_STATUS_BLOCK IoStatus;

    // The handle has FILE_APPEND_DATA but not FILE_WRITE_DATA: every write goes
    // to the end of the file, whichever thread makes it.
    NtWriteFile(File, nullptr, nullptr, nullptr, &IoStatus, Buffer, Size, nullptr, nullptr);
}

VOID LeDiagLogOpen()
{
    NTSTATUS        Status;
    HANDLE          File, Directory;
    WCHAR           Value[MAX_NTPATH], Dir[MAX_NTPATH], Path[MAX_NTPATH];
    ULONG_PTR       Length;
    PLDR_MODULE     Exe, Self;
    PPEB_BASE       Peb;
    UNICODE_STRING  ExeName;

    if (g_LeDiagLogFile != nullptr)
        return;

    if (NT_FAILED(LeDiagQueryVariable(LE_DIAG_LOG_VARIABLE, Value, countof(Value))))
        return;

    if (_wcsicmp(Value, L"0") == 0 || _wcsicmp(Value, L"off") == 0 || _wcsicmp(Value, L"false") == 0 || _wcsicmp(Value, L"no") == 0)
        return;

    // Only an absolute path (X:\... or \\server\...) is used as the directory. Anything
    // else ("1", "on", "true", a relative path) means the default directory, so the
    // log never lands in the game's current directory.
    BOOL Absolute = (Value[0] != 0 && Value[1] == ':' && Value[2] == '\\') || (Value[0] == '\\' && Value[1] == '\\');

    if (!Absolute)
    {
        if (NT_FAILED(LeDiagQueryVariable(L"TEMP", Dir, countof(Dir))) &&
            NT_FAILED(LeDiagQueryVariable(L"TMP", Dir, countof(Dir))))
        {
            return;
        }

        Length = StrLengthW(Dir);
        while (Length != 0 && Dir[Length - 1] == '\\')
            Dir[--Length] = 0;

        if (_snwprintf(Dir + Length, countof(Dir) - Length - 1, L"\\" LE_DIAG_LOG_SUBDIR) < 0)
            return;

        Dir[countof(Dir) - 1] = 0;
    }
    else
    {
        CopyMemory(Dir, Value, sizeof(Dir));
    }

    Length = StrLengthW(Dir);
    while (Length != 0 && Dir[Length - 1] == '\\')
        Dir[--Length] = 0;

    // Create the last level of the directory if it does not exist yet.
    Status = LeDiagCreateFile(&Directory, Dir, FILE_LIST_DIRECTORY, FILE_OPEN_IF, FILE_DIRECTORY_FILE);
    if (NT_FAILED(Status))
        return;

    NtClose(Directory);

    Exe = FindLdrModuleByHandle(nullptr);
    if (Exe != nullptr)
        ExeName = Exe->BaseDllName;
    else
        RtlInitUnicodeString(&ExeName, L"unknown");

    if (_snwprintf(Path, countof(Path) - 1, L"%s\\%wZ.%u.log", Dir, &ExeName, (ULONG)CurrentPid()) < 0)
        return;

    Path[countof(Path) - 1] = 0;

    Status = LeDiagCreateFile(&File, Path, FILE_APPEND_DATA, FILE_OVERWRITE_IF, FILE_NON_DIRECTORY_FILE);
    if (NT_FAILED(Status))
        return;

    NtQueryPerformanceCounter(&g_LeDiagLogStart, &g_LeDiagLogFrequency);

    WCHAR Bom = BOM_UTF16_LE;
    LeDiagWriteRaw(File, &Bom, sizeof(Bom));

    g_LeDiagLogFile = File;

    Peb  = CurrentPeb();
    Self = FindLdrModuleByHandle(&__ImageBase);

    WriteLog(L"Locale Emulator diagnostic log");
    WriteLog(L"image: %wZ", &Peb->ProcessParameters->ImagePathName);
    WriteLog(L"pid: %u", (ULONG)CurrentPid());
    WriteLog(L"os: %u.%u.%u", Peb->OSMajorVersion, Peb->OSMinorVersion, (ULONG)Peb->OSBuildNumber);
    if (Self != nullptr)
        WriteLog(L"le: %wZ", &Self->FullDllName);
    WriteLog(L"built: " LE_DIAG_WIDE(__DATE__) L" " LE_DIAG_WIDE(__TIME__));
    WriteLog(L"mode: %s", IsLeLoader() ? L"loader" : L"target");
}

VOID LeDiagLogClose()
{
    HANDLE File;

    File = (HANDLE)_InterlockedExchangePointer((PVOID volatile *)&g_LeDiagLogFile, nullptr);
    if (File != nullptr)
        NtClose(File);
}

VOID __cdecl LeDiagLogWrite(PCWSTR Format, ...)
{
    HANDLE          File;
    WCHAR           Buffer[LE_DIAG_LOG_LINE_MAX];
    LONG            Prefix, Length, Capacity;
    LARGE_INTEGER   Now;
    ULONG           Milliseconds;
    va_list         Args;

    File = g_LeDiagLogFile;
    if (File == nullptr)
        return;

    Milliseconds = 0;
    NtQueryPerformanceCounter(&Now, nullptr);
    if (g_LeDiagLogFrequency.QuadPart != 0)
        Milliseconds = (ULONG)((Now.QuadPart - g_LeDiagLogStart.QuadPart) * 1000 / g_LeDiagLogFrequency.QuadPart);

    // Leave room for "\r\n".
    Capacity = (LONG)countof(Buffer) - 2;

    Prefix = _snwprintf(Buffer, Capacity, L"%6u.%03u [%5u] ", Milliseconds / 1000, Milliseconds % 1000, (ULONG)CurrentTid());
    if (Prefix < 0)
        Prefix = 0;

    va_start(Args, Format);
    Length = _vsnwprintf(Buffer + Prefix, Capacity - Prefix, Format, Args);
    va_end(Args);

    // ntdll's _vsnwprintf returns -1 when the text does not fit; keep what fits.
    if (Length < 0 || Length > Capacity - Prefix)
        Length = Capacity - Prefix;

    Length += Prefix;
    Buffer[Length++] = '\r';
    Buffer[Length++] = '\n';

    LeDiagWriteRaw(File, Buffer, Length * sizeof(WCHAR));
}
