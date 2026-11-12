//#pragma comment(linker, "/ENTRY:DllMain")
#pragma comment(linker, "/SECTION:.text,ERW /MERGE:.rdata=.text /MERGE:.data=.text")
#pragma comment(linker, "/SECTION:.Asuna,ERW /MERGE:.text=.Asuna")
//#pragma comment(linker, "/EXPORT:LeCreateProcess=_LeCreateProcess@44")

#include "ml.cpp"
#include "LoaderDll.h"

// The x64 Core injects LocaleEmulator64.dll; both builds live in the same folder.
#if defined(_M_X64)
#define LE_INJECTED_DLL_NAME L"LocaleEmulator64.dll"
#else
#define LE_INJECTED_DLL_NAME L"LocaleEmulator.dll"
#endif

EXTC
NTSTATUS
NTAPI
LeCreateProcess2(
	PLEB                    Leb,
	PCWSTR                  ApplicationName,
	PWSTR                   CommandLine,
	PCWSTR                  CurrentDirectory,
	ULONG                   CreationFlags,
	LPSTARTUPINFOW          StartupInfo,
	PML_PROCESS_INFORMATION ProcessInformation,
	LPSECURITY_ATTRIBUTES   ProcessAttributes,
	LPSECURITY_ATTRIBUTES   ThreadAttributes,
	PVOID                   Environment,
	HANDLE                  Token
	)
{
	ULONG_PTR               Length;
	PWSTR                   DllFullPath;
	PLDR_MODULE             Module;
	NTSTATUS                Status;
	ML_PROCESS_INFORMATION  ProcessInfo;

	static WCHAR Dll[] = LE_INJECTED_DLL_NAME;

	Module = FindLdrModuleByHandle(nullptr);

	Length = Module->FullDllName.Length - Module->BaseDllName.Length;
	DllFullPath = (PWSTR)AllocStack(Length + sizeof(Dll));
	CopyMemory(DllFullPath, Module->FullDllName.Buffer, Length);
	CopyStruct(PtrAdd(DllFullPath, Length), Dll, sizeof(Dll));

#if defined(_M_X64)
	// ml's CreateProcessWithDll / InjectDllBeforeKernel32Loaded only exist for
	// x86. The x64 injection path is part of a later phase of the x64 design.
	Status = STATUS_NOT_SUPPORTED;
#else
	Status = CreateProcessWithDll(
		CPWD_BEFORE_KERNEL32,
		DllFullPath,
		ApplicationName,
		CommandLine,
		CurrentDirectory,
		CreationFlags | CREATE_SUSPENDED,
		StartupInfo,
		&ProcessInfo,
		ProcessAttributes,
		ThreadAttributes,
		Environment,
		Token
		);
#endif

	if (NT_FAILED(Status))
		return Status;

	PLEPEB LePeb;

	LePeb = nullptr;

	LOOP_ONCE
	{
		if (Leb == nullptr)
		break;

	ULONG_PTR   ExtraSize;
	PVOID       MaximumAddress;
	PREGISTRY_REDIRECTION_ENTRY64 Entry64;

	ExtraSize = Leb->NumberOfRegistryRedirectionEntries * sizeof(Leb->RegistryReplacement[0]);

	if (ExtraSize != 0)
	{
		MaximumAddress = Leb->RegistryReplacement + Leb->NumberOfRegistryRedirectionEntries;
		FOR_EACH(Entry64, Leb->RegistryReplacement, Leb->NumberOfRegistryRedirectionEntries)
		{
			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd(Entry64->Original.SubKey.Buffer,      Leb),   Entry64->Original.SubKey.MaximumLength));
			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd(Entry64->Original.ValueName.Buffer,   Leb),   Entry64->Original.ValueName.MaximumLength));
			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd((PVOID)Entry64->Original.Data,        Leb),   Entry64->Original.DataSize));

			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd(Entry64->Redirected.SubKey.Buffer,    Leb),   Entry64->Redirected.SubKey.MaximumLength));
			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd(Entry64->Redirected.ValueName.Buffer, Leb),   Entry64->Redirected.ValueName.MaximumLength));
			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd((PVOID)Entry64->Redirected.Data,      Leb),   Entry64->Redirected.DataSize));
		}

		ExtraSize += PtrOffset(MaximumAddress, Leb);
	}

	LePeb = OpenOrCreateLePeb(ProcessInfo.dwProcessId, TRUE, ExtraSize);
	if (LePeb == nullptr)
	{
		Status = STATUS_NONE_MAPPED;
		break;
	}

	CopyMemory(&LePeb->Leb, Leb, FIELD_OFFSET(LEB, NumberOfRegistryRedirectionEntries) + ExtraSize);

	LePeb->LdrLoadDllAddress = GetCallDestination(ProcessInfo.FirstCallLdrLoadDll);
	LePeb->LdrLoadDllBackupSize = LDR_LOAD_DLL_BACKUP_SIZE;
	ReadMemory(ProcessInfo.hProcess, LePeb->LdrLoadDllAddress, LePeb->LdrLoadDllBackup, LDR_LOAD_DLL_BACKUP_SIZE);

	ULONG_PTR Length = (StrLengthW(DllFullPath) + 1) * sizeof(WCHAR);
	CopyMemory(LePeb->LeDllFullPath, DllFullPath, ML_MIN(Length, sizeof(LePeb->LeDllFullPath)));
	}

	CloseLePeb(LePeb);

	if (NT_SUCCESS(Status) && FLAG_OFF(CreationFlags, CREATE_SUSPENDED))
		Status = NtResumeProcess(ProcessInfo.hProcess);

	if (NT_FAILED(Status))
	{
		NtTerminateProcess(ProcessInfo.hProcess, Status);
		NtClose(ProcessInfo.hProcess);
		NtClose(ProcessInfo.hThread);
	}
	else if (ProcessInformation != nullptr)
	{
		*ProcessInformation = ProcessInfo;
	}
	else
	{
		NtClose(ProcessInfo.hProcess);
		NtClose(ProcessInfo.hThread);
	}

	return Status;
}

