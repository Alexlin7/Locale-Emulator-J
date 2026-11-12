# 計畫：LEProc 偶發當機（`LeCreateProcess` 內存取違規）

> **日期**：2026-11-12
> **狀態**：計畫（經 agent review 後執行）
> **相關**：`docs/roadmap.md` §2「優先」第一項

## 1. 症狀

- LEProc 偶爾在 `LoaderWrapper.LeCreateProcess` 內發生 `0xc0000005`，事件記錄的 faulting module 是 `coreclr.dll`。
- 使用者感受：「按了沒反應，要多按幾次」。
- CI：master run 43066293655（主 smoke test 的 zh-CN 逾時）、run 43208011917（E1 的 zh-CN 失敗）；同一份程式碼重跑就通過。E0、E2 都發生過，與時機實驗無關。

## 2. 現況：LEProc 怎麼啟動目標

`LoaderWrapper.Start()` → `LoaderDll!LeCreateProcess`（`src/Core/LoaderDll/LoaderDll.cpp`）：

1. 以 **LEProc 自己的 PID** 建立 LePeb，寫入 LEB。
2. 推一個 `LE_LOADER_PROCESS` 的 TEB frame，然後 **`LdrLoadDll` 把 `LocaleEmulator.dll` 載入 LEProc 自己**。
3. `LocaleEmulator.dll` 的 `DllMain` 以 loader 模式初始化：`InstallHookPort()`（改寫 ntdll 的 syscall 分派）＋ `HookNtdllRoutines()`（registry 轉向、`NtCreateUserProcess` 等 filter），**在 LEProc 程序內生效，且之後不會移除**。
4. `Ps::CreateProcess` 建立目標；被 hook 的 `NtCreateUserProcess`（`LeNtCreateUserProcess`）在建立後呼叫 `InjectSelfToChildProcess`，把 LePeb 複製給子程序並注入。

LEProc 是 .NET 10（CoreCLR）程序，此時已經有 GC、finalizer、tiered JIT、EventPipe 等背景執行緒在跑。

## 3. 假設

| # | 假設 | 依據 | 可能性 |
|---|---|---|---|
| H1 | **在多執行緒的 CoreCLR 程序裡即時改寫 ntdll 的 syscall 路徑**，與其他 CLR 執行緒正在執行的 syscall 競爭（改到一半的指令、hook 狀態不一致），或之後 CLR 執行緒的 syscall 被 LE 的 filter（registry 轉向等）處理時出錯 | faulting module 是 coreclr（不是 LEProc 主執行緒的 P/Invoke 呼叫點）；偶發、與時機有關；E0 / E2 都有（兩者在 loader 端行為相同） | 高 |
| H2 | P/Invoke 參數問題：`STARTUPINFO.cb` 從未設定（=0）、`ref STARTUPINFO` 內含 `string` 欄位 | `cb=0` 是確定性的，不像偶發；但仍是應該修的小瑕疵 | 低 |
| H3 | LePeb section 以 LEProc 的 PID 命名，PID 重用時撞到殘留 section | 需要 PID 重用 + section 未關，機率極低 | 很低 |

## 4. 步驟

### S1 量測（只加測試工具，不改產品程式碼）

- 新增 `tests/SmokeTest/Invoke-LaunchStress.ps1`：用同一個 `.le.config` 連續以 `LEProc -run` 啟動 LocaleTestApp N 次（預設 100）。每次記錄：
  - LEProc 結束碼（`Start-Process -PassThru`，等待上限 30 秒；逾時就 kill 並記為 TIMEOUT）。LEProc 會等目標結束，所以結束碼可用；`0xC0000005` = `-1073741819`。
  - 目標是否產生 `locale_result.txt`、ACP 是否正確。
  - 輸出統計（OK / CRASH / TIMEOUT / WRONG）與每種失敗的前幾筆。
- 腳本開頭設定 WER LocalDumps（`HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\LEProc.exe`，`DumpType=1` minidump，`DumpCount=5`，`DumpFolder=<輸出目錄>\dumps`），結束時還原（刪掉該機碼）。
- 若找得到 `cdb.exe`（Windows SDK Debugging Tools，x86 版），對每個 dump 跑 `!analyze -v; ~*kb; lm` 並把輸出存檔、在主控台印出摘要。找不到就略過，只上傳 dump。
- 只在 CI 跑：`ci.yml` 新增 `workflow_dispatch` 輸入 `launch_stress`（整數字串，預設 `0`＝不跑）。步驟 `continue-on-error: true`，結果寫進 Job Summary，dump 與報告上傳為 artifact。
- 先以 N=200 跑一次取得**基準失敗率**（目前的 `LeCreateProcess`）。

