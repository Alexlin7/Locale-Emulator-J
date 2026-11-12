# src/Core/AGENTS.md

Core 是 LE 的原生核心：`LoaderDll` 建立目標程序並注入，`LocaleEmulator` 在目標程序內 hook Windows API，`Loader` 是獨立的命令列啟動器。源自 [Locale-Emulator-Core](https://github.com/xupefei/Locale-Emulator-Core)（commit `ae7160d`）。

跨模組鐵則見根目錄 [`AGENTS.md`](../../AGENTS.md)（尤其 LEB 契約、`[Core]` commit 前綴、改完要跑 smoke test）。建置方式與無 CRT 設計的完整說明見 [`README.md`](README.md)。

## 鐵則

1. **不使用 C runtime，靜態 import 只有 `ntdll.dll`**（LoaderDll 另有 kernel32 的 `CreateProcessInternalW`）。CRT 函式來自 ntdll 匯出（`ImportLibs/<x86|x64>/*.def`，修飾規則與 helper 依架構不同），執行期支援在 `Runtime/CoreRuntime.cpp`（x86 的 SEH handler 自行實作；x64 使用 ntdll 的 `__C_specific_handler`），共用設定在 `Core.props`。不要把 CRT 或 Delayimp.lib 加回來。
2. **`LocaleEmulator.dll` 絕不能靜態 import kernel32。** 它是在目標程序第一次呼叫 `LdrLoadDll`（ntdll 正要載入 kernel32）時被注入的，當下 kernel32 還不存在。要用 kernel32 的函式就在執行時查找（`LocaleEmulator/Hooks/User32Hook.cpp` 的 `KERNEL32_API`）；建置時有檢查（`Core.props` 的 `LeCheckNoStaticKernel32`），出現靜態 kernel32 import 會直接失敗。
3. **hook 只能依賴 ntdll。** hook 裡不能呼叫 kernelbase 的 `MultiByteToWideChar` 之類函式；code page 轉換一律用 ntdll 的 `RtlCustomCPToUnicodeN` / `RtlUnicodeToCustomCPN` 搭配 LE 自己載入的 NLS 表。CodeQL 自訂查詢 `le/core/kernelbase-nls-call` 會檢查 Core（排除 ml）直接呼叫或在執行時查找（`KERNEL32_API`、`KERNEL32_*` hash、依名稱查找）kernel32 / kernelbase 的 code page、地區、時區 API。確實需要時（例如 `Hooks/Kernel32Hook.cpp` 刻意操作 kernelbase 的地區快取），在同一行或上一行加 `// LE-ALLOW-KERNELBASE: <理由>`，review 時要看理由是否成立。
4. **新版 Windows 上換 ntdll NLS 表的做法已經失效**：`RtlInitNlsTables` / `RtlResetRtlTranslations` 是空函式（build 26300 實測）。ANSI/OEM 轉換靠 `LocaleEmulator/Hooks/NtdllHook.cpp` 的 `LeRtl*` hook，不要拿掉。
5. **x86 的 syscall hook（`HookPort.cpp`）靠比對 `KiFastSystemCall` 的 opcode 運作**，x64 沒有這個函式；x64 的 hook 引擎是另一套實作（設計見 `docs/superpowers/specs/2026-11-11-core-modularization-and-x64-design.md`）。不要試圖把 HookPort 移植到 x64。

## 結構

```
Core.props                 三個專案共用的建置設定（無 CRT、C++17、.def → import lib、kernel32 檢查、實驗開關）
ImportLibs/x86|x64/*.def   SDK import lib 缺少的 ntdll/kernel32 匯出
Runtime/CoreRuntime.cpp    取代原本的 MyLib.lib：delay-load helper、x86 SEH、編譯器支援符號
LoaderDll/                 LeCreateProcess / LeCreateProcess2
LocaleEmulator/
  LocaleEmulator.cpp       DllMain、LeGlobalData::Initialize（改寫 PEB → HookPort → ntdll hook → DLL 載入通知）
  Hooks/                   Ntdll / Kernel32 / User32 / Gdi32 的 hook
  HookPort.cpp             x86 syscall hook；x64 改編譯 HookPortX64Stub.cpp
  Arch/LeArch.h            TEB / PEB 位移常數；Arch/LayoutAsserts.cpp 在編譯期對照 ml.h
  ml.h / ml.cpp            上游的大型工具庫（約 4.9 萬行，LE 實際只用約 250 個符號）
Loader/                    命令列啟動器（只有 x86）
```

## 現況

| 項目 | 狀態 |
|---|---|
| x86 | 正式使用，smoke test 全綠 |
| x64 | 能編譯（`Release\|x64` 輸出 `LoaderDll64.dll` / `LocaleEmulator64.dll`），但不安裝任何 hook：`LeGlobalData::Initialize` 回傳 `STATUS_NOT_SUPPORTED`，`LeCreateProcess` 不建立程序；尚未組進 `Build/Release/` |
| C++ 標準 | C++17，`ConformanceMode=false`，加上 `/Zc:templateScope- /Zc:strictStrings- /Zc:threadSafeInit- /Zc:alignedNew-` |
| 組態 | 只有 Release；方案的 Debug 組態也對應到 Release（`LocaleEmulator.sln`） |

## 常見陷阱

- **沒有標準函式庫**：可以用語言功能（`constexpr`、lambda、template 等），不能用 `std::vector`、`std::string`、`std::format`、例外處理。需要的話用 ml 的對應物，或自己寫小型替代品。
- **連結時出現未解析的編譯器 helper 或 `operator new/delete` 多載**：要在 `Runtime/CoreRuntime.cpp` 或 `.def` 補，不能把 CRT 加回來。C++17 的對齊版 `operator new/delete` 目前以 `/Zc:alignedNew-` 關閉（x86 的預設對齊是 8，有 16-byte 對齊的型別被 `delete` 時會需要它）；改成正式實作是待辦（`docs/roadmap.md`）。
- **x86 與 x64 的差異**：修飾名不同（`/EXPORT` 要依架構寫，見 `LocaleEmulator.cpp`）、`.def` 各一份、TEB 走 `fs` / `gs`（`__readfsdword` 只能在 x86 用）、`GWL_*` 要改用 `GWLP_*`。新程式碼用 `#if defined(_M_IX86)` / `#elif defined(_M_X64)` 分段，不要假設指標是 4 bytes。
- **ml 的 x64 路徑從未在上游使用過**，有已知錯誤（例如 x64 的 `OpPush` 編碼已停用）。用到 ml 的 x64 功能前先確認。
- **vcxproj 的輸出路徑**：LocaleEmulator 的 Win32 組態已改用 `$(OutDir)` / `$(IntDir)`；LoaderDll、Loader 仍把 `.\Release\` 寫死，要另外建一份時 `/p:OutDir` 對它們無效。
- **`WriteLog` 目前不輸出任何東西**，Core 沒有診斷 log。
- **HookPort 的 filter 是靠堆疊位置拿參數的**：filter 宣告成 `HPARGS` 加上 syscall 本身的參數，但 `HpServiceDispatcherInternal` 只用兩個 FASTCALL 暫存器參數呼叫它，其餘參數由 filter 直接從堆疊頂端讀，也就是迴圈裡 `_alloca` 複製出來的那份。看起來沒被讀的 `LocalArguments` 不是死碼，`_alloca` 也必須留在迴圈內（FASTCALL 由被呼叫端清堆疊，每個 filter 都會彈掉自己那份）。曾經誤刪過，結果所有被 hook 的程式都起不來（CI run 43233800178）。

## 時機實驗（D7）

`LE_EXPERIMENT_TIMING`（`LocaleEmulator.cpp`）是兩段式 Core 提案的驗證開關，預設 0，正式產物不受影響。`msbuild ... /p:LeExperimentTiming=1|2` 會定義它：

- `1`（E1）：PEB 的 NLS 指標延後到 kernel32 初始化完成後才寫入
- `2`（E2）：整個 `LeGlobalData::Initialize` 延後到 kernel32 初始化完成後

實驗版會設定環境變數 `LE_EXPERIMENT`，證明延後路徑確實執行。CI 每次都會建實驗版並跑 smoke test（僅供參考）。結果與解讀見 spec §4.5。

## 建置與測試

```powershell
msbuild src/Core/LoaderDll/LoaderDll.vcxproj /p:Configuration=Release /p:Platform=Win32
msbuild src/Core/LocaleEmulator/LocaleEmulator.vcxproj /p:Configuration=Release /p:Platform=Win32
msbuild src/Core/LocaleEmulator/LocaleEmulator.vcxproj /p:Configuration=Release /p:Platform=x64
```

- `tests/Core.Tests/`：LEB、`RTL_TIME_ZONE_INFORMATION`、`REGISTRY_REDIRECTION_ENTRY64` 的大小與欄位位移（x86 + x64）。它手抄了結構定義，不 include `LocaleEmulator.h`（會拉進整個 ml.h），改 LEB 時要一起改。
- smoke test：見 [`tests/SmokeTest/AGENTS.md`](../../tests/SmokeTest/AGENTS.md)。**改 Core 之後一定要跑。**

## 相關文件

- `docs/superpowers/specs/2026-11-11-core-modularization-and-x64-design.md`：模組化與 x64 設計（D1～D7、Phase 0～7）
- `docs/superpowers/specs/2026-11-11-x64-core-migration-research.md`：x64 技術研究
- `docs/superpowers/specs/2026-11-11-ml-usage-inventory.md`：ml 使用盤點
- `docs/superpowers/plans/2026-11-11-core-phase1-foundation.md`、`2026-11-11-core-phase2-hook-engine.md`：實作計畫
