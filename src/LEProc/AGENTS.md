# src/LEProc/AGENTS.md

LEProc 是 LE 的啟動器（.NET 10，**x86**）：讀 profile、用 `CultureInfo` 算出 code page / LCID / charset / 時區，組成 LEB 結構與 registry 轉向項目，再 P/Invoke 呼叫 `LoaderDll.dll` 的 `LeCreateProcess` 建立目標程序。

跨模組鐵則見根目錄 [`AGENTS.md`](../../AGENTS.md)。

## 鐵則

1. **必須維持 x86**（`LEProc.csproj` 的 `PlatformTarget`）：它 P/Invoke 的是 x86 的 `LoaderDll.dll`。64 位元目標程式的支援規劃為「同一個 csproj 再建一份 x64 的 `LEProc64.exe`」，由 LEProc 偵測目標架構後轉交（spec Q2、Phase 5），不要把 LEProc 改成 AnyCPU。
2. **LEB 結構是跨語言契約**：`LoaderWrapper.cs` 的 `LEB`、`RTL_TIME_ZONE_INFORMATION`、`REGISTRY_REDIRECTION_ENTRY64` 必須與 `src/Core/LocaleEmulator/LocaleEmulator.h` 一致，並由 `tests/LEProc.Tests/LEBStructLayoutTests.cs` 與 `tests/Core.Tests/main.cpp` 保護。改欄位要四處一起改。
3. **地區是否可模擬一律經 `LECommonLibrary.RegionSupport` 判斷**，不要在 LEProc 另寫判斷。

## 結構

| 檔案 | 內容 |
|---|---|
| `Program.cs` | CLI 進入點：`-run %APP%`、`-runas %GUID% %APP%`、`-manage %APP%`、`-global`；組出 LEB 並呼叫 `LoaderWrapper` |
| `LoaderWrapper.cs` | LEB 等結構的 C# 定義、`LeCreateProcess` 的 P/Invoke、把 LEB 與 registry 資料整理進 unmanaged 記憶體 |
| `RegistryEntriesLoader.cs` | registry 轉向項目：基本（`HKLM\...\Nls\CodePage` 的 ACP / OEMCP / Default / InstallLanguage）與進階（`IsAdvancedRedirection` 的 HKCU 地區與 UI 語言） |
| `LERegistryRedirector.cs` | 把 registry 項目轉成 `REGISTRY_REDIRECTION_ENTRY64` 的二進位格式 |

## 常見陷阱

- `GetCharsetFromANSICodepage` 的結果會變成 LEB 的 `DefaultCharset`，影響 gdi32 hook 的字型 charset。
- `RunWithSuspend` 對應 LEB 的 `DebugMode`；`RunAsAdmin` 會在需要時重新以系統管理員身分啟動自己。
- .NET 端的全球化必須是 NLS 模式（根目錄鐵則 3），否則 `CultureInfo` 的 LCID 會變成 0x1000。

## 測試

```powershell
dotnet test tests/LEProc.Tests/ --arch x86     # 需要 x86 test host
```

- `CharacterizationTests`：`CultureInfo` 查詢結果與 .NET Framework 4.0 基準值比對
- `GetCharsetFromANSICodepageTests`、`RegistryEntriesLoaderTests`、`LEBStructLayoutTests`

端對端行為由 smoke test 驗證（[`tests/SmokeTest/AGENTS.md`](../../tests/SmokeTest/AGENTS.md)）。
