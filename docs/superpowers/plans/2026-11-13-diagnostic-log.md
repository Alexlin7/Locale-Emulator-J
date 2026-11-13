# 計畫：執行期開關的診斷 log（`LE_DIAG_LOG`）

> **日期**：2026-11-13
> **狀態**：進行中（Slice 1）
> **相關**：`docs/roadmap.md`；舊的 `ENABLE_LOG`（`src/Core/LocaleEmulator/LocaleEmulator.h`，編譯期開關、早已壞掉）

## 1. 目的

遊戲「開不起來、顯示不對」時，目前只能靠 smoke test 或猜。需要一個**不必重新建置**、使用者自己就能打開的 log，回答：

- LE 有沒有注入成功、初始化走到哪一步、哪一步失敗（NTSTATUS）。
- 實際套用的模擬值（ACP / OEMCP / LCID / charset / 時區 / registry 轉向項目數）。
- 哪些 DLL 被載入、user32 / gdi32 / kernel32 的 hook 有沒有裝上。
- 子程序有沒有被注入。

## 2. 設計

| 項目 | 決定 |
|---|---|
| 開關 | 環境變數 `LE_DIAG_LOG`。未設定、空字串、`0` / `off` / `false` / `no` → 關閉（預設）；絕對路徑（`X:\...` 或 UNC）→ 當成輸出目錄；其他值（`1`、`on`、相對路徑…）→ 預設目錄。相對路徑不採用，否則會以遊戲的目前目錄為基準、寫進遊戲資料夾。 |
| 讀取方式 | ntdll `RtlQueryEnvironmentVariable_U`（LE 初始化時 kernel32 還沒載入）。LEProc 呼叫 `LeCreateProcess` 時環境傳 null，子程序繼承，**不需要改 LEProc / GUI / 語系檔**。 |
| 預設目錄 | `%TEMP%\LocaleEmulator\`（不存在就建立一層）。不寫在遊戲 exe 旁邊：遊戲資料夾可能唯讀（Program Files），也不該弄髒使用者的遊戲資料夾。 |
| 檔名 | `<exe 檔名>.<PID>.log`。LEProc（loader 模式）與目標程序各一份，子程序也各一份。 |
| 寫入 | ntdll `NtCreateFile`（`FILE_APPEND_DATA`、`FILE_SYNCHRONOUS_IO_NONALERT`），**每行一次 `NtWriteFile`**，沒有共用緩衝區，多執行緒不需要鎖。UTF-16LE + BOM（不經 code page 轉換，避免碰到 LE 自己 hook 的 `Rtl*` 轉換）。 |
| 格式化 | ntdll 匯出的 `_vsnwprintf`（支援 `%wZ`），堆疊上 1024 個 WCHAR，超過就截斷。 |
| 每行前綴 | `+<毫秒>` 與 TID。 |
| 關閉 | `DLL_PROCESS_DETACH` 時關檔。 |
| 遞迴風險 | LE 的 syscall filter 只有 `NtCreateUserProcess`、registry、NLS / 地區相關，**沒有** `NtCreateFile` / `NtWriteFile`；log 不呼叫任何被 hook 的 `Rtl*` 轉換函式。 |
| 關閉時的成本 | 一個全域 handle 是否為 null 的判斷。 |

舊的 `ENABLE_LOG` / `InitLog` / `LeGlobalData::LogFile` 移除；既有的 `WriteLog(...)` 呼叫點改由新的 log 實作，保留巨集名稱以減少差異。

## 3. 切片

### Slice 1：開關、檔案、初始化流程（本次）

1. 新增 `LocaleEmulator/DiagLog.h` / `DiagLog.cpp`：`LeDiagLogOpen()`、`LeDiagLogClose()`、`LeDiagLogWrite(Format, ...)`、`LeDiagLogEnabled()`；`WriteLog(...)` 對應到 `LeDiagLogWrite`。
2. `Initialize(BaseAddress)` 一開始開檔並寫標頭：exe 完整路徑、PID、模式（loader / target）、OS 版本（`RtlGetNtVersionNumbers`）、WOW64、LE DLL 路徑、建置時間。
3. `LeGlobalData::Initialize` 讀完 LEB 後寫出模擬值：ACP、OEMCP、LCID、charset、`HookUILanguageApi`、字型名稱、時區 bias 與名稱、registry 轉向項目數、系統原本的 LCID。
4. 修既有呼叫點：`lookup %s` 用 `%wZ`（`ValueName` 不保證以 null 結尾），只記成功轉向的項目；`Kernel32Hook.cpp` 去掉多餘的 `\n`。
5. `NtCreateUserProcess` hook：記錄子程序的映像路徑、PID、注入結果。
6. DLL 載入通知：記錄每個載入 / 卸載的 DLL 與基底位址（只在 log 開啟時格式化）。
7. 初始化失敗（`Initialize` 回傳 FALSE）時記錄失敗的 NTSTATUS 與步驟。

**驗證（CI）**：

- `Invoke-SmokeTest.ps1` 新增一段：只對 ja-JP 再跑一次，`LE_DIAG_LOG=<輸出目錄>`，檢查
  - 有 `LocaleTestApp.exe.*.log` 與 `LEProc.exe.*.log`；
  - 目標的 log 含 `ACP=932`、`init ok`、`hook USER32.dll`；
  - LEProc 的 log 含 `inject` 且結果為 0；
  - 原本 6 個地區（未設定 `LE_DIAG_LOG`）跑完後，預設目錄沒有新增 log 檔（預設關閉）。
- 主 smoke test 6 個地區全綠；`launch_stress=200` 一次 0 失敗（確認 log 程式碼沒有讓 LEProc 變不穩）。
- 建置的 kernel32 靜態 import 檢查照常通過。

### Slice 2：hook 命中統計（之後另議）

- HookPort dispatcher 與 `Rtl*` / user32 / gdi32 hook 入口加計數，`DLL_PROCESS_DETACH` 時輸出。需要改很多 hook 函式，等 Slice 1 實際用過再決定。

### Slice 3：參數追蹤（可選，不預定）

## 3.1 Review 修正（2026-11-13，agent review）

- 只接受絕對路徑當輸出目錄（見上表）；`TEMP` 找不到時退到 `TMP`。
- NTSTATUS 一律寫成 `status=0x%08X`（舊呼叫點用 `%p`），smoke test 比對這個字樣。
- log 在 `Initialize(BaseAddress)` 一開始就開，`kernel32 已初始化` 那條失敗路徑也記得到；失敗行在 `UnInitialize` 之前寫出，log handle 是獨立於 `LeGlobalData` 的全域變數。
- 補記失敗路徑：`TextMetricCache`、`NtInitializeNlsFiles`、NLS 檔案讀取、`InstallHookPort`、`HookNtdllRoutines`（含找不到 `LdrInitNtContinue`）、`LdrRegisterDllNotification`、子程序不是 32 位元而略過注入、沒有從 LEProc 拿到 LEB（使用預設值）。
- 格式字串禁用窄字串（`%S` / `%hs` / `%Z`），因為會經過 LE hook 的 `RtlMultiByteToUnicodeN`；`LeRtl*` 裡不呼叫 `WriteLog`。
- 檔案用 `FILE_OVERWRITE_IF`（PID 重用時覆寫舊檔）。
- CI：主測試期間把 `TEMP` / `TMP` 指到空目錄，檢查預設關閉時確實沒有 log；`LE_DIAG_LOG=1` 與絕對路徑各測一次；結束後還原環境變數；等 LocaleTestApp 結束再讀 log。
- README 註明 log 含完整路徑（通常有 Windows 使用者名稱）、不會自動刪除。

## 4. 不做

- 不加 GUI 開關（22 種語系的字串成本高；需要時用環境變數）。
- 不用 `OutputDebugString` / DebugView。
- 不記錄每一次 API 呼叫的完整參數（量太大，且可能含遊戲的文字內容）。
- 不記錄命令列（可能含使用者的路徑或帳號資訊）。

## 5. 使用方式（寫進 README / AGENTS）

```bat
:: 只對這次啟動
set LE_DIAG_LOG=1
LEProc.exe -run "C:\path\to\game.exe"
```

用右鍵選單啟動時，Explorer 不會有命令列設定的環境變數，要用 `setx LE_DIAG_LOG 1` 設成使用者環境變數並重新登入（或重開 Explorer）；用完 `setx LE_DIAG_LOG 0` 關閉。log 在 `%TEMP%\LocaleEmulator\`。

## 6. Commit

1. `[Core] feat: 執行期開關的診斷 log（LE_DIAG_LOG）`
2. `test(smoke-test): 驗證診斷 log 的輸出與預設關閉`
3. `docs: 診斷 log 的用法`
