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

測試樣本 `kSample` 用 `\u` 跳脫寫成（原始碼沒有 BOM；直接寫中日韓字元會被建置機的 code page 讀成亂碼，曾經發生過）。新增非 ASCII 字元時同樣用 `\u` 跳脫。

目前的檢查類別：ntdll `Rtl*` 轉換、kernelbase（`CP_ACP` / `CP_OEMCP` / `CP_THREAD_ACP`、`GetCPInfoEx`、地區資訊）、ANSI 檔案 API、user32 ANSI 視窗標題、registry 轉向、gdi32 字型 charset，另有時區。尚未涵蓋：`SendMessageA` 等訊息轉換、`EnumFontFamilies*`、進階 registry 轉向與 UI 語言 API。

## `PROBE_*` 與 `INFO_*`

- `PROBE_*`：格式同 `CHECK_*`，用來**探測候選 API**（例如找系統 ACP 65001 下漏攔的 API）。`Invoke-SmokeTest.ps1` 只列出失敗項目與統計，**不影響結果**。在 CI 上穩定通過、確定要保護的行為，再升級成 `CHECK_*`。
- `INFO_*`：只輸出不判斷的診斷資訊，例如 `INFO_SAMPLE_UTF16LE`（實際用到的測試樣本）、`INFO_OS_VERSION`、CRT 的 locale 字串、字型解析出的 charset。`-Verbose` 時會列出。
- 目前在 CI 上就失敗的 PROBE（代表 LE 現有的缺口，與 65001 無關）記在 `docs/roadmap.md`。

## 在使用者電腦上探測（給本機 agent）

CI 測不到的環境（例如開著 UTF-8 beta、系統 ACP 65001 的電腦）由使用者或本機的 agent 用 `Invoke-LocalProbe.ps1` 跑：

```powershell
# 1. 建置 LocaleTestApp（只需要這一個；LE 本體可以用使用者安裝的版本）
msbuild tests/SmokeTest/LocaleTestApp/LocaleTestApp.vcxproj /p:Configuration=Release /p:Platform=Win32 "/p:SolutionDir=$PWD\"

# 2. 跑探測：-LeDir 指向使用者平常玩遊戲用的 LE 安裝目錄（或本機建置的 Build\Release\x86）
.\tests\SmokeTest\Invoke-LocalProbe.ps1 -LeDir "<LE 安裝目錄>" -Region ja-JP
```

- 腳本把 LocaleTestApp 複製到 `%TEMP%\LEProbe_<時間>\work`，不經 LE 與經 LE 各跑一次，在 `%TEMP%\LEProbe_<時間>\` 留下 `baseline.txt`、`le.txt`、`report.md`。
- profile 參數（`-Region`、`-Timezone`、`-RedirectRegistry`、`-AdvancedRedirection`）盡量和使用者玩那款遊戲時的 profile 相同；不確定就用預設值（ja-JP、Tokyo Standard Time、RedirectRegistry=true、AdvancedRedirection=false）。
- **回報**：把 `report.md` 的完整內容貼回對話（裡面只有 code page、十六進位的測試結果與 OS 版本；`LE directory` 一行含安裝路徑，使用者不想公開可以刪掉）。
- **不要做**：
  - 不要切換 UTF-8 beta 或其他地區設定：要改系統設定並重開機，一定先問使用者。報告的 `Environment` 會顯示目前是否開著。
  - 不要對遊戲或使用者的其他程式執行或掃描；目標程式只有 LocaleTestApp。
  - 不要修改 LE 安裝目錄裡的檔案（腳本只讀取 `LEProc.exe` 等）。

## 時機實驗

CI 會另外建 `LE_EXPERIMENT_TIMING=1|2` 的 `LocaleEmulator.dll`，複製一份執行目錄後跑同一套 smoke test（`continue-on-error`）。實驗版會設定環境變數 `LE_EXPERIMENT`，LocaleTestApp 輸出、腳本顯示，用來證明延後路徑確實執行。詳見 `src/Core/AGENTS.md` 與 spec §4.5。

## 注意

- 不要拿使用者的遊戲或應用程式來跑 smoke test；目標程式只有 LocaleTestApp。
- LocaleTestApp 是 C++23、靜態 CRT（`/MT`）的 x86 console 程式。
