## Install

1. Install the **.NET 10 Windows Desktop Runtime**, both x86 and x64 (LEProc runs as x86, LEGUI as x64 on 64-bit Windows):

   ```powershell
   winget install Microsoft.DotNet.DesktopRuntime.10 --architecture x86
   winget install Microsoft.DotNet.DesktopRuntime.10 --architecture x64
   ```

   If one is missing, Windows shows a dialog with a link to the download page when the program starts.

2. Extract the zip to a folder you can write to, for example `D:\Tools\LocaleEmulator-J\`. Keep the `x86` and `x64` folders side by side.
3. Run `x86\LEGUI.exe`, open the **Shell Extension** tab and choose **Install (Current User)** or **Install (All Users)**. Restart Explorer or sign out and in.

## Upgrading

- From an earlier build of this fork: extract over the old folder. The zip does not contain `LEConfig.xml`, so your global profiles are kept.
- From the original Locale Emulator 2.x: uninstall its context menu with the original `LEInstaller.exe` first, or use **Clean up old registration** in LEGUI's Shell Extension tab. Per-application `.le.config` files keep working; global profiles have to be created again (or copy the old `LEConfig.xml` into the `x86` folder).

## Limitations

- Only 32-bit (x86) applications can be emulated. Launching a 64-bit application shows a message instead.
- The binaries are not code-signed, so SmartScreen or antivirus software may warn about them (LE injects a DLL into the target process). Only download from this repository's Releases page.

`*-symbols.zip` contains debugging symbols (`.pdb`) and is only needed for analysing crash dumps. `SHA256SUMS.txt` lists the checksums of both zips.
