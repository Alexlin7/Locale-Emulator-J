# 計畫：LEProc 偶發當機（`LeCreateProcess` 內存取違規）

> **日期**：2026-11-12
> **狀態**：執行中。S1 已依 agent review 修正；S3 的候選改為「LoaderDll 自行注入」（review 指出 `LeCreateProcess2` 走 debugger 注入，風險較大）
> **相關**：`docs/roadmap.md` §2「優先」第一項

## 1. 症狀

- LEProc 偶爾在 `LoaderWrapper.LeCreateProcess` 內發生 `0xc0000005`，事件記錄的 faulting module 是 `coreclr.dll`。
- 使用者感受：「按了沒反應，要多按幾次」。
- CI：master run 43066293655（主 smoke test 的 zh-CN 逾時）、run 43208011917（E1 的 zh-CN 失敗）；同一份程式碼重跑就通過。E0、E2 都發生過，與時機實驗無關。

## 2. 現況：LEProc 怎麼啟動目標

`LoaderWrapper.Start()` → `LoaderDll!LeCreateProcess`（`src/Core/LoaderDll/LoaderDll.cpp`）：

1. 以 **LEProc 自己的 PID** 建立 LePeb，寫入 LEB。
2. 推一個 `LE_LOADER_PROCESS` 的 TEB frame，然後 **`LdrLoadDll` 把 `LocaleEmulator.dll` 載入 LEProc 自己**。
3. `LocaleEmulator.dll` 的 `DllMain` 以 loader 模式初始化：`InstallHookPort()` 在 WOW64 下改寫所有執行緒共用的 `Wow64SyscallJumpStub`（`HookPort.cpp` 約 648–709 行，用非原子的 16 bytes 寫入，**寫入時沒有暫停其他執行緒**）；`HookNtdllRoutines()` 在 loader 模式只加 `NtCreateUserProcess` 一個 filter（`IsLeLoader()` 為真就 return，不裝 `Rtl*` 與 registry filter）。之後 LEProc 所有執行緒的 syscall 都經過 LE 的 dispatcher，直到程序結束。
4. `Ps::CreateProcess` 建立目標；被 hook 的 `NtCreateUserProcess`（`LeNtCreateUserProcess`）在建立後呼叫 `InjectSelfToChildProcess`，把 LePeb 複製給子程序並注入。

LEProc 是 .NET 10（CoreCLR）程序，此時已經有 GC、finalizer、tiered JIT、EventPipe 等背景執行緒在跑。

## 3. 假設

| # | 假設 | 依據 | 可能性 |
|---|---|---|---|
| H1 | **在多執行緒的 CoreCLR 程序裡改寫共用的 syscall 閘道**：寫到一半時其他 CLR 執行緒可能執行到殘缺的指令；之後 CLR 執行緒的每個 syscall 也都繞經 LE 的 dispatcher | faulting module 是 coreclr（不是 LEProc 主執行緒的 P/Invoke 呼叫點）；偶發、與時機有關；E0 / E2 都有（兩者在 loader 端行為相同） | 高 |
| H2 | P/Invoke 參數問題：`STARTUPINFO.cb` 從未設定（=0）、`ref STARTUPINFO` 內含 `string` 欄位 | `cb=0` 是確定性的，不像偶發；但仍是應該修的小瑕疵 | 低 |
| H3 | LePeb section 以 LEProc 的 PID 命名，PID 重用時撞到殘留 section | 需要 PID 重用 + section 未關，機率極低 | 很低 |

## 4. 步驟

### S1 量測（只加測試工具，不改產品程式碼）

- 新增 `tests/SmokeTest/Invoke-LaunchStress.ps1`：用同一個 `.le.config` 連續以 `LEProc -run` 啟動 LocaleTestApp N 次（預設 100）。每次記錄：
  - LEProc 結束碼（`Start-Process -PassThru`，等待上限 30 秒；逾時就 kill 並記為 TIMEOUT）。LEProc 會等目標結束，所以結束碼可用；`0xC0000005` = `-1073741819`。
  - 目標是否產生 `locale_result.txt`、ACP 是否正確。
  - 輸出統計（OK / CRASH / TIMEOUT / WRONG）與每種失敗的前幾筆。
