// tests/ShellExtension.Tests/main.cpp
// Standalone COM export and registration test for ShellExtension.dll,
// plus ConfigReader unit tests (ConfigReaderTests.cpp).
// No framework dependency -- exit code 0 = pass, 1 = fail.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

// ========================================================================
// RAII helpers
// ========================================================================

struct ModuleGuard
{
    HMODULE h;
    explicit ModuleGuard(HMODULE m) : h(m) {}
    ~ModuleGuard() { if (h) FreeLibrary(h); }
    ModuleGuard(const ModuleGuard&) = delete;
    ModuleGuard& operator=(const ModuleGuard&) = delete;
};

// ========================================================================
// Helpers
// ========================================================================

static bool FileExists(const wchar_t* path)
{
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES &&
           !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

int RunConfigReaderTests();

// ========================================================================
// Main
// ========================================================================

int wmain()
{
    // ConfigReader tests do not need ShellExtension.dll; run them first so
    // their result is reported even if the DLL checks below bail out early.
    const int configFailures = RunConfigReaderTests();

    int failures = 0;

    // ------------------------------------------------------------------
    // 1. Locate ShellExtension.dll
    // ------------------------------------------------------------------
    wchar_t exeDir[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
    // Trim to directory
    wchar_t* lastSlash = wcsrchr(exeDir, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';

    wchar_t dllPath[MAX_PATH] = {};

    // The x64 build is ShellExtension64.dll, in the same directory as this test exe
    // (both vcxproj use $(SolutionDir)Build\$(Configuration)\x64\ as OutDir). In the
    // product folder it sits next to the x86 ShellExtension.dll
    // (docs/superpowers/specs/2026-11-16-flat-product-layout.md).
    _snwprintf_s(dllPath, _TRUNCATE, L"%sShellExtension64.dll", exeDir);
    if (!FileExists(dllPath))
    {
        wprintf(L"FAIL: Cannot find ShellExtension64.dll\n");
        wprintf(L"  Expected: %s\n", dllPath);
        return 1;
    }

    // The DLL reads its menu text from Lang\ next to itself (I18n::GetLangDirectory).
    // Without the language files the menu shows string keys such as "Submenu".
    wchar_t langPath[MAX_PATH] = {};
    _snwprintf_s(langPath, _TRUNCATE, L"%sLang\\DefaultLanguage.xml", exeDir);
    if (!FileExists(langPath))
    {
        wprintf(L"FAIL: Language file missing next to the DLL: %s\n", langPath);
        ++failures;
    }
    else
    {
        wprintf(L"PASS: Lang\\DefaultLanguage.xml is next to the DLL\n");
    }

    wprintf(L"INFO: Loading %s\n", dllPath);

    HMODULE hDll = LoadLibraryW(dllPath);
    if (!hDll)
    {
        wprintf(L"FAIL: LoadLibraryW failed (error %lu)\n", GetLastError());
        return 1;
    }
    ModuleGuard dllGuard(hDll);

    wprintf(L"PASS: LoadLibraryW succeeded\n");

    // ------------------------------------------------------------------
    // 2. Verify 4 COM exports exist
    // ------------------------------------------------------------------
    const char* exports[] = {
        "DllRegisterServer",
        "DllUnregisterServer",
        "DllGetClassObject",
        "DllCanUnloadNow",
    };

    for (int i = 0; i < 4; ++i)
    {
        FARPROC proc = GetProcAddress(hDll, exports[i]);
        if (!proc)
        {
            wprintf(L"FAIL: Export '%hs' not found\n", exports[i]);
            ++failures;
        }
        else
        {
            wprintf(L"PASS: Export '%hs' found\n", exports[i]);
        }
    }

    if (failures > 0)
    {
        wprintf(L"\nRESULT: %d DLL check(s) FAILED\n", failures);
        return 1;
    }

    if (configFailures > 0)
    {
        wprintf(L"\nRESULT: %d ConfigReader check(s) FAILED\n", configFailures);
        return 1;
    }

    // Registration test (DllRegisterServer / DllUnregisterServer round-trip)
    // is intentionally NOT automated. It modifies HKCR and could interfere
    // with an existing Shell Extension installation. Use manual testing:
    //   regsvr32 ShellExtension64.dll
    //   regsvr32 /u ShellExtension64.dll

    wprintf(L"\nRESULT: all DLL and ConfigReader checks passed\n");
    return 0;
}
