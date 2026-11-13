Locale Emulator
===============

[![license](https://img.shields.io/github/license/Alexlin7/Locale-Emulator-J.svg)](https://www.gnu.org/licenses/lgpl-3.0.en.html)
[![CI](https://github.com/Alexlin7/Locale-Emulator-J/actions/workflows/ci.yml/badge.svg)](https://github.com/Alexlin7/Locale-Emulator-J/actions/workflows/ci.yml)

Yet Another System Region and Language Simulator

## About

Locale Emulator lets you run Windows applications under a different locale without changing the OS system locale. It works by hooking Windows API calls in target processes via native DLL injection.

## Download & Security Notice

Only download Locale Emulator builds from the [Releases page of this repository](https://github.com/Alexlin7/Locale-Emulator-J/releases). No prebuilt release has been published yet; until then, build from source (see [Build](#build)).

The original author's website (`pooi.moe`) is **no longer an official source** — it now shows a parked "domain for sale" page, and such pages may redirect visitors to ads or scams. Do not download anything from it, and do not trust third-party mirrors that claim to host Locale Emulator.

## Project Origin

This project is a continuation of [xupefei/Locale-Emulator](https://github.com/xupefei/Locale-Emulator), which was archived by its original author on 2022-04-15. It was continued as [ooxxTaiwan/Locale-Emulator-J](https://github.com/ooxxTaiwan/Locale-Emulator-J) (detached from the original fork network for independent development), and this repository is a fork of that project.

The native hooking core (`src/Core/`) originates from [Locale-Emulator-Core](https://github.com/xupefei/Locale-Emulator-Core) (upstream commit `ae7160d`, archived 2022-04-15), integrated directly into this repository for continued maintenance.

### Major changes from the original

- Migrated from .NET Framework 4.0 to **.NET 10**
- Shell Extension rewritten from .NET COM to **native C++** (COM IShellExtInit + IContextMenu)
- Integrated Locale-Emulator-Core source code directly into repository
- Added CI/CD with GitHub Actions (build, test, code coverage)
- LEUpdater removed (will be redesigned)
- LEInstaller merged into LEGUI

### Changes in this fork

- Core builds with the standard MSVC v145 toolset and Windows SDK; the bundled VS2015 compilers, WDK and prebuilt libraries were removed. Core still uses no C runtime and statically imports only `ntdll.dll`.
- Fixed code page emulation on recent Windows builds, where ntdll's NLS table swap no longer takes effect: ntdll ANSI/OEM conversion functions are now hooked directly.
- .NET globalization uses NLS instead of ICU, so the region list again offers emulatable regions such as `zh-CN` / `zh-TW` / `zh-HK`; profiles saved with ICU names (e.g. `zh-Hans-CN`) are resolved automatically, and unsupported regions show an error instead of failing silently.

## System Requirements

- **Minimum OS**: Windows 10 version 1607 or later
- **Runtime**: .NET 10 (framework-dependent deployment)
- **Target applications**: x86 (32-bit) executables only (Core hooking engine limitation)

## Troubleshooting: diagnostic log

If an application does not behave as expected under Locale Emulator, you can turn on a diagnostic log. Set the environment variable `LE_DIAG_LOG` before launching:

```bat
set LE_DIAG_LOG=1
LEProc.exe -run "C:\path\to\app.exe"
```

- `1` (or any value that is not an absolute path) writes to `%TEMP%\LocaleEmulator\`; an absolute path such as `D:\LeLogs` writes there instead. `0`, `off` or unset turns it off (the default).
- Each process gets its own file, `<exe name>.<PID>.log` (UTF-16): one for LEProc and one for the application (and for each child process it starts). It records the emulated code page / locale / time zone, each initialization step with its status code, loaded DLLs, which hooks were installed, and child process injection.
- To use it with the right-click menu, set it as a user environment variable (`setx LE_DIAG_LOG 1`, then restart Explorer or sign out and in), and run `setx LE_DIAG_LOG 0` afterwards.
- Log files are not deleted automatically. They contain full file paths (which usually include your Windows user name); review them before sharing.

## Build

### Prerequisites

- [.NET 10 SDK](https://dotnet.microsoft.com/)
- Visual Studio 2026 (v18.0+) with:
  - .NET desktop development workload
  - Desktop development with C++ workload (MSVC v145)
- Or standalone: .NET 10 SDK + MSVC Build Tools 2026

### Build from IDE

1. Open `LocaleEmulator.sln` in Visual Studio 2026
2. Build Solution (Ctrl+Shift+B)

### Build from CLI

```bash
# Build all projects (x86 + x64)
msbuild LocaleEmulator.sln /p:Configuration=Release /p:Platform=Win32
msbuild LocaleEmulator.sln /p:Configuration=Release /p:Platform=x64

# Run .NET tests
dotnet test tests/LECommonLibrary.Tests/
dotnet test tests/LEGUI.Tests/
dotnet test tests/LEProc.Tests/ --arch x86    # needs Windows Desktop Runtime x86
```

### Build Output

- `Build/Release/x86/` -- LEProc, LEGUI, Core DLLs, ShellExtension (x86)
- `Build/Release/x64/` -- ShellExtension (x64 only)

## Translate

If you want to help translating Locale Emulator, you can find all strings in:

- `DefaultLanguage.xaml` in `src/LEGUI/Lang/` folder (WPF UI)
- `DefaultLanguage.xml` in `src/ShellExtension/Lang/` folder (context menu)

After you translated the above files into your language, please submit a pull request.

## Contributing

Contributions are welcome! Feel free to open issues or submit pull requests. Please ensure CI passes before requesting review.

## License

![LGPL](https://www.gnu.org/graphics/lgplv3-147x51.png)

- Main project: [LGPL-3.0](https://opensource.org/licenses/LGPL-3.0)
- Core (`src/Core/`): LGPL-3.0 (code) / GPL-3.0 (project)
- Shell Extension uses [pugixml](https://pugixml.org/) (MIT license)

Flat icon set from graphicex (the original source page is no longer available; the link was removed because the domain now redirects to unrelated sites).
