# Roadmap：待辦、取捨與風險

> **活文件**：每完成一項、做了新的取捨或發現新的風險，就更新這份文件（改勾選狀態或增刪項目，不必保留歷史；歷史在 git 與 `docs/superpowers/`）。
> **最後整理**：2026-11-11（Phase 1 完成、Phase 2 Part A / Part C 進行中）
> **相關文件**：[`AGENTS.md`](../AGENTS.md)、[Core 模組化與 x64 設計](superpowers/specs/2026-11-11-core-modularization-and-x64-design.md)、[Phase 2 計畫](superpowers/plans/2026-11-11-core-phase2-hook-engine.md)

## 1. 現況摘要

| 項目 | 狀態 |
|---|---|
| x86 Core | 正式使用；smoke test 6 個地區、每個地區 32 個 `CHECK_*` 加時區全綠 |
| x64 Core | 能編譯（`LoaderDll64.dll` / `LocaleEmulator64.dll`），不安裝 hook、不發佈 |
| C++ 標準 | Core C++17（加 `/Zc:alignedNew-`）；ShellExtension 與 C++ 測試 C++23 |
| D7 兩段式 Core | 時機實驗 E1、E2 在 smoke test 範圍內全 PASS；等實際遊戲驗證後定案 |
| 開發分支 | `claude/busy-wozniak-et7oxc`，尚未合回 main |

## 2. TODO

依建議順序排列。`(spec §x)` 指向設計文件的章節，`Task Xn` 指向 Phase 2 計畫。

### 近期：D7 與 Phase 2

- [ ] **實際遊戲測試 E2 版**：使用者在自己的 Windows 主機上編譯 `/p:LeExperimentTiming=2` 的 `LocaleEmulator.dll`，換掉 LE 目錄裡的 DLL 測幾個 32 位元遊戲，用 Process Explorer 確認環境變數 `LE_EXPERIMENT=E2:init-after-kernel32`。結果寫回 spec §4.5。
- [ ] **D7 定案**；成立的話，寫兩段式 Core 的實作計畫（第一段只負責在 kernel32 初始化後載入第二段，第二段可以用 CRT）。
- [ ] **A2b**：盤點需要超出預設對齊的型別（開 C4316，或暫時拿掉 `/Zc:alignedNew-` 看連結錯誤），評估改成正式實作對齊版 `operator new/delete`。
- [ ] **A3 / A4**：Core 升到 C++20、C++23（風險在 ml.h：`operator==` 改寫規則、`char8_t`、聚合初始化）。
- [ ] **Part B**：HookEngine 介面（`Le::Hook`），x86 實作包住現有 `Mp::PatchMemory`，行為不變。

### x64（Phase 3～7，spec §5）

- [ ] Phase 3：x64 hook 引擎（Zydis、near relay + `FF 25`、syscall stub 複製）、hook 單元測試、LocaleTestApp x64、遠端改寫 PEB 的實驗
- [ ] Phase 4：移植 `LeRtl*`、`NtQueryDefault*`、時區 hook；smoke test 擴充成 x86 + x64
- [ ] Phase 5：`TargetArchitectureDetector`（C#）、LEProc64、把 x64 產物組進 `Build/Release/`
- [ ] Phase 6：user32 / gdi32 hook、x64 → x64 子程序注入
- [ ] Phase 7：減少 ml 依賴（巨集、NT 宣告、程序 / 記憶體、PE、容器）
- [ ] 處理 x64 的大量截斷警告（`ULONG_PTR` ↔ `ULONG` 等）

### 測試與 CI