- 腳本開頭設定 WER LocalDumps（`HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\LEProc.exe`，`DumpType=2` full dump，`DumpCount=5`，`DumpFolder=<輸出目錄>\dumps`），結束時還原（刪掉該機碼）。
- LEProc 的 `Main` 回傳 `void`，正常結束一律是 0，所以**非 0 一律記為 CRASH** 並記錄 hex（不只 `0xC0000005`，CoreCLR 的 fatal error 可能是 `0xC0000409` / `0x80131506`）。
- TIMEOUT 有兩種可能：卡住，或 `Start()` 回傳錯誤後跳出的 MessageBox（CI 是系統管理員）。kill 之前先用 x86 runtime 內附的 `createdump.exe` 取 full dump，並記錄主視窗標題。
- 若找得到 `cdb.exe`（Windows SDK Debugging Tools，x86 版），對每個 dump 跑 `!analyze -v; ~*kb; lm` 並把輸出存檔、在主控台印出摘要。找不到就略過，只上傳 dump。
- 只在 CI 跑：`ci.yml` 新增 `workflow_dispatch` 輸入 `launch_stress`（整數字串，預設 `0`＝不跑）。步驟 `continue-on-error: true`，結果寫進 Job Summary，dump 與報告上傳為 artifact。
- 先以 N=200 跑一次取得**基準失敗率**（目前的 `LeCreateProcess`）。

### S2 判讀

- dump 的當機執行緒若不是 LEProc 主執行緒，EIP 落在 `Wow64SyscallJumpStub` 附近、LE 的 `HookSysEnter_*` / dispatcher，或堆疊經過 `LocaleEmulator.dll` → H1 成立。
- 若 N=200 一次都沒重現：再跑一次 N=500；仍無法重現就回報使用者，改以 S3 的理由（設計上消除風險）決定是否採用，**不宣稱已修好**。

### S3 修正候選（依 S2 的結果決定）

**不採用 `LeCreateProcess2`**（review 結論）：它用 ml 的 `CreateProcessWithDll(CPWD_BEFORE_KERNEL32)`，以 `DEBUG_PROCESS` 建立目標、LEProc 當 debugger，`DbgUiWaitStateChange` 沒有逾時（可能變成新的「按了沒反應」），目標的 `NtGlobalFlag` 會帶 debug heap 旗標（只清了 `BeingDebugged`，遊戲行為可能改變），x64 目標會直接失敗（hook 路徑是略過注入照常啟動），而且上游從未匯出、沒用過。

**首選：LoaderDll 自行注入，注入方式與目前的 hook 路徑相同**

1. `[Core]` LoaderDll 新增入口（例如 `LeCreateProcess3`，簽章同 `LeCreateProcess`）：
   - 以 `CREATE_SUSPENDED` 建立目標（直接呼叫 kernel32 的 `CreateProcessInternalW`，不經 hook）。
   - 以 `LoadPeImage` 把 `LocaleEmulator.dll` 讀進 LEProc 的記憶體（**只映射、不執行 DllMain、不安裝 hook**），照 `InjectSelfToChildProcess`（`NtdllHook.cpp` 約 264–404 行）的作法：在目標配置記憶體、relocate 後寫入、在 `LdrLoadDll` 開頭寫 JMP 到影像裡的 `LoadFirstDll`、建立目標 PID 的 LePeb（LEB、registry 轉向項目、`LdrLoadDll` 備份、`LeDllFullPath` / `LeDllDirPath`）。
   - `LoadFirstDll` 的位置：由 `LocaleEmulator.dll` 匯出（或固定 ordinal），LoaderDll 在映射後用匯出表找到。
   - 依呼叫端旗標決定是否 resume；失敗時終止目標。
   - 目標是 x64 時維持現行行為：不注入、照常啟動（或回傳錯誤，由 S3 實作時決定並記錄）。
2. 盡量讓 hook 路徑與新入口共用同一份注入程式碼（例如抽成 LoaderDll 與 LocaleEmulator 都 include 的 inline 函式），避免兩份實作分歧。
3. LEProc：`LoaderWrapper.Start()` 改呼叫新入口；順手修 `STARTUPINFO`（設 `cb`、加 `CharSet.Unicode` 或把字串欄位改成 `IntPtr`）。`LeCreateProcess` 保留給 `Loader.exe`。

**驗證**：主 smoke test 6 個地區全綠、PROBE 結果與修改前相同、Local probe self-check 正常；`RunWithSuspend` 行為一致；壓力測試以與基準相同的 N 跑，失敗數為 0 且明顯低於基準。任何退步就退回並先回報使用者。

**若 S1 / S2 顯示原因不是 H1**：依 dump 結果另擬修正，先回報使用者，不硬套上面的做法。

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
