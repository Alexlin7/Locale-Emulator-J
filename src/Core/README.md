# Locale-Emulator-Core

This repository holds the source codes of the core libraries of the [Locale-Emulator project](https://github.com/xupefei/Locale-Emulator).

## Build

Prerequisites:

 - MSVC Build Tools 2026 (PlatformToolset `v145`) and the Windows 10/11 SDK. No WDK is required.

Building steps (Release|Win32 only):

```
msbuild src/Core/LoaderDll/LoaderDll.vcxproj /p:Configuration=Release /p:Platform=Win32
msbuild src/Core/LocaleEmulator/LocaleEmulator.vcxproj /p:Configuration=Release /p:Platform=Win32
```

## Notes

The original project built with a modified Visual Studio 2015 toolchain (`_Compilers`), WDK 7.1/8.0 headers and libraries (`_WDK`) and prebuilt libraries (`_Libs`). This fork builds with the standard toolchain and Windows SDK instead:

 - `Core.props` holds the settings shared by the three projects.
 - The binaries do not use the C runtime. They statically import only `ntdll.dll` (plus kernel32's `CreateProcessInternalW` in LoaderDll) and get C runtime functions and compiler helpers from ntdll's exports.
 - `ImportLibs/*.def` describe the ntdll/kernel32 exports that the SDK's import libraries lack. They are turned into import libraries on every build.
 - `Runtime/CoreRuntime.cpp` replaces the prebuilt `MyLib.lib`: the delay-load helper (built on `LdrLoadDll`), the SEH frame handlers (`_except_handler3`/`_except_handler4`) and a few compiler support symbols.
 - `LocaleEmulator.dll` is injected before kernel32.dll is loaded, so it must never import kernel32 statically. The original toolchain delay-loaded kernel32; current linkers refuse to (LNK4194), so the few kernel32 routines it needs are resolved at run time, and the build fails if a static kernel32 import appears.

## License

![enter image description here](http://www.gnu.org/graphics/lgplv3-147x51.png)

All source code files in `Loader`, `LoaderDll`, `LocaleEmulator`, `ImportLibs` and `Runtime` folders are licensed under [LGPL-3.0](https://opensource.org/licenses/LGPL-3.0).

If you want make any modification on these source codes while keeping new codes not protected by LGPL-3.0, please contact me for a sublicense instead.