### S2 判讀

- dump 的當機執行緒若不是 LEProc 主執行緒、堆疊經過 ntdll syscall stub / `Wow64Transition` / `LocaleEmulator.dll` → H1 成立。
- 若 N=200 一次都沒重現：再跑一次 N=500；仍無法重現就回報使用者，改以 S3 的理由（設計上消除風險）決定是否採用，**不宣稱已修好**。

### S3 修正候選：改用 `LeCreateProcess2`（LEProc 不再載入 `LocaleEmulator.dll`）

`LoaderDll.cpp` 已有上游寫好、但沒有匯出的 `LeCreateProcess2`：用 ml 的 `CreateProcessWithDll(CPWD_BEFORE_KERNEL32, ...)` 直接建立並在 kernel32 之前注入目標，LePeb 以**目標的 PID** 建立，LEProc 自己不載入 `LocaleEmulator.dll`、不安裝任何 hook。

1. `[Core]` `LoaderDll.def` 匯出 `LeCreateProcess2`（`LeCreateProcess` 保留，`Loader.exe` 仍用它）。
2. LEProc：`LoaderWrapper` 新增 `LeCreateProcess2` 的 P/Invoke，`Start()` 改呼叫它；`PML_PROCESS_INFORMATION`（多一個 `FirstCallLdrLoadDll`）作為輸出結構，取 `hProcess` / `hThread` 等待與關閉。順手把 `STARTUPINFO.cb` 設成 `Marshal.SizeOf<STARTUPINFO>()`（H2）。
3. 先檢查 `LeCreateProcess2` 與 hook 路徑（`InjectSelfToChildProcess`）寫給子程序的 LePeb 是否等價：LEB 與 registry 轉向項目、`LdrLoadDllAddress` / `LdrLoadDllBackup`、`LeDllFullPath`、**`LeDllDirPath`**（`LeCreateProcess2` 目前沒寫，要確認 LocaleEmulator 端是否用到；用到就補上）。
4. 驗證：
   - 主 smoke test 6 個地區全綠（CHECK 全過、PROBE 結果與修改前相同）、Local probe self-check 正常。
   - `DebugMode`（`RunWithSuspend`，`CREATE_SUSPENDED`）：`LeCreateProcess2` 內部一律 suspended 建立，依呼叫端旗標決定是否 resume，行為一致。
   - S1 的壓力測試 N=200（或與基準相同的 N）：當機數 0，且明顯低於基準。
5. 若 `LeCreateProcess2` 在 smoke test 有任何退步：退回，改評估其他做法（例如在 LEProc 內縮小 hook 範圍），先回報使用者。

### S4 文件

- `docs/roadmap.md`：勾掉或更新「查 LEProc 偶發當機」，附 CI run 編號與前後失敗率。
- `src/LEProc/AGENTS.md`：說明 LEProc 改用 `LeCreateProcess2`、為什麼不能在 CoreCLR 程序內載入 `LocaleEmulator.dll`。
- `src/Core/AGENTS.md`：`LoaderDll` 的兩個入口與用途；壓力測試的跑法。
- `docs/guides/github-actions-ci.md`：新增 `launch_stress` 輸入的說明。
- 本計畫文件補記結果。

## 5. 不做

- 不改 `LocaleEmulator.dll` 在目標程序內的 hook 行為（目標端的 smoke test 是全綠的）。
- 不改 `Loader.exe`（命令列工具，不是 CoreCLR 程序，沒有同樣的風險；之後要統一可另議）。
- 不碰使用者的遊戲資料夾；所有驗證都在 CI 用 LocaleTestApp。

## 6. 切片與 commit

1. `test(smoke-test): 新增 LEProc 連續啟動壓力測試與 WER dump 收集`（腳本 + ci.yml 輸入 + CI 指南）→ 跑基準。
2. `[Core] feat(loaderdll): 匯出 LeCreateProcess2`（必要時補 `LeDllDirPath`）。
3. `fix(leproc): 改用 LeCreateProcess2，LEProc 不再載入 LocaleEmulator.dll`（含 `STARTUPINFO.cb`）→ smoke test + 壓力測試。
4. `docs: ...`。
