# Roadmap：待辦、取捨與風險

> **活文件**：每完成一項、做了新的取捨或發現新的風險，就更新這份文件（改勾選狀態或增刪項目，不必保留歷史；歷史在 git 與 `docs/superpowers/`）。
> **最後整理**：2026-11-12（依「目標」重新排序：x64 與 D7 暫緩）
> **相關文件**：[`AGENTS.md`](../AGENTS.md)、[Core 模組化與 x64 設計](superpowers/specs/2026-11-11-core-modularization-and-x64-design.md)、[Phase 2 計畫](superpowers/plans/2026-11-11-core-phase2-hook-engine.md)

## 0. 目標

**讓維護者自己玩的遊戲能正常開、正常顯示。** 這些遊戲都是 32 位元，所以 x86 Core 是唯一需要的路徑。

挑工作的判斷順序：

1. 會讓某款遊戲開不起來、亂碼、當機的問題，優先處理。
2. 能幫忙查出「某款遊戲為什麼不正常」的工具（診斷 log、測項），其次。
3. 跟玩遊戲無關的（程式碼品質、靜態分析、新平台、介面改善），只在順手或成本很低時做。

沒有實際需求的情境不投入，例如在 Sandboxie 這類沙箱裡執行 LE（`LeFindSectionObject` 的 `\Sandbox` 路徑）。

## 1. 現況摘要

| 項目 | 狀態 |
|---|---|
| x86 Core | 正式使用；smoke test 6 個地區、每個地區 32 個 `CHECK_*` 加時區全綠 |
| 已知有問題的遊戲 | 無（ExHIBIT 遊戲在 65001 下的亂碼已確認由目前的 fork 版修好） |
| x64 Core | **暫緩**。能編譯（`LoaderDll64.dll` / `LocaleEmulator64.dll`），不安裝 hook、不發佈 |
| D7 兩段式 Core | **暫緩**。時機實驗 E1、E2 在 smoke test 範圍內全 PASS；E2 實測 5 款遊戲沒有任何一款比 E0 好或差 |
| C++ 標準 | Core C++17（加 `/Zc:alignedNew-`）；ShellExtension 與 C++ 測試 C++23 |
| 靜態分析 | CodeQL（C# + C/C++）在 push / PR 到 `master`、每週排程時執行；剩 3 筆結果，皆判斷為誤報；另 2 筆已 dismiss 為 Won't fix（見 §2「已完成」） |

## 2. TODO

依優先順序排列。`(spec §x)` 指向設計文件的章節。

### 優先：影響遊戲能不能玩

- [ ] **查 LEProc 偶發當機**：`LoaderWrapper.LeCreateProcess` 內存取違規（`0xc0000005`，faulting module 顯示 coreclr.dll），E0、E2 都會發生，本機 smoke test 的偶發逾時即此問題。CI 上也出現過：master 的 run 43066293655（主 smoke test 的 zh-CN 逾時）、run 43208011917（E1 的 zh-CN 失敗），同一份程式碼重跑就通過。症狀是「按了沒反應、要多按幾次」。
- [ ] **新遊戲出問題時**：照 65001 那次的做法，先在 LocaleTestApp 加能重現的 `PROBE_*` / `CHECK_*`，CI 測不到的環境用 `Invoke-LocalProbe.ps1` 在使用者主機上跑，找到漏攔的 API 再修。

### 其次：幫忙查出遊戲為什麼不正常

- [ ] **Core 的可選診斷 log**（目前 `WriteLog` 不輸出任何東西）：遊戲沒效時目前沒有任何線索。建議用 ntdll 的 `DbgPrint`（用 DebugView 看），以環境變數開關，預設關閉。PROBE 全部通過、遊戲卻仍不正常時，靠它找出遊戲實際呼叫的 API。
- [ ] **smoke test 補上遊戲常用但還沒測的 API**：`SendMessageA` 等 user32 訊息轉換、`EnumFontFamilies*`（字型選單）、進階 registry 轉向（`IsAdvancedRedirection`）與 UI 語言 API。

### 順手或成本很低時再做

