# LEInstaller 設計

> 日期：2026-11-15
> 狀態：設計中（等待確認）

## 背景

原版 LE 有獨立的 `LEInstaller.exe`，.NET 10 遷移時把它併進 LEGUI 的「Shell Extension」分頁（`docs/superpowers/specs/2026-03-28-dotnet10-migration-design.md`）。準備發佈 v3.0.0-beta.1 時重新檢討：舊版使用者習慣「解壓 → 執行 LEInstaller」，找不到它會以為版本不完整。新版還多了一道門檻：要裝 x86 + x64 兩個 .NET 10 Desktop Runtime，缺的時候 LEGUI 根本打不開。

## 決定

- 恢復獨立的 `LEInstaller.exe`，放在 zip 根目錄（與 `x86/`、`x64/` 並列）。
- **原生 C++**（不依賴 .NET），因此 runtime 還沒裝時也能執行，負責檢查並協助安裝 runtime。
- **註冊右鍵選單仍由 LEGUI 執行**：LEInstaller 在 runtime 齊全後開啟 LEGUI 的「安裝模式」視窗，不重寫 `ShellExtensionRegistrar`。
- LEInstaller 自己的字串支援 英 / 繁中 / 簡中 / 日，依 Windows 介面語言選擇，其他語言顯示英文。LEGUI 的安裝模式視窗沿用既有的 22 種語系。

## 使用流程

```
LEInstaller.exe
  ├─ 檢查 .NET 10 Desktop Runtime：x86（LEProc 需要）、x64（LEGUI 需要；32 位元 Windows 不檢查）
  │    └─ 有缺 → TaskDialog：列出缺少的項目
  │         [自動下載並安裝]  從 aka.ms 下載微軟的安裝程式 → 驗證數位簽章 → 以 /install /passive /norestart 執行（UAC）
  │         [開啟下載頁]      開瀏覽器到微軟的 .NET 10 下載頁
  │         [取消]
  │       安裝完重新檢查；仍缺則再次提示
  └─ 齊全 → 執行 x86\LEGUI.exe --installer，LEInstaller 結束
              └─ LEGUI 安裝模式：小視窗只放 ShellExtensionPanel（目前使用者 / 所有使用者的安裝與解除、清除舊版殘留）
```

## 元件

### 1. LEGUI `--installer` 模式

- `App_OnStartup` 新增 `--installer` 參數：開啟新視窗 `InstallerWindow.xaml`，內容只有 `ShellExtensionPanel`。
- 這個模式**不檢查安裝目錄的寫入權限、也不建立 `LEConfig.xml`**：它不碰 profile，提權（寫 HKLM）沿用 `ShellExtensionPanel` 既有的 `--shell-ext` 子程序做法。
- 視窗標題用既有字串組合（`AppName` + `InstallShellExtTitle`），**不新增語系鍵**。若實作時發現需要新字串，就照 LEGUI 鐵則處理 22 種語系。
- 測試：參數判斷放進可測試的函式，加進 `LEGUI.Tests`。

### 2. `src/LEInstaller/`（C++23，x86，靜態 CRT）

- **x86、`/MT`**：在 32 / 64 位元 Windows 都能執行，也不需要 VC++ 可轉散發套件。
- 加 manifest：comctl32 v6（TaskDialog）、`asInvoker`（只有安裝 runtime 時才由安裝程式自己提權）、per-monitor DPI。
- **runtime 檢查**（純函式，可測試）：
  1. 從 `HKLM\SOFTWARE\dotnet\Setup\InstalledVersions\<arch>` 的 32 位元 registry view 讀 `InstallLocation`；讀不到就用預設位置（x86：`%ProgramFiles(x86)%\dotnet`，x64：`%ProgramW6432%\dotnet`）。
  2. 檢查 `<InstallLocation>\shared\Microsoft.WindowsDesktop.App\` 底下是否有 `10.` 開頭的資料夾。**不信任 registry 的 `sharedfx` 版本清單**：實測它會殘留已移除的版本。
- **下載與安裝**：
  - 網址：`https://aka.ms/dotnet/10.0/windowsdesktop-runtime-win-{x86,x64}.exe`（微軟的固定連結，轉到最新的 10.0.x）。
  - 用 WinHTTP 或 `URLDownloadToFileW` 下載到 `%TEMP%`。
  - 執行前用 `WinVerifyTrust` 驗證 Authenticode 簽章，並確認簽署者是 Microsoft Corporation；驗證失敗就刪檔並改提示開啟下載頁。
  - 以 `ShellExecuteEx`（`runas`）執行 `/install /passive /norestart`，等它結束後重新檢查。
- **字串**：放在程式內的表格，依 `GetUserDefaultUILanguage()` 選 en / zh-TW（含 zh-HK）/ zh-CN / ja。
- **找 LEGUI**：`<LEInstaller.exe 所在目錄>\x86\LEGUI.exe`；找不到就提示「請把 zip 完整解壓縮」。

### 3. 建置、測試與發佈

- 加進 `LocaleEmulator.sln`（Release|Win32），輸出到 `Build/Release/`（根目錄，不是 `x86/`）。
- `tests/LEInstaller.Tests/`（C++ exe，無框架，與 `Core.Tests` 相同做法）：以暫存目錄模擬 `shared\Microsoft.WindowsDesktop.App\<版本>` 的檔案結構，測試版本判斷（`10.0.15` 符合、`9.0.22` 與 `10` 開頭但格式不對的資料夾不符合、空目錄、目錄不存在）、語言選擇。下載、簽章、UAC 的部分不做自動測試，改為手動驗證。
- CI：建置 LEInstaller 並執行 LEInstaller.Tests。
- Release：`LEInstaller.exe` 打包到 zip 根目錄；`release-notes.md`、README 的安裝步驟改成「執行 LEInstaller.exe」。

## 切片

1. LEGUI `--installer` 模式 + 測試。
2. LEInstaller：runtime 檢查（純函式）+ LEInstaller.Tests。
3. LEInstaller：UI 流程（TaskDialog、下載、驗章、安裝、開 LEGUI）。
4. sln / CI / Release 打包 + 文件（README、release-notes、AGENTS.md、roadmap），發 `v3.0.0-beta.2` 的草稿。

## 不做

- 不做真正的「安裝程式」（複製到 Program Files、開始選單、解除安裝項目）：原版也沒有，解壓即用維持不變。
- 不改用 self-contained 部署（取捨見 roadmap 的 runtime 項目）。
- 不支援 ARM64 Windows 上的特殊處理：x86 程式在 ARM64 上可以模擬執行，但沒有測試環境，先不保證。

## 風險

- **aka.ms 連結失效**：屬於微軟官方的固定連結，風險低；失效時仍有「開啟下載頁」可以用。
- **防毒誤判**：會下載並執行檔案的未簽章程式容易被誤判。驗證簽章能降低實際風險，但無法避免誤判；說明裡要寫。
- **32 位元 Windows**：LEGUI 的 apphost 是 x64，在 32 位元 Windows 上無法執行。LEInstaller 偵測到 32 位元系統時直接說明不支援（Windows 11 已無 32 位元版）。
