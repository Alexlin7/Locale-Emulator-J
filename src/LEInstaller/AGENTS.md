# src/LEInstaller/AGENTS.md

LEInstaller.exe（C++23，**x86，靜態 CRT**）放在發佈 zip 的根目錄，給習慣原版「解壓 → 執行 LEInstaller」的使用者：先確認 x86 與 x64 的 .NET 10 Desktop Runtime 都已安裝（缺的話引導下載安裝），再開啟同資料夾的 `LEGUI.exe --installer` 註冊右鍵選單。設計見 `docs/superpowers/specs/2026-11-15-leinstaller-design.md`。

跨模組鐵則見根目錄 [`AGENTS.md`](../../AGENTS.md)。

## 鐵則

1. **靜態 CRT（`/MT`）、不依賴 .NET 與 VC++ 可轉散發套件**：它要在 runtime 還沒裝的電腦上執行。改完用 `dumpbin /dependents` 確認只有系統 DLL（沒有 `VCRUNTIME*` / `MSVCP*`）。
2. **不註冊右鍵選單**：註冊一律由 LEGUI（`ShellExtensionRegistrar`）負責，LEInstaller 只開啟 `LEGUI.exe --installer`，不要在 C++ 重寫一份註冊程式。
3. **下載的安裝程式執行前必須通過 `IsSignedByMicrosoft`**（Authenticode 有效、簽署憑證的 O = `Microsoft Corporation`），失敗就刪檔、不執行。
4. **runtime 檢查不信任 registry 的 `sharedfx` 版本清單**：它會殘留已移除的版本，一律以 `shared\Microsoft.WindowsDesktop.App\<版本>` 資料夾為準。
5. **字串改動要四種語言一起改**（en / zh-TW / zh-CN / ja，`Strings.cpp`），`{}` 佔位符的數量要與英文相同（測試會檢查）。對外連結只放微軟官方網址。

## 結構

| 檔案 | 內容 |
|---|---|
| `main.cpp` | 流程：32 位元 Windows / 找不到同資料夾 `LEGUI.exe` 的錯誤 → 檢查 runtime → TaskDialog 詢問（自動下載安裝 / 開啟下載頁 / 取消）→ 下載、驗章、`/install /passive /norestart` → 重新檢查 → 開 LEGUI（`SEE_MASK_NOZONECHECKS`，未解除封鎖時不再對 LEGUI 跳第二次安全性警告） |
| `InstallSteps.h` | 缺少的 runtime 依序安裝（x86 → x64）；任一步取消或失敗就停，取消會回到詢問畫面，不會接著跳下一個下載與 UAC |
| `RuntimeCheck.*` | Desktop Runtime 偵測：registry 的 `InstallLocation`（32 位元 view）→ 預設資料夾 → 版本資料夾 |
| `Download.*` | `URLDownloadToFileW`（跟隨轉址、不用快取、可取消、回報進度） |
| `Signature.*` | `WinVerifyTrust` + 簽署憑證的組織名稱 |
| `Strings.*` | 四種語言的字串與 `PickLanguage`（依 `GetUserDefaultUILanguage`） |
| `LEInstaller.manifest`、`LEInstaller.rc` | Common Controls 6（TaskDialog）、Per-Monitor V2 DPI；圖示沿用 LEGUI；版本資訊 |

輸出到 `Build\<組態>\x86\LEInstaller.exe`（產品資料夾，與 `LEGUI.exe` 等同一層；`docs/superpowers/specs/2026-11-16-flat-product-layout.md`）。

## 測試

`tests/LEInstaller.Tests/`（x86 console exe，無框架）把除了 `main.cpp` 以外的原始碼直接編進測試：

```powershell
Build\Release\x86\LEInstaller.Tests.exe
$env:LE_INSTALLER_NETWORK_TEST = "1"; Build\Release\x86\LEInstaller.Tests.exe   # 另外實際下載 x86 安裝程式並驗章（不安裝）
```

- 版本資料夾名稱、模擬的 dotnet 資料夾結構、狀態判斷、預設路徑
- 安裝順序：x86 取消或失敗時不會開始 x64
- 語言選擇、四種語言字串齊全且佔位符一致
- 簽章：`dotnet.exe`（微軟）通過；Git for Windows 的 `git.exe`（有效但非微軟）、未簽章 exe、非 PE 檔、不存在的檔案都拒絕
- 網路測試（預設跳過）：下載、進度到 100%、檔案大於 10 MB、微軟簽章、取消會回傳 `E_ABORT` 並刪檔

UI 流程（TaskDialog、UAC、實際安裝）沒有自動測試，改動後手動確認：runtime 齊全時直接開 LEGUI、在空資料夾執行顯示「找不到 LEGUI.exe」。