- [ ] smoke test 補上：`SendMessageA` 等 user32 訊息轉換、`EnumFontFamilies*`、進階 registry 轉向（`IsAdvancedRedirection`）與 UI 語言 API
- [ ] smoke test 輸出 OS 版本與 build 號碼，讓結果可以對照 Windows 版本
- [ ] LoaderDll / Loader 的 vcxproj 也改用 `$(OutDir)` / `$(IntDir)`（目前把 `.\Release\` 寫死）
- [x] CodeQL（C# + C/C++）與第一條自訂查詢 `le/core/kernelbase-nls-call`
- [ ] 處理 CodeQL `security-extended` 第一次掃描的 6 筆結果（皆在 Core，非 ml）：
  - 值得修：`cpp/alloca-in-loop` 兩筆（`HookPort.cpp` 在 syscall 過濾迴圈裡每次 `_alloca`；`LocaleEmulator.h` 的目錄列舉加倍緩衝區時 `_alloca`），堆疊會隨迭代增長
  - 型別小問題：`cpp/wrong-type-format-argument`（`LocaleEmulator.h` 以 `%p` 格式化 `ULONG_PTR` 的 PID；同架構內寬度一致，但未來若支援跨架構子程序，x86 與 x64 產生的共享記憶體名稱會不同）
  - 判斷為誤報，可在 GitHub 上 dismiss：`cpp/incorrect-string-type-conversion`（`Utility.cpp` 的 atom 轉型）、`cpp/suspicious-pointer-scaling` 兩筆（`NtdllHook.cpp` 刻意以元素為單位的指標運算）
- [ ] 更多自訂查詢：例如 `LocaleEmulator.dll` 的 hook 裡不准呼叫 user32 / gdi32（延遲載入可能在 kernel32 之前觸發）、x64 程式碼不准用 `__readfsdword`
- [ ] （可選）SonarQube Cloud：可維護性、重複率、技術債趨勢，適合追蹤 Phase 7 的進度
- [ ] （可選）實驗版 DLL 加進 CI artifact

### 其他

- [ ] Windows 11 右鍵選單第一層（`IExplorerCommand` + 有簽章的 sparse package）
- [ ] Core 的可選診斷 log（目前 `WriteLog` 不輸出任何東西）
- [ ] 開 PR，把 Phase 1 + Part A + Part C 合回 main

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
| Core 維持 MSVC C++ + ml，不用 Rust 重寫（D1） | 不用重寫約 5 萬行；保留 SEH | 現代語言的安全性；ml 的技術債留著 | D7 成立後，第二段可以改用現代 C++ |
| 自寫 hook 引擎（Zydis），不用 Detours，也不沿用 Locale Remulator | 不依賴維護緩慢的上游；完全掌控 | 要自己寫、自己除錯 x64 hook | Phase 3 遇到重大困難時 |
| x64 第一版只做 NLS、LCID、時區（Q6） | 較快可用 | user32 / gdi32 要等 Phase 6 | Phase 4 完成後 |
| 不支援跨架構子程序（Q5） | 不需要 IPC | x86 程式啟動 x64 子程序時不會被模擬 | 有使用者需求時 |
| LEProc 維持 x86，另建 LEProc64（Q2） | x86 路徑完全不動 | 多一個執行檔要管 | — |
| `/Zc:alignedNew-` | C++17 立刻可用 | 暫時沒有符合標準的對齊配置 | A2b |
| Core 升 C++17 並保留非符合模式（`ConformanceMode=false` 等） | 不用動 ml.h | C++20 以後的語法還不能用 | A3 |
| Core 只有 Release 組態 | 設定簡單 | 沒有 Debug 版可以除錯 | 加診斷 log 時一起考慮 |
| 只靠 CI 驗證 | 不受本機環境限制 | 回饋較慢；只測一種 Windows 版本與一種系統地區 | 使用者主機可用時補測 |
| 時機實驗用編譯期開關 | 正式版完全不受影響 | 換時機要重新編譯 | — |

## 5. 現在不做，未來可能會後悔的事

依影響排序。

1. **D7 要在寫 x64 hook 之前定案。** 否則 x64 hook 可能先寫在無 CRT 的第一段，之後又要搬到可以用 CRT 的第二段，同一批程式碼寫兩次。
2. **新模組不要直接依賴 ml。** HookEngine 與 x64 程式碼若一開始就到處用 ml 的 API，Phase 7 會越來越難。Part B 一開始就在新模組外面包一層薄介面。
3. **LEB 契約在 x64 上的變化。** 指標大小會變，C# / C++ / 兩個測試專案是手抄的定義。做 LEProc64 之前，至少要讓 Core.Tests 與 LEProc.Tests 以 x64 檢查 LEProc64 看到的佈局。
4. **Core 沒有診斷 log。** 使用者回報「某個遊戲沒效」時會沒有線索。建議早點加可選的 log（例如 ntdll 的 `DbgPrint`，用環境變數開關）。
5. **Windows 11 右鍵選單。** 要出現在第一層通常需要 `IExplorerCommand` 加有簽章的 sparse package，會牽涉安裝方式與簽章；越晚決定，現有的安裝流程越難改。
6. **長期分支太久沒合回 main。** 分支上已有數十個 commit，越晚合衝突越多、review 越難。
7. **只在一種 Windows 版本上測過。** 鐵則中「NLS 表替換失效」就是在特定 build 上發現的。至少讓 smoke test 輸出 OS build；有條件時多一種 Windows 版本。ARM64 Windows 以模擬執行 x86 程式，`KiFastSystemCall` 那套做法能否運作也值得早點確認。
