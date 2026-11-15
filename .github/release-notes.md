## Install

1. **Before extracting**, right-click the downloaded zip → **Properties** → check **Unblock** → **OK**. Otherwise every program extracted from it is marked as coming from the Internet, and Microsoft Defender runs a cloud security scan the first time each one starts (the binaries are not code-signed).
2. Extract all files to one folder you can write to, for example `D:\Tools\LocaleEmulator-J\`.
3. Run **`LEInstaller.exe`**.
   - It checks for the **.NET 10 Desktop Runtime** (x86 and x64; LEProc runs as x86, LEGUI as x64). If one is missing, it can download Microsoft's official installer and install it for you (administrator permission required), or open the download page.
   - Then it opens the **Shell Extension** window: choose **Install (Current User)** or **Install (All Users)**, then restart Explorer or sign out and in.

To install the runtime yourself instead:

```powershell
winget install Microsoft.DotNet.DesktopRuntime.10 --architecture x86
winget install Microsoft.DotNet.DesktopRuntime.10 --architecture x64
```

## Upgrading

- **From an earlier build of this fork**: extract over the old folder. The zip does not contain `LEConfig.xml`, so your global profiles are kept.
  - **From v3.0.0-beta.2 or earlier** (with `x86` / `x64` subfolders): those builds' context menu did not work. Click **Uninstall** in the old Shell Extension window, then install this version into a new folder and copy your old `x86\LEConfig.xml` into it (or use **Import...**).
- **From the original Locale Emulator 2.x**: run `LEInstaller.exe`. When the old context menu is still registered, the Shell Extension window shows **Old version residue detected** with **Also import the profiles of the old version** (checked): **Clean up old registration** imports your old global profiles, then removes the old context menu. Profiles with the same name or GUID as an existing one are skipped.
  - If the old context menu was already removed, use **Import...** in the same window (or next to the profile list in `LEGUI.exe`) and pick the old `LEConfig.xml`.
  - Per-application `.le.config` files keep working as they are.

## Limitations

- Only 32-bit (x86) applications can be emulated. Launching a 64-bit application shows a message instead.
- Requires 64-bit Windows 10 1607 or later.
- The binaries are not code-signed, so SmartScreen or antivirus software may warn about them (LE injects a DLL into the target process, and LEInstaller can download and run the .NET installer). Only download from this repository's Releases page.

`*-symbols.zip` contains debugging symbols (`.pdb`) and is only needed for analysing crash dumps. `SHA256SUMS.txt` lists the checksums of both zips.
