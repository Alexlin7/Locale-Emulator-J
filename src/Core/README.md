# Locale-Emulator-Core

This repository holds the source codes of the core libraries of the [Locale-Emulator project](https://github.com/xupefei/Locale-Emulator).

## Build

Prerequisites:

 - MSVC Build Tools 2026 (PlatformToolset `v145`) and the Windows 10/11 SDK. No WDK is required.

Building steps (Release only):

```
msbuild src/Core/LoaderDll/LoaderDll.vcxproj /p:Configuration=Release /p:Platform=Win32
msbuild src/Core/LocaleEmulator/LocaleEmulator.vcxproj /p:Configuration=Release /p:Platform=Win32
```

`Release|x64` builds `LoaderDll64.dll` and `LocaleEmulator64.dll` (see "x64 build" below). It is under development and not shipped yet.

## Notes

The original project built with a modified Visual Studio 2015 toolchain (`_Compilers`), WDK 7.1/8.0 headers and libraries (`_WDK`) and prebuilt libraries (`_Libs`). This fork builds with the standard toolchain and Windows SDK instead:

 - `Core.props` holds the settings shared by the three projects.
 - The binaries do not use the C runtime. They statically import only `ntdll.dll` (plus kernel32's `CreateProcessInternalW` in LoaderDll) and get C runtime functions and compiler helpers from ntdll's exports.
 - `ImportLibs/<machine>/*.def` (`x86`, `x64`) describe the ntdll/kernel32 exports that the SDK's import libraries lack. They are turned into import libraries on every build; decorated names and compiler helpers differ per architecture, hence one directory per machine.
 - `Runtime/CoreRuntime.cpp` replaces the prebuilt `MyLib.lib`: the delay-load helper (built on `LdrLoadDll`), the x86 SEH frame handlers (`_except_handler3`/`_except_handler4`) and a few compiler support symbols.
 - `LocaleEmulator.dll` is injected before kernel32.dll is loaded, so it must never import kernel32 statically. The original toolchain delay-loaded kernel32; current linkers refuse to (LNK4194), so the few kernel32 routines it needs are resolved at run time, and the build fails if a static kernel32 import appears.

## x64 build

The x64 Core is being built up in phases (`docs/superpowers/specs/2026-11-11-core-modularization-and-x64-design.md`). Current state:

 - It compiles, but installs no hooks: `HookPort.cpp` relies on `KiFastSystemCall` and x86 inline assembly, so the x64 build links `LocaleEmulator/HookPortX64Stub.cpp` instead, and `LeGlobalData::Initialize` returns `STATUS_NOT_SUPPORTED`.
 - SEH uses table-based unwinding (`.pdata`/`.xdata`) with ntdll's `__C_specific_handler`; `CoreRuntime.cpp` implements nothing architecture specific for x64.
 - `LocaleEmulator/Arch/LeArch.h` holds the TEB/PEB offsets for both architectures; `Arch/LayoutAsserts.cpp` checks them against `ml.h` at compile time.
 - `LoaderDll64.dll` injects `LocaleEmulator64.dll` from its own folder.

## License

![enter image description here](http://www.gnu.org/graphics/lgplv3-147x51.png)

All source code files in `Loader`, `LoaderDll`, `LocaleEmulator`, `ImportLibs` and `Runtime` folders are licensed under [LGPL-3.0](https://opensource.org/licenses/LGPL-3.0).

If you want make any modification on these source codes while keeping new codes not protected by LGPL-3.0, please contact me for a sublicense instead.