- [ ] **`CharNextA` 在英文系統上沒有照模擬的 code page 判斷雙位元組字元**（低優先）：`PROBE_USER32_CHARNEXT_A` 在 CI（en-US、ACP 1252）的 ja / zh / ko 都失敗（例如 cp=932 預期 35 步、實際 50 步），但在使用者的中文系統（ACP 65001）上經 fork 版通過。推測 user32 在非雙位元組的系統上直接逐位元組前進、不查 code page（未驗證）。只影響在英文系統上用 LE 的人，對維護者自己玩遊戲沒有影響。
- [ ] **LEGUI 也檢查 `.le.config` 所在目錄是否存在**：直接開 LEGUI 或把不存在的路徑拖進去時，仍會因目錄不存在而判定不可寫、觸發 UAC 後顯示「目錄無寫入權限」。要新增訊息字串，需處理 22 種語系。
- [ ] **評估 LEProc `Main` 最外層的空 `catch {}`**：目前所有未預期的例外都被靜默吞掉（例如 `-runas` 的 GUID 在 `LEConfig.xml` 找不到時 `.First` 丟例外，什麼都不會發生）。改成顯示例外能幫助回報問題，但會讓過去一直靜默的錯誤全部浮上來，影響面要先評估。
- [ ] LoaderDll / Loader 的 vcxproj 也改用 `$(OutDir)` / `$(IntDir)`（目前把 `.\Release\` 寫死）
- [ ] CodeQL 剩下的結果（皆判斷為誤報）：`cpp/incorrect-string-type-conversion`（`Utility.cpp` 的 atom 轉型）、`cpp/suspicious-pointer-scaling` 兩筆（`NtdllHook.cpp` 刻意以元素為單位的指標運算），可在 GitHub 上 dismiss
- [ ] 更多 CodeQL 自訂查詢：例如 `LocaleEmulator.dll` 的 hook 裡不准呼叫 user32 / gdi32（延遲載入可能在 kernel32 之前觸發）
- [ ] **LocaleTestApp 的輸出改用 `std::format`**（下次加 probe 時順手做第一片）：目前有 83 處 `sprintf_s` / `fprintf` 搭配固定大小的 `char` 緩衝區。先寫一個以 `std::format` 為基礎的 `Emit(name, fmt, args...)`，把 `Check` / `Probe` / `Info` 改過去，其餘呼叫點逐步替換。好處：不用算緩衝區大小；格式與參數型別在編譯期檢查；`std::format` 預設與地區無關，不會被 `ProbeCrt` 呼叫的 `setlocale` 影響輸出。其他 C++（ShellExtension、測試專案、LoaderDll）不為了換成新寫法而改：程式碼量小、效能不在這裡，真的要改功能時再順手用

### 暫緩（遊戲都是 32 位元，目前沒有需求）

重新啟動的條件：要玩 64 位元的遊戲，或 x86 Core 遇到只有兩段式架構才能解的問題。重啟前先讀第 5 節的「重啟 x64 前要先處理的事」。

- [ ] **D7 定案**；成立的話，寫兩段式 Core 的實作計畫（第一段只負責在 kernel32 初始化後載入第二段，第二段可以用 CRT）。實測紀錄在 spec §4.5。
- [ ] Phase 2 剩下的項目：A2b（正式實作對齊版 `operator new/delete`）、A3 / A4（Core 升 C++20 / C++23，風險在 ml.h）、Part B（HookEngine 介面 `Le::Hook`）
- [ ] x64（Phase 3～7，spec §5）：x64 hook 引擎（Zydis）、移植 `LeRtl*` / `NtQueryDefault*` / 時區 hook、LEProc64、user32 / gdi32 hook、減少 ml 依賴、x64 的大量截斷警告
- [ ] Windows 11 右鍵選單第一層（`IExplorerCommand` + 有簽章的 sparse package）：Windows 11 的「顯示其他選項」裡仍然可以用
- [ ] （可選）SonarQube Cloud、實驗版 DLL 加進 CI artifact

### 已完成（最近）

- [x] **系統 ACP 65001（UTF-8 beta）下的亂碼**：一款 ExHIBIT 引擎的遊戲在原版 LE 2.5.0.1 與較早的 fork 版下劇本呈「縺ゅ」型亂碼，**目前的 fork 版（3.0.0.0）正常**（使用者實測）。原因由使用者主機上的 `Invoke-LocalProbe.ps1`（OS build 26300、ACP 65001）確認：原版經 LE 後 `GetACP()` 回報 932，但 ntdll `Rtl*` 轉換仍用 UTF-8（「日」轉成 `E697A5` 而非 `93FA`），27 項 CHECK / PROBE 失敗，集中在 `Rtl*`、ANSI 檔案 API、user32 視窗文字、環境變數與目前目錄；fork 版同一組參數 0 項失敗。即 `src/Core/AGENTS.md` 鐵則 4 的 NLS 表替換失效，由 `LeRtl*` hook 修正。
- [x] smoke test 輸出 OS 版本（`INFO_OS_VERSION`）
- [x] LocaleTestApp 新增 32 個 `PROBE_*` 與本機探測腳本 `Invoke-LocalProbe.ps1`（CI run 43254226628），之後遇到「只在使用者主機才重現」的問題可以沿用
- [x] LocaleTestApp 的測試樣本原本因原始碼編碼被讀成亂碼，`CHECK_*` 幾乎沒測到中日韓字元；改用 `\u` 跳脫後，現有 32 個 `CHECK_*` 在真正的雙位元組字元下仍全部通過（CI run 43254226628）
- [x] `.le.config` 的 `<Location>` 相容（CI run 43208011917）
- [x] `-run` 的路徑不存在時給錯誤訊息（CI run 43208011917）
- [x] CodeQL `cpp/alloca-in-loop`：`LeFindSectionObject` 改用 `NtAllocateVirtualMemory`（CI run 43236475905）
- [x] CodeQL alert #2 `cpp/alloca-in-loop`（`HookPort.cpp` 的 `HpServiceDispatcherInternal`）dismiss 為 Won't fix：filter 從堆疊頂端讀這份參數複本，FASTCALL 由被呼叫端清堆疊，所以 `_alloca` 必須在迴圈內，堆疊也不會增長；誤刪會讓所有被 hook 的程式起不來（CI run 43233800178），程式碼已加註解
- [x] CodeQL alert #1 `cpp/wrong-type-format-argument`（`LocaleEmulator.h` 的 `GetLePebSectionName`）dismiss 為 Won't fix，留到重啟 x64 時處理（見 §5「重啟 x64 前要先處理的事」）
- [x] CodeQL（C# + C/C++）與第一條自訂查詢 `le/core/kernelbase-nls-call`

## 3. 做得不錯、要維持的做法

1. **CI 就是驗證迴圈**：本機沒有 Windows 也能前進；每個結論都附 CI run 編號，查得到。
2. **smoke test 測的是真實行為**：預期值從系統 API 推導、不寫死；覆蓋 ntdll、kernelbase、檔案 API、user32、registry、gdi32、時區。
3. **實驗要有「確實生效」的證據**：時機實驗第一次全 PASS 時沒有直接採信，補了 `LE_EXPERIMENT` 標記證明延後路徑真的執行。之後的實驗也要這樣做。
4. **無 CRT 的建置可以重現**：標準 MSVC + Windows SDK + `.def` 就能建，不需要 WDK 或修改過的編譯器；靜態 kernel32 import 有自動檢查擋住。
5. **結構佈局雙重保護**：Core.Tests / LEProc.Tests 在執行期檢查，`Arch/LeArch.h` 用 `static_assert` 在編譯期檢查。
6. **決策有紀錄**：研究 → spec → 計畫 → 實驗 → 結果，每個決定都有理由與定案條件。
7. **小切片、`[Core]` 前綴**：方便 review，也方便日後拆開回饋上游。
8. **順手修掉隱藏問題**：CI 只看最後一個 msbuild 結束碼、`.gitignore` 吞掉 x64 的 `.def`、vcxproj 寫死輸出路徑。

## 4. 已做的取捨

| 取捨 | 得到 | 放棄 / 代價 | 何時重新檢討 |
|---|---|---|---|
| x64、D7 與 Phase 2 剩餘項目暫緩，只顧 x86 與自己玩的遊戲（2026-11-12） | 時間集中在實際影響遊戲的問題 | 64 位元遊戲不能用；Core 的程式碼現代化停在 C++17 | 要玩 64 位元遊戲時 |
| Core 維持 MSVC C++ + ml，不用 Rust 重寫（D1） | 不用重寫約 5 萬行；保留 SEH | 現代語言的安全性；ml 的技術債留著 | D7 成立後，第二段可以改用現代 C++ |
| 自寫 hook 引擎（Zydis），不用 Detours，也不沿用 Locale Remulator | 不依賴維護緩慢的上游；完全掌控 | 要自己寫、自己除錯 x64 hook | Phase 3 遇到重大困難時 |
| x64 第一版只做 NLS、LCID、時區（Q6） | 較快可用 | user32 / gdi32 要等 Phase 6 | Phase 4 完成後 |
| 不支援跨架構子程序（Q5） | 不需要 IPC | x86 程式啟動 x64 子程序時不會被模擬 | 有使用者需求時 |
| LEProc 維持 x86，另建 LEProc64（Q2） | x86 路徑完全不動 | 多一個執行檔要管 | — |
| `/Zc:alignedNew-` | C++17 立刻可用 | 暫時沒有符合標準的對齊配置 | A2b |
| Core 升 C++17 並保留非符合模式（`ConformanceMode=false` 等） | 不用動 ml.h | C++20 以後的語法還不能用 | A3 |
| Core 只有 Release 組態（方案的 Debug 組態也對應到 Core 的 Release，PR #6） | 設定簡單；VS 以 Debug 開方案時三個 Core 專案能正常載入與建置 | 沒有 Debug 版可以除錯 | 加診斷 log 時一起考慮 |
| 只靠 CI 驗證 | 不受本機環境限制 | 回饋較慢；只測一種 Windows 版本與一種系統地區 | 使用者主機可用時補測 |
| 時機實驗用編譯期開關 | 正式版完全不受影響 | 換時機要重新編譯 | — |

## 5. 現在不做，未來可能會後悔的事

依影響排序。

1. **Core 沒有診斷 log。** 遊戲沒效時會沒有線索，只能靠猜和反覆試。已列在 TODO「其次」的第一項。
2. **只在一種 Windows 版本上測過。** 鐵則中「NLS 表替換失效」就是在特定 build 上發現的；Windows 更新可能讓原本能玩的遊戲失效。至少讓 smoke test 輸出 OS build，並在使用者主機上跑過一次 smoke test 作為對照。
3. **系統 ACP 65001 只能在使用者主機重現。** CI 測不到，沒有自動化的回歸保護。改了 `LeRtl*` 或其他轉換相關的 hook 之後，請在使用者主機上跑一次 `Invoke-LocalProbe.ps1`（開著 UTF-8 beta）確認經 LE 仍是 0 項失敗。

### 重啟 x64 前要先處理的事

只在第 2 節「暫緩」的項目重新啟動時才需要：

1. **D7 要在寫 x64 hook 之前定案。** 否則 x64 hook 可能先寫在無 CRT 的第一段，之後又要搬到可以用 CRT 的第二段，同一批程式碼寫兩次。
2. **新模組不要直接依賴 ml。** HookEngine 與 x64 程式碼若一開始就到處用 ml 的 API，Phase 7 會越來越難。
3. **LEB 契約在 x64 上的變化。** 指標大小會變，C# / C++ / 兩個測試專案是手抄的定義。做 LEProc64 之前，至少要讓 Core.Tests 與 LEProc.Tests 以 x64 檢查 LEProc64 看到的佈局。
4. **LePeb 共享記憶體的名稱要跨架構一致。** `GetLePebSectionName`（`LocaleEmulator.h`）用 `%p` 格式化 `ULONG_PTR` 的 PID，x86 產生 8 位、x64 產生 16 位十六進位，x86 LoaderDll 建立的名稱與 x64 LocaleEmulator 找的名稱會不同（跨架構啟動子程序時就找不到 LEB）。改成固定寬度的格式（例如 `%08X` 搭配 `(ULONG)ProcessId`）（只有 `LocaleEmulator.h` 這一處，x86 與 x64 共用），改完跑 smoke test。CodeQL alert #1（`cpp/wrong-type-format-argument`）已先 dismiss 為 Won't fix。