EXTC
NTSTATUS
NTAPI
LeCreateProcess(
	PLEB                    Leb,
	PCWSTR                  ApplicationName,
	PWSTR                   CommandLine,
	PCWSTR                  CurrentDirectory,
	ULONG                   CreationFlags,
	LPSTARTUPINFOW          StartupInfo,
	PML_PROCESS_INFORMATION ProcessInformation,
	LPSECURITY_ATTRIBUTES   ProcessAttributes,
	LPSECURITY_ATTRIBUTES   ThreadAttributes,
	PVOID                   Environment,
	HANDLE                  Token
	)
{
	NTSTATUS                Status;
	PVOID                   LeDllHandle;
	ULONG_PTR               Length;
	PWSTR                   DllFullPath;
	PLDR_MODULE             Module;
	PLEPEB                  LePeb;

	static WCHAR Dll[] = LE_INJECTED_DLL_NAME;

	Module = FindLdrModuleByHandle(nullptr);

	Length = Module->FullDllName.Length - Module->BaseDllName.Length;
	DllFullPath = (PWSTR)AllocStack(Length + sizeof(Dll));
	CopyMemory(DllFullPath, Module->FullDllName.Buffer, Length);
	CopyStruct(PtrAdd(DllFullPath, Length), Dll, sizeof(Dll));

	LePeb = nullptr;

	LOOP_ONCE
	{
		if (Leb == nullptr)
		break;

	ULONG_PTR   ExtraSize;
	PVOID       MaximumAddress;
	PREGISTRY_REDIRECTION_ENTRY64 Entry64;

	ExtraSize = Leb->NumberOfRegistryRedirectionEntries * sizeof(Leb->RegistryReplacement[0]);

	if (ExtraSize != 0)
	{
		MaximumAddress = Leb->RegistryReplacement + Leb->NumberOfRegistryRedirectionEntries;
		FOR_EACH(Entry64, Leb->RegistryReplacement, Leb->NumberOfRegistryRedirectionEntries)
		{
			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd(Entry64->Original.SubKey.Buffer,      Leb),   Entry64->Original.SubKey.MaximumLength));
			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd(Entry64->Original.ValueName.Buffer,   Leb),   Entry64->Original.ValueName.MaximumLength));
			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd((PVOID)Entry64->Original.Data,        Leb),   Entry64->Original.DataSize));

			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd(Entry64->Redirected.SubKey.Buffer,    Leb),   Entry64->Redirected.SubKey.MaximumLength));
			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd(Entry64->Redirected.ValueName.Buffer, Leb),   Entry64->Redirected.ValueName.MaximumLength));
			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd((PVOID)Entry64->Redirected.Data,      Leb),   Entry64->Redirected.DataSize));
		}

		ExtraSize += PtrOffset(MaximumAddress, Leb);
	}

	LePeb = OpenOrCreateLePeb((ULONG_PTR)CurrentTeb()->ClientId.UniqueProcess, TRUE, ExtraSize);
	if (LePeb == nullptr)
	{
		Status = STATUS_NONE_MAPPED;
		break;
	}

	CopyMemory(&LePeb->Leb, Leb, FIELD_OFFSET(LEB, NumberOfRegistryRedirectionEntries) + ExtraSize);

	LePeb->LdrLoadDllAddress = LookupExportTable(GetNtdllHandle(), NTDLL_LdrLoadDll);
	LePeb->LdrLoadDllBackupSize = LDR_LOAD_DLL_BACKUP_SIZE;
	CopyMemory(LePeb->LdrLoadDllBackup, LePeb->LdrLoadDllAddress, LDR_LOAD_DLL_BACKUP_SIZE);

	ULONG_PTR Length = (StrLengthW(DllFullPath) + 1) * sizeof(WCHAR);
	CopyMemory(LePeb->LeDllFullPath, DllFullPath, ML_MIN(Length, sizeof(LePeb->LeDllFullPath)));

  Length = Module->FullDllName.Length + sizeof(WCHAR) - Module->BaseDllName.Length;

  Length = ML_MIN(Length, sizeof(LePeb->LeDllFullPath));
  CopyMemory(LePeb->LeDllDirPath, Module->FullDllName.Buffer, Length);
  LePeb->LeDllDirPath[Length / sizeof(WCHAR) - 1] = 0;
	}

	UNICODE_STRING DllFullPathString;
	TEB_ACTIVE_FRAME frame(LE_LOADER_PROCESS);

	frame.Data = (ULONG_PTR)LePeb;
	frame.Push();

	RtlInitUnicodeString(&DllFullPathString, DllFullPath);

	Status = LdrLoadDll(nullptr, nullptr, &DllFullPathString, &LeDllHandle);
	CloseLePeb(LePeb);

	FAIL_RETURN(Status);

	Status = Ps::CreateProcess(
		ApplicationName,
		CommandLine,
		CurrentDirectory,
		CreationFlags,
		StartupInfo,
		ProcessInformation,
		ProcessAttributes,
		ThreadAttributes,
		Environment,
		Token
		);

	return Status;
}

