# Core Phase 2 計畫：C++ 標準升級 + HookEngine 介面

- Spec：`docs/superpowers/specs/2026-11-11-core-modularization-and-x64-design.md`（Phase 2）
- 前置：Phase 1（`2026-11-11-core-phase1-foundation.md`）已完成，CI run 42960443765 全綠。
- 驗證：本機沒有 Windows，每個切片都靠 CI（x86 + x64 建置、Core.Tests、smoke test 6/6）驗證。

Phase 2 分成兩部分。Part A 先升級 C++ 標準，讓後續寫的新模組可以用較新的語法；Part B 才是 spec 裡的 HookEngine 介面。

## Part A：C++ 標準升級

### 現況

| 專案 | 升級前 | 備註 |
|---|---|---|
| Core（LoaderDll、LocaleEmulator、Loader，共用 `Core.props`） | `stdcpp14` | `ConformanceMode=false`、`/Zc:templateScope-`、`/Zc:strictStrings-`、`/Zc:threadSafeInit-`；無 CRT |
| ShellExtension | `stdcpp17` | 一般 C++，有 CRT/STL |
| Core.Tests、ShellExtension.Tests | `stdcpp17` | 一般 C++ |
| LocaleTestApp | 未設定（MSVC 預設 C++14） | 一般 C++ |

MSBuild 的 C++23 值是 `stdcpp23`，對應 `/std:c++23preview`；MSVC Build Tools 14.52 起 `/std:c++23` 正式化，`/std:c++23preview` 保留為別名，因此專案檔不必再改。不用 `stdcpplatest`，因為它會隨工具鏈版本吃進 working draft 的功能，行為不固定。

### Task A1：非 Core 專案升到 C++23

- 檔案：`src/ShellExtension/ShellExtension.vcxproj`、`tests/Core.Tests/Core.Tests.vcxproj`、`tests/ShellExtension.Tests/ShellExtension.Tests.vcxproj` 的 `stdcpp17` 改成 `stdcpp23`；`tests/SmokeTest/LocaleTestApp/LocaleTestApp.vcxproj` 新增一個不帶條件的 `ItemDefinitionGroup`，明確設定 `stdcpp23`。
- 不改原始碼，只改標準；有錯誤才修，修法限於讓既有程式碼在新標準下維持原意。
- 驗收：CI 的 ShellExtension x86/x64、ShellExtension.Tests、Core.Tests x86/x64、LocaleTestApp 建置成功，測試與 smoke test 全過。
- commit：`build: ShellExtension 與 C++ 測試專案升級到 C++23`

### Task A2：Core 從 C++14 升到 C++17

- 檔案：`src/Core/Core.props` 的 `stdcpp14` 改成 `stdcpp17`。`ConformanceMode` 與 `/Zc:*-` 選項維持不變；放寬它們是另一個獨立切片，不在本 Task。
- 預期風險：C++17 移除動態例外規格與 `register`、把 `noexcept` 納入函式型別、新增 aligned `operator new`。Core 沒有 CRT，若連結時出現新的 `operator new/delete` 多載或其他 helper 符號未解析，要在 `Runtime/CoreRuntime.cpp` 或 `.def` 補，不能把 CRT 加回來（鐵則 3）。
- 驗收：CI 的 Core x86（含「不得靜態 import kernel32」檢查）與 x64 建置成功；Core.Tests 通過；smoke test 6/6，所有 `CHECK_*` 為 `OK`（鐵則 9）。
- commit：`[Core] build: Core 升級到 C++17`
- 結果（CI run 42961436478）：x86 的 LocaleEmulator 連結失敗，缺 `operator delete(void*, unsigned int, std::align_val_t)`。x86 的預設 new 對齊是 8，有 16-byte 對齊的型別被 `delete` 時，C++17 會改呼叫 aligned 版本（x64 預設對齊為 16，不受影響）。Core.props 加上 `/Zc:alignedNew-`，維持 C++14 的行為。同一次 run 也發現 CI 的 Win32 建置步驟只檢查最後一個 msbuild 的結束碼，中間專案失敗會被蓋掉，已改為每個 msbuild 後都檢查。

### 之後（不在本計畫內，視 A2 結果另外排）

- A3：Core 從 C++17 升到 C++20。C++20 的 `operator==` 改寫規則、`char8_t`、聚合初始化規則都可能讓 `ml.h` 出錯；錯誤量太大時改走「ml 留在舊標準、新模組用新標準」，前提是先在標頭層級隔開 ml（Phase 7 的方向）。
- A4：Core 從 C++20 升到 C++23。
- Core 在無 CRT 下只能用語言功能（`if consteval`、deducing `this`、concepts、`consteval` 等）；`std::expected`、`std::format`/`std::print`、容器等標準函式庫功能不能用，需要的話自己寫小型替代品（例如包裝 `NTSTATUS` 的 `Result` 型別）。

## Part B：HookEngine 介面（x86 行為不變）

待 Part A 完成、依 A2 結果決定新模組可用的標準後，再寫細部 Task。範圍依 spec Phase 2：抽出 HookEngine 介面，x86 實作包住現有 `HookPort`，行為不變。
