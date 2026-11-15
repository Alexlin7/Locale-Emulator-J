// src/LEInstaller/RuntimeCheck.cpp
#include "RuntimeCheck.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cwctype>
#include <string>
#include <system_error>

namespace LeInstaller
{

namespace
{

// Parses a run of decimal digits starting at pos; returns false when there is none.
bool ReadNumber(std::wstring_view s, size_t& pos, int& value)
{
    const size_t start = pos;
    value = 0;
    while (pos < s.size() && std::iswdigit(s[pos]))
    {
        if (pos - start >= 9)  // keep the value in range
            return false;
        value = value * 10 + (s[pos] - L'0');
        ++pos;
    }
    return pos > start;
}

std::wstring GetEnv(const wchar_t* name)
{
    wchar_t buffer[MAX_PATH] = {};
    const DWORD len = GetEnvironmentVariableW(name, buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return {};
    return buffer;
}

}  // namespace

bool IsReleasedVersionOf(std::wstring_view folderName, int major)
{
    // Exactly <major>.<minor>.<patch>, digits only.
    size_t pos = 0;
    int parts[3] = {};
    for (int i = 0; i < 3; ++i)
    {
        if (!ReadNumber(folderName, pos, parts[i]))
            return false;
        if (i < 2)
        {
            if (pos >= folderName.size() || folderName[pos] != L'.')
                return false;
            ++pos;
        }
    }
    return pos == folderName.size() && parts[0] == major;
}

bool HasDesktopRuntime(const std::filesystem::path& dotnetRoot, int major)
{
    std::error_code ec;
    const auto dir = dotnetRoot / L"shared" / L"Microsoft.WindowsDesktop.App";
    std::filesystem::directory_iterator it(dir, ec);
    if (ec)
        return false;

    for (const auto& entry : it)
    {
        if (entry.is_directory(ec) && IsReleasedVersionOf(entry.path().filename().native(), major))
            return true;
    }
    return false;
}

std::optional<std::filesystem::path> ReadRegisteredInstallLocation(Arch arch)
{
    const wchar_t* subKey = arch == Arch::X86
        ? L"SOFTWARE\\dotnet\\Setup\\InstalledVersions\\x86"
        : L"SOFTWARE\\dotnet\\Setup\\InstalledVersions\\x64";

    wchar_t value[MAX_PATH] = {};
    DWORD size = sizeof(value);
    // The .NET installers write these keys to the 32-bit view for both architectures.
    const LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE, subKey, L"InstallLocation",
                                        RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY, nullptr, value, &size);
    if (status != ERROR_SUCCESS || value[0] == L'\0')
        return std::nullopt;
    return std::filesystem::path(value);
}

std::filesystem::path DefaultDotnetRoot(Arch arch)
{
    const std::wstring programFiles = GetEnv(arch == Arch::X86 ? L"ProgramFiles" : L"ProgramW6432");
    return std::filesystem::path(programFiles.empty() ? L"C:\\Program Files" : programFiles) / L"dotnet";
}

std::filesystem::path ResolveDotnetRoot(Arch arch)
{
    if (auto registered = ReadRegisteredInstallLocation(arch))
        return *registered;
    return DefaultDotnetRoot(arch);
}

bool Is64BitWindows()
{
#if defined(_WIN64)
    return true;
#else
    BOOL wow64 = FALSE;
    return IsWow64Process(GetCurrentProcess(), &wow64) && wow64;
#endif
}

RuntimeStatus CheckRuntimes(int major)
{
    RuntimeStatus status;
    status.is64BitWindows = Is64BitWindows();
    status.hasX86 = HasDesktopRuntime(ResolveDotnetRoot(Arch::X86), major);
    if (status.is64BitWindows)
        status.hasX64 = HasDesktopRuntime(ResolveDotnetRoot(Arch::X64), major);
    return status;
}

}  // namespace LeInstaller