// ---------------------------------------------------------------------------
// LeCreateProcessDirect
//
// LeCreateProcess loads LocaleEmulator.dll into the *calling* process so that its
// NtCreateUserProcess hook injects the target. In LEProc (a CoreCLR process with
// GC / JIT / finalizer threads already running) that patches the shared WOW64
// syscall stub while other threads may be executing it, which crashes LEProc
// with 0xC0000005 about 1.5% of the time (CI run 43274267214).
//
// This entry point creates the target suspended and performs the same injection
// as LeGlobalData::InjectSelfToChildProcess (Hooks/NtdllHook.cpp), using a copy
// of LocaleEmulator.dll mapped from disk: nothing is loaded or hooked in the
// calling process. Keep the two in sync.
// ---------------------------------------------------------------------------

static
NTSTATUS
LeInjectIntoSuspendedProcess(
	HANDLE      Process,
	ULONG_PTR   ProcessId,
	PCWSTR      DllFullPath,
	PLEB        Leb
	)
{
	NTSTATUS    Status;
	PVOID       LocalImage, RemoteImage, LocalLoadFirstDll, RemoteLoadFirstDll, LdrLoadDllAddress;
	ULONG_PTR   SizeOfImage, ExtraSize, Length;
	BYTE        Backup[LDR_LOAD_DLL_BACKUP_SIZE];
	BYTE        Jump[5];
	PLEPEB      LePeb;

	// Same rule as the hook path: a 32-bit launcher cannot inject into a 64-bit
	// target. The caller still resumes the process, so it runs without LE.
	if (Ps::IsWow64Process() && !Ps::IsWow64Process(Process))
		return STATUS_NOT_SUPPORTED;

	// ---- Map LocaleEmulator.dll locally (no DllMain) and copy it into the target ----

	Status = LoadPeImage(DllFullPath, &LocalImage, nullptr, 0);
	FAIL_RETURN(Status);

	SizeOfImage = ImageGetSizeOfImage(LocalImage);

	LocalLoadFirstDll = LookupExportTable(LocalImage, "LoadFirstDll");
	if (LocalLoadFirstDll == nullptr)
	{
		UnloadPeImage(LocalImage);
		return STATUS_PROCEDURE_NOT_FOUND;
	}

	RemoteImage = nullptr;
	Status = AllocVirtualMemoryEx(Process, &RemoteImage, SizeOfImage);
	if (NT_FAILED(Status))
	{
		UnloadPeImage(LocalImage);
		return Status;
	}

	RemoteLoadFirstDll = PtrAdd(RemoteImage, PtrOffset(LocalLoadFirstDll, LocalImage));

	RelocPeImage(LocalImage, LocalImage, nullptr, RemoteImage);
	Status = WriteMemory(Process, RemoteImage, LocalImage, SizeOfImage);
	UnloadPeImage(LocalImage);

	if (NT_FAILED(Status))
	{
		Mm::FreeVirtualMemory(RemoteImage, Process);
		return Status;
	}

	// ntdll is mapped at the same address in every process of the same architecture.
	LdrLoadDllAddress = LookupExportTable(GetNtdllHandle(), NTDLL_LdrLoadDll);

	Status = ReadMemory(Process, LdrLoadDllAddress, Backup, sizeof(Backup));
	if (NT_FAILED(Status))
	{
		Mm::FreeVirtualMemory(RemoteImage, Process);
		return Status;
	}

	// ---- LePeb for the target (read by LoadSelfAsFirstDll / LeGlobalData::Initialize) ----

	ExtraSize = 0;
	if (Leb != nullptr && Leb->NumberOfRegistryRedirectionEntries != 0)
	{
		PVOID                           MaximumAddress;
		PREGISTRY_REDIRECTION_ENTRY64   Entry64;

		ExtraSize = Leb->NumberOfRegistryRedirectionEntries * sizeof(Leb->RegistryReplacement[0]);
		MaximumAddress = Leb->RegistryReplacement + Leb->NumberOfRegistryRedirectionEntries;
		FOR_EACH(Entry64, Leb->RegistryReplacement, Leb->NumberOfRegistryRedirectionEntries)
		{
			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd(Entry64->Original.SubKey.Buffer,      Leb),   Entry64->Original.SubKey.MaximumLength));
			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd(Entry64->Original.ValueName.Buffer,   Leb),   Entry64->Original.ValueName.MaximumLength));
			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd((PVOID)Entry64->Original.Data,        Leb),   Entry64->Original.DataSize));

			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd(Entry64->Redirected.SubKey.Buffer,    Leb),   Entry64->Redirected.SubKey.MaximumLength));
			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd(Entry64->Redirected.ValueName.Buffer, Leb),   Entry64->Redirected.ValueName.MaximumLength));
			MaximumAddress = ML_MAX(MaximumAddress, PtrAdd(PtrAdd((PVOID)Entry64->Redirected.Data,      Leb),   Entry64->Redirected.DataSize));
		}

		ExtraSize += PtrOffset(MaximumAddress, Leb);
	}

	// Duplicates the section handle into the target, which keeps the section alive
	// after we unmap it; the target closes that handle in LeGlobalData::Initialize.
	LePeb = OpenOrCreateLePeb(ProcessId, TRUE, ExtraSize);
	if (LePeb == nullptr)
	{
		Mm::FreeVirtualMemory(RemoteImage, Process);
		return STATUS_NONE_MAPPED;
	}

	if (Leb != nullptr)
		CopyMemory(&LePeb->Leb, Leb, FIELD_OFFSET(LEB, NumberOfRegistryRedirectionEntries) + ExtraSize);
	else
		InitDefaultLeb(&LePeb->Leb);

	LePeb->LdrLoadDllAddress = LdrLoadDllAddress;
	LePeb->LdrLoadDllBackupSize = LDR_LOAD_DLL_BACKUP_SIZE;
	CopyMemory(LePeb->LdrLoadDllBackup, Backup, LDR_LOAD_DLL_BACKUP_SIZE);

	Length = (StrLengthW(DllFullPath) + 1) * sizeof(WCHAR);
	CopyMemory(LePeb->LeDllFullPath, DllFullPath, ML_MIN(Length, sizeof(LePeb->LeDllFullPath)));

	// Directory part including the trailing backslash.
	Length = StrLengthW(DllFullPath);
	while (Length != 0 && DllFullPath[Length - 1] != '\\')
		--Length;
	Length = ML_MIN(Length, countof(LePeb->LeDllDirPath) - 1);
	CopyMemory(LePeb->LeDllDirPath, DllFullPath, Length * sizeof(WCHAR));
	LePeb->LeDllDirPath[Length] = 0;

	CloseLePeb(LePeb);

	// ---- Redirect the target's first LdrLoadDll call into the injected copy ----

	Jump[0] = JUMP;
	*(PULONG)&Jump[1] = PtrOffset(RemoteLoadFirstDll, PtrAdd(LdrLoadDllAddress, sizeof(Jump)));

	Status = WriteProtectMemory(Process, LdrLoadDllAddress, Jump, sizeof(Jump));
	if (NT_FAILED(Status))
	{
		Mm::FreeVirtualMemory(RemoteImage, Process);
		return Status;
	}

	return STATUS_SUCCESS;
}

