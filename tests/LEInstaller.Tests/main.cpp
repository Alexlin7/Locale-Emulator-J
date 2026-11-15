// tests/LEInstaller.Tests/main.cpp
// Unit tests for LEInstaller's .NET runtime detection (src/LEInstaller/RuntimeCheck.cpp,
// compiled into this exe). No framework dependency -- exit code 0 = pass, 1 = fail.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#include <filesystem>
#include <string>

#include "RuntimeCheck.h"

namespace fs = std::filesystem;
using namespace LeInstaller;

namespace
{

int g_failures = 0;

void Check(bool condition, const wchar_t* name)
{
    wprintf(L"%s: %s\n", condition ? L"PASS" : L"FAIL", name);
    if (!condition)
        ++g_failures;
}

// A fresh temporary "dotnet root" that is deleted when the test ends.
struct TempDotnetRoot
{
    fs::path root;

    TempDotnetRoot()
    {
        wchar_t tmp[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tmp);
        root = fs::path(tmp) / (L"LEInstallerTests_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
                                std::to_wstring(GetTickCount64()));
        fs::create_directories(root);
    }

    ~TempDotnetRoot()
    {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    void AddDesktopRuntime(const wchar_t* version) const
    {
        fs::create_directories(root / L"shared" / L"Microsoft.WindowsDesktop.App" / version);
    }

    void AddFile(const wchar_t* relative) const
    {
        const auto p = root / relative;
        fs::create_directories(p.parent_path());
        FILE* f = nullptr;
        if (_wfopen_s(&f, p.c_str(), L"w") == 0 && f)
            fclose(f);
    }
};

void TestIsReleasedVersionOf()
{
    Check(IsReleasedVersionOf(L"10.0.15", 10), L"IsReleasedVersionOf: 10.0.15 matches 10");
    Check(IsReleasedVersionOf(L"10.1.0", 10), L"IsReleasedVersionOf: 10.1.0 matches 10");
    Check(IsReleasedVersionOf(L"10.0.100", 10), L"IsReleasedVersionOf: three-digit patch matches");

    Check(!IsReleasedVersionOf(L"9.0.22", 10), L"IsReleasedVersionOf: other major does not match");
    Check(!IsReleasedVersionOf(L"100.0.0", 10), L"IsReleasedVersionOf: 100 is not 10");
    Check(!IsReleasedVersionOf(L"1.0.0", 10), L"IsReleasedVersionOf: 1 is not 10");
    Check(!IsReleasedVersionOf(L"10.0.0-rc.1.25451.107", 10), L"IsReleasedVersionOf: preview does not match");
    Check(!IsReleasedVersionOf(L"10", 10), L"IsReleasedVersionOf: major only does not match");
    Check(!IsReleasedVersionOf(L"10.0", 10), L"IsReleasedVersionOf: two parts do not match");
    Check(!IsReleasedVersionOf(L"10.0.15.1", 10), L"IsReleasedVersionOf: four parts do not match");
    Check(!IsReleasedVersionOf(L"10..15", 10), L"IsReleasedVersionOf: empty part does not match");
    Check(!IsReleasedVersionOf(L"", 10), L"IsReleasedVersionOf: empty name does not match");
    Check(!IsReleasedVersionOf(L"v10.0.15", 10), L"IsReleasedVersionOf: prefix does not match");
}

void TestHasDesktopRuntime()
{
    {
        TempDotnetRoot t;
        t.AddDesktopRuntime(L"10.0.15");
        Check(HasDesktopRuntime(t.root, 10), L"HasDesktopRuntime: 10.0.15 installed");
    }
    {
        TempDotnetRoot t;
        t.AddDesktopRuntime(L"8.0.33");
        t.AddDesktopRuntime(L"9.0.22");
        Check(!HasDesktopRuntime(t.root, 10), L"HasDesktopRuntime: only older majors");
    }
    {
        TempDotnetRoot t;
        t.AddDesktopRuntime(L"10.0.0-rc.2.25502.107");
        Check(!HasDesktopRuntime(t.root, 10), L"HasDesktopRuntime: only a preview");
    }
    {
        TempDotnetRoot t;
        t.AddDesktopRuntime(L"9.0.22");
        t.AddDesktopRuntime(L"10.0.11");
        Check(HasDesktopRuntime(t.root, 10), L"HasDesktopRuntime: 10.x among other versions");
    }
    {
        TempDotnetRoot t;
        // A file, not a folder, with a matching name.
        t.AddFile(L"shared\\Microsoft.WindowsDesktop.App\\10.0.15");
        Check(!HasDesktopRuntime(t.root, 10), L"HasDesktopRuntime: file with version name ignored");
    }
    {
        TempDotnetRoot t;
        // Only the base runtime, not the Desktop runtime.
        fs::create_directories(t.root / L"shared" / L"Microsoft.NETCore.App" / L"10.0.15");
        Check(!HasDesktopRuntime(t.root, 10), L"HasDesktopRuntime: NETCore.App alone is not enough");
    }
    {
        TempDotnetRoot t;
        Check(!HasDesktopRuntime(t.root, 10), L"HasDesktopRuntime: empty dotnet root");
    }
    Check(!HasDesktopRuntime(L"Z:\\does\\not\\exist\\dotnet", 10), L"HasDesktopRuntime: missing dotnet root");
}

void TestRuntimeStatus()
{
    RuntimeStatus s;
    s.is64BitWindows = true;
    s.hasX86 = true;
    s.hasX64 = false;
    Check(s.NeedsX64() && !s.IsComplete(), L"RuntimeStatus: 64-bit Windows without x64 is incomplete");

    s.hasX64 = true;
    Check(!s.NeedsX64() && s.IsComplete(), L"RuntimeStatus: 64-bit Windows with both is complete");

    s.hasX86 = false;
    Check(!s.IsComplete(), L"RuntimeStatus: missing x86 is incomplete");

    RuntimeStatus s32;
    s32.is64BitWindows = false;
    s32.hasX86 = true;
    Check(!s32.NeedsX64() && s32.IsComplete(), L"RuntimeStatus: 32-bit Windows needs only x86");
}

void TestDefaultDotnetRoot()
{
    Check(DefaultDotnetRoot(Arch::X86).filename() == L"dotnet", L"DefaultDotnetRoot: x86 ends with dotnet");
    Check(DefaultDotnetRoot(Arch::X64).filename() == L"dotnet", L"DefaultDotnetRoot: x64 ends with dotnet");
    if (Is64BitWindows())
    {
        Check(DefaultDotnetRoot(Arch::X86) != DefaultDotnetRoot(Arch::X64),
              L"DefaultDotnetRoot: x86 and x64 differ on 64-bit Windows");
    }
}

// Informational: what this machine has. Not asserted, it depends on the machine.
void PrintMachineInfo()
{
    const auto status = CheckRuntimes();
    wprintf(L"INFO: 64-bit Windows=%d, x86 root=%s, x64 root=%s\n", status.is64BitWindows,
            ResolveDotnetRoot(Arch::X86).c_str(), ResolveDotnetRoot(Arch::X64).c_str());
    wprintf(L"INFO: Desktop Runtime %d: x86=%d, x64=%d\n", kRequiredMajor, status.hasX86, status.hasX64);
}

}  // namespace

int wmain()
{
    TestIsReleasedVersionOf();
    TestHasDesktopRuntime();
    TestRuntimeStatus();
    TestDefaultDotnetRoot();
    PrintMachineInfo();

    if (g_failures == 0)
    {
        wprintf(L"ALL TESTS PASSED\n");
        return 0;
    }
    wprintf(L"%d TEST(S) FAILED\n", g_failures);
    return 1;
}
