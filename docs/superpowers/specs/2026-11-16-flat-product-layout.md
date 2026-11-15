# 產品目錄改為單一資料夾

> 日期：2026-11-16
> 狀態：已實作（2026-11-16）。另外：ShellExtension 改用靜態 CRT（原本依賴 VC++ 可轉散發套件），`.def` 不再寫死 LIBRARY 名稱

## 問題

v3.0.0-beta.2 的 zip 是 `LEInstaller.exe` + `x86\`（全部程式）+ `x64\ShellExtension.dll`。安裝後右鍵選單不能用：

1. 64 位元的 Explorer 載入 `x64\ShellExtension.dll`，它在**自己的資料夾**找 `Lang\`、`LEConfig.xml`、`LEProc.exe`（`ConfigReader::GetModuleDirectory`），但這些都在 `x86\`：選單只剩預設項、點了也啟動不了 LEProc。
2. `ShellExtension.vcxproj` 把複製 `Lang\*.xml` 的 `PostBuildEvent` 寫在 `PropertyGroup`，MSBuild 不會執行：CI 輸出與發佈的 zip 裡從來沒有右鍵選單的語系檔，選單顯示鍵名（`Submenu`、`RunDefault`）。

根因是把 MSBuild 依平台分開的建置輸出（`Build\Release\x86`、`x64`）直接當成產品結構，而各元件對「自己在哪」的假設不一致：ShellExtension 假設全部在自己旁邊、LEGUI 假設 `x86` / `x64` 並列（`GetBuildOutputRoot` 取上兩層）、LEInstaller 假設自己在上一層。實際上只有右鍵選單 DLL 需要分 32 / 64 位元（跟著 Explorer），其他元件都是 x86 或 AnyCPU。

## 決定

產品是**單一資料夾**，與原版 LE 2.x 相同：

```
LocaleEmulator-J\
  LEInstaller.exe
  LEProc.exe、LEProc.dll、*.deps.json、*.runtimeconfig.json
  LEGUI.exe、LEGUI.dll、LECommonLibrary.dll
  LoaderDll.dll、LocaleEmulator.dll
  ShellExtension.dll      ← x86（32 位元 Windows 的 Explorer）
  ShellExtension64.dll    ← x64（64 位元 Windows 的 Explorer）
  Lang\                   ← LEGUI 的 *.xaml + 右鍵選單的 *.xml（副檔名不同，不衝突）
  LEConfig.xml            ← 第一次使用時由 LEGUI 建立，不在 zip 裡
```

- **x64 檔案加 `64` 後綴、與 x86 同資料夾**：與 x64 Core 規劃的 `LoaderDll64.dll` / `LocaleEmulator64.dll` / `LEProc64.exe` 相同的命名規則（`docs/superpowers/specs/2026-11-11-core-modularization-and-x64-design.md`），日後重啟 x64 不用再改結構。
- **每個元件都在自己的資料夾找其他檔案**：ShellExtension 不用改；LEGUI 在 `LEGUI.exe` 的資料夾找 `ShellExtension.dll` / `ShellExtension64.dll`（依 OS 位元數）；LEInstaller 找同資料夾的 `LEGUI.exe`。
- **建置輸出**：`Build\Release\x86\` 繼續當「組好的產品資料夾」（smoke test 等腳本的預設路徑不變）。x64 的 `ShellExtension64.dll` 仍輸出到 `Build\Release\x64\`，由 CI 的組裝步驟複製進產品資料夾；`LEInstaller.exe` 改輸出到 `Build\Release\x86\`。

## 改動

| 元件 | 改動 |
|---|---|
| ShellExtension | x64 的 `TargetName` 改為 `ShellExtension64`；`PostBuildEvent` 移到 `ItemDefinitionGroup`，讓 `Lang\*.xml` 真的複製到輸出資料夾 |
| ShellExtension.Tests | 載入 `ShellExtension64.dll`；新增檢查：DLL 旁邊有 `Lang\DefaultLanguage.xml` |
| LEGUI | `ShellExtensionRegistrar`：移除 `GetBuildOutputRoot`，改為在 `AppContext.BaseDirectory` 依 OS 位元數選 `ShellExtension.dll` / `ShellExtension64.dll`；更新測試。同一個 CLSID 重新安裝會覆寫 `InprocServer32` 的路徑，beta 的舊註冊不用特別處理 |
| LEInstaller | 找同資料夾的 `LEGUI.exe`；輸出改到 `Build\Release\x86\` |
| CI | 組裝步驟把 `ShellExtension64.dll`（與 pdb）複製進 `Build\Release\x86\`；新增「檢查產品資料夾」步驟：必要檔案（`LEInstaller.exe`、`LEProc.exe`、`LEGUI.exe`、`LoaderDll.dll`、`LocaleEmulator.dll`、`ShellExtension.dll`、`ShellExtension64.dll`、`Lang\DefaultLanguage.xml`）缺一就失敗，PR 就擋得下 |
| Release | 從產品資料夾挑檔案打包成單一資料夾的 zip（仍不含 `LEConfig.xml`、測試程式、pdb）；symbols zip 同樣攤平 |
| 文件 | README、Release 說明、`docs/guides/release.md`、根目錄與模組 `AGENTS.md`、`Scripts/sign-package.ps1`、roadmap |

## 同一批一起做（前一輪回饋）

- **安裝視窗加「匯入...」按鈕**（一直顯示，沿用既有字串 `ImportProfiles`），不只在偵測到原版註冊時才能匯入。
- **README / Release 說明**：下載後先在 zip 的「內容」勾選「解除封鎖」再解壓，減少 Microsoft Defender 對每個 exe 的雲端掃描；roadmap 新增「評估程式碼簽章（Azure Trusted Signing / SignPath Foundation）」。

## 切片

1. ShellExtension：`ShellExtension64` 檔名、語系檔複製；ShellExtension.Tests 更新與新檢查。
2. LEGUI：同資料夾找 DLL；安裝視窗的「匯入...」；測試。
3. LEInstaller：同資料夾找 LEGUI、輸出位置。
4. CI 組裝與產品資料夾檢查、Release 打包。
5. 文件；PR → CI → 發 `v3.0.0-beta.3`。

## 驗證

- 單元測試：LEGUI（DLL 路徑選擇）、ShellExtension.Tests（DLL 名稱、語系檔存在）。
- CI：產品資料夾檢查、smoke test。
- 手動（維護者本機）：解壓 beta.3 → `LEInstaller.exe` → 安裝 → 右鍵選單顯示中文與 profile 清單、點選能啟動 LEProc。這一步自動測試做不到（要改 Explorer 的註冊並在 Explorer 裡操作）。