EXTC
NTSTATUS
NTAPI
LeCreateProcessDirect(
	PLEB                    Leb,
	PCWSTR                  ApplicationName,
	PWSTR                   CommandLine,
	PCWSTR                  CurrentDirectory,
	ULONG                   CreationFlags,
	LPSTARTUPINFOW          StartupInfo,
	PML_PROCESS_INFORMATION ProcessInformation,
	LPSECURITY_ATTRIBUTES   ProcessAttributes,
	LPSECURITY_ATTRIBUTES   ThreadAttributes,
	PVOID                   Environment,
	HANDLE                  Token
	)
{
#if defined(_M_X64)
	// The x64 Core does not inject yet (see LeCreateProcess2).
	return STATUS_NOT_SUPPORTED;
#else
	NTSTATUS                Status;
	ULONG_PTR               Length;
	PWSTR                   DllFullPath;
	PLDR_MODULE             Module;
	ML_PROCESS_INFORMATION  ProcessInfo;

	static WCHAR Dll[] = LE_INJECTED_DLL_NAME;

	// Same lookup as LeCreateProcess: LocaleEmulator.dll next to the executable.
	Module = FindLdrModuleByHandle(nullptr);

	Length = Module->FullDllName.Length - Module->BaseDllName.Length;
	DllFullPath = (PWSTR)AllocStack(Length + sizeof(Dll));
	CopyMemory(DllFullPath, Module->FullDllName.Buffer, Length);
	CopyStruct(PtrAdd(DllFullPath, Length), Dll, sizeof(Dll));

	ZeroMemory(&ProcessInfo, sizeof(ProcessInfo));

	Status = Ps::CreateProcess(
		ApplicationName,
		CommandLine,
		CurrentDirectory,
		CreationFlags | CREATE_SUSPENDED,
		StartupInfo,
		&ProcessInfo,
		ProcessAttributes,
		ThreadAttributes,
		Environment,
		Token
		);

	FAIL_RETURN(Status);

	Status = LeInjectIntoSuspendedProcess(ProcessInfo.hProcess, ProcessInfo.dwProcessId, DllFullPath, Leb);

	// A 64-bit target runs without LE, as with the hook path.
	if (Status == STATUS_NOT_SUPPORTED)
		Status = STATUS_SUCCESS;

	if (NT_SUCCESS(Status) && FLAG_OFF(CreationFlags, CREATE_SUSPENDED))
		Status = NtResumeThread(ProcessInfo.hThread, nullptr);

	if (NT_FAILED(Status))
	{
		NtTerminateProcess(ProcessInfo.hProcess, Status);
		NtClose(ProcessInfo.hProcess);
		NtClose(ProcessInfo.hThread);
		return Status;
	}

	if (ProcessInformation != nullptr)
	{
		ProcessInformation->hProcess    = ProcessInfo.hProcess;
		ProcessInformation->hThread     = ProcessInfo.hThread;
		ProcessInformation->dwProcessId = ProcessInfo.dwProcessId;
		ProcessInformation->dwThreadId  = ProcessInfo.dwThreadId;
	}
	else
	{
		NtClose(ProcessInfo.hProcess);
		NtClose(ProcessInfo.hThread);
	}

	return Status;
#endif
}
