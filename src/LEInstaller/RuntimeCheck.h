// src/LEInstaller/RuntimeCheck.h
// Detects whether the .NET Windows Desktop Runtime needed by LE is installed.
//
// LEProc.exe has an x86 apphost and LEGUI.exe an x64 apphost (on 64-bit Windows),
// so LE needs the Desktop Runtime for both architectures. Detection mirrors how
// the apphost finds .NET: the install location registered under
// HKLM\SOFTWARE\dotnet\Setup\InstalledVersions\<arch> (32-bit registry view),
// falling back to the default folder, then the version folders under
// shared\Microsoft.WindowsDesktop.App. The registry's own version list
// (sharedfx\...) is not used: it keeps entries of runtimes that were removed.
#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace LeInstaller
{

enum class Arch
{
    X86,
    X64
};

// The major version LE targets (net10.0-windows).
inline constexpr int kRequiredMajor = 10;

// True for a released runtime folder name of the given major version, e.g. "10.0.15".
// Previews ("10.0.0-rc.1"), other majors and malformed names do not match.
bool IsReleasedVersionOf(std::wstring_view folderName, int major);

// True when <dotnetRoot>\shared\Microsoft.WindowsDesktop.App has a released folder
// of the given major version.
bool HasDesktopRuntime(const std::filesystem::path& dotnetRoot, int major);

// The install location registered by the .NET installer, if any.
std::optional<std::filesystem::path> ReadRegisteredInstallLocation(Arch arch);

// Where the .NET installer puts the runtime by default.
// x86: %ProgramFiles% as seen by this (x86) process, i.e. "Program Files (x86)" on
// 64-bit Windows and "Program Files" on 32-bit Windows. x64: %ProgramW6432%.
std::filesystem::path DefaultDotnetRoot(Arch arch);

// Registered location, else the default one.
std::filesystem::path ResolveDotnetRoot(Arch arch);

struct RuntimeStatus
{
    bool is64BitWindows = false;
    bool hasX86 = false;
    bool hasX64 = false;  // only meaningful when is64BitWindows

    bool NeedsX64() const { return is64BitWindows && !hasX64; }
    bool IsComplete() const { return hasX86 && !NeedsX64(); }
};

RuntimeStatus CheckRuntimes(int major = kRequiredMajor);

bool Is64BitWindows();

}  // namespace LeInstaller
