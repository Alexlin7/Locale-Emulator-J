# tests/SmokeTest/AGENTS.md

Smoke test 是唯一能驗證「hook 真的生效」的測試：用不同地區的 profile 經 LEProc 啟動 `LocaleTestApp.exe`，比對它寫出的結果。**改 Core 之後一定要跑。**

跨模組鐵則見根目錄 [`AGENTS.md`](../../AGENTS.md)；使用說明見 [`README.md`](README.md)。

## 運作方式

1. `Invoke-SmokeTest.ps1 [-BuildDir <dir>] [-Verbose]`：預設對 `Build/Release/x86/` 執行；`-Verbose` 會列出每一項的 OK。
2. 對每個地區（ja-JP、zh-TW、zh-CN、zh-Hant-TW、ko-KR、en-US）在 `LocaleTestApp.exe` 旁邊產生 `LocaleTestApp.exe.le.config`（`RedirectRegistry=true`、時區 Tokyo Standard Time），用 `LEProc.exe -run` 啟動。
3. LocaleTestApp 把結果寫到同目錄的 `locale_result.txt`（`KEY=VALUE`，因為 LEProc 啟動的程序的 stdout 拿不到）。
4. 腳本檢查 `ACP`、`OEMCP`、`LCID` 與地區表相符、`TZBIAS=-540`，且所有 `CHECK_*` 都是 `OK`。

## `CHECK_*` 的寫法（新增檢查時必讀）

- **預期值一律從系統 API 推導**，不寫死：例如以 `GetACP()` / `GetOEMCP()` / `GetUserDefaultLCID()`（腳本已經比對過它們等於 profile）為準，用 kernelbase 的 `MultiByteToWideChar` / `WideCharToMultiByte` 搭配**明確的 code page 編號**算出參考答案（不受 LE 影響）。
- **要有鑑別力**：沒有 LE 時應該會 `FAIL`。CI runner 是 en-US（ACP 1252、OEMCP 437、UTC），en-US 這一列分辨不出有沒有模擬，其他地區才有。
- **新增後先確認 E0（正式版）會過**：E0 就失敗代表是 LE 現有的缺口，要和時機實驗的結論分開記錄。
- 測試字元只取該 code page 能精確表示的部分（`RepresentableSample`）。
- 值的格式：`CHECK_<名稱>=OK` 或 `CHECK_<名稱>=FAIL <說明>`；說明要包含 code page 與實際得到的值（十六進位），方便從 CI log 判讀。

目前的檢查類別：ntdll `Rtl*` 轉換、kernelbase（`CP_ACP` / `CP_OEMCP` / `CP_THREAD_ACP` 雙向、`GetCPInfoEx`、`IsDBCSLeadByte(Ex)`、地區資訊含 ANSI 版）、user32 `CharNextA`、CRT `setlocale` 後的 `wcstombs` / `mbstowcs`、ANSI 檔案 API、user32 ANSI 視窗標題、registry 轉向、gdi32 字型 charset，另有時區。尚未涵蓋：`SendMessageA` 等訊息轉換、`EnumFontFamilies*`、進階 registry 轉向與 UI 語言 API。

**系統 ACP 65001（UTF-8 beta）**：有些漏洞只在這個系統設定下出現（例如 `CHARNEXTA_DBCS`）。CI runner 是 1252 且不能重開機切換，只能在本機開啟 UTF-8 beta、重開機後執行 `Invoke-SmokeTest.ps1 -Verbose`。

## 時機實驗

CI 會另外建 `LE_EXPERIMENT_TIMING=1|2` 的 `LocaleEmulator.dll`，複製一份執行目錄後跑同一套 smoke test（`continue-on-error`）。實驗版會設定環境變數 `LE_EXPERIMENT`，LocaleTestApp 輸出、腳本顯示，用來證明延後路徑確實執行。詳見 `src/Core/AGENTS.md` 與 spec §4.5。

## 注意

- 不要拿使用者的遊戲或應用程式來跑 smoke test；目標程式只有 LocaleTestApp。
- LocaleTestApp 是 C++23、靜態 CRT（`/MT`）的 x86 console 程式。
