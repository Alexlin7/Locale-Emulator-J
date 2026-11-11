// Minimal stand-ins for the Windows SDK / ml declarations used by the test.
typedef unsigned int UINT;
typedef void *HANDLE, *PVOID;

extern "C" int  MultiByteToWideChar(UINT, unsigned long, const char *, int, wchar_t *, int);
extern "C" int  WideCharToMultiByte(UINT, unsigned long, const wchar_t *, int, char *, int, const char *, int *);
extern "C" UINT GetACP();
extern "C" PVOID GlobalLock(HANDLE);
extern "C" long RtlMultiByteToUnicodeN(wchar_t *, unsigned long, unsigned long *, const char *, unsigned long);

PVOID GetKernel32Handle();
PVOID LookupExportTable(PVOID Module, unsigned long Hash);
PVOID GetRoutineAddress(PVOID Module, const char *Name);

#define KERNEL32_MultiByteToWideChar 0x11111111u
#define KERNEL32_GetACP              0x22222222u
#define KERNEL32_GlobalLock          0x33333333u
#define KERNEL32_API(Name) ((decltype(&::Name))LookupExportTable(GetKernel32Handle(), KERNEL32_##Name))
