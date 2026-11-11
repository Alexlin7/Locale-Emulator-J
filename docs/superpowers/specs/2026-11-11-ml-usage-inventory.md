# ml（MyLibrary）使用盤點

> 狀態：盤點紀錄（只讀程式碼，未修改任何檔案）
> 日期：2026-11-11
> 目的：找出 LE 自己的程式碼實際依賴 ml 的哪些部分，作為後續重構（逐步以自有模組取代 ml）與 x64 化的清單。

## 1. 規模總覽

| 項目 | 數量 |
|---|---|
| `ml.h` + `ml.cpp` | 49,364 行（34,801 + 14,563） |
| ml 定義的符號（巨集、型別、函式、列舉，regex 粗估） | 約 12,500 個 |
| 其中 API 名稱雜湊巨集（`NTDLL_xxx`、`KERNEL32_xxx`……） | 4,923 個 |
| LE 自己的程式碼（不含 ml、`CoreRuntime.cpp`） | 約 11,100 行 |
| ├ 其中 `Hooks/MessageTable.h`（純資料表） | 1,256 行 |
| └ 其中 `HookPort.cpp` / `.h` 裡只在 `ML_KERNEL_MODE` 編譯的死碼 | **832 行**（791 + 41） |
| **LE 實際用到的 ml 符號（分類整理後）** | **約 250 個** |
| LE 呼叫的 NT 原生函式（`Nt*`/`Rtl*`/`Ldr*`/`Zw*`，宣告來自 ml） | 63 個、121 處呼叫 |
| LE 用到的 API 雜湊巨集 | 22 個（ml 定義了 4,923 個） |

**結論：ml 有 4.9 萬行，但 LE 只用了大約 250 個符號，約佔 2%。** 重構時需要關心的就是這 2%。

### 引入方式

| 檔案 | 方式 |
|---|---|
| `LocaleEmulator/stdafx.h`、`LocaleEmulator.h`、`HandleTable.h`、`SectionProtector.h` | `#include "ml.h"` |
| `HookPort.h` | `#include "MyLibrary.h"`（內容只有一行 `#include <ml.h>`） |
| `LocaleEmulator.vcxproj` | `ml.cpp` 是獨立的編譯單元 |
| `LoaderDll/LoaderDll.cpp` | **`#include "ml.cpp"`**，整包編進來 |
| `Loader/Loader.cpp` | `#include "../LoaderDll/LoaderDll.cpp"`，間接也整包編進來 |

---

## 2. 分類明細

數字是「LE 程式碼中出現的次數」。

### A. Hook / 記憶體 patch —— **x64 化的阻塞點**，共 164 處

| 符號 | 次數 | 出現位置 |
|---|---:|---|
| `Mp::RestoreMemory` | 21 | User32Hook 12、Gdi32Hook 8、NtdllHook 1 |
| `LeHookFromEAT`（LE 巨集，底層是 `Mp::FunctionJumpVa`） | 19 | Gdi32Hook 9、User32Hook 9 |
| `Mp::FunctionJumpVa` | 16 | NtdllHook 13、LocaleEmulator.h 3 |
| `ProtectVirtualMemory` | 14 | HookPort 13 |
| `INLINE_ASM` / `NAKED` | 13 / 7 | 全在 HookPort |
| `GetCallDestination` | 12 | User32Hook 7、Kernel32Hook 2 等 |
| `WalkOpCodeT` / `WalkOpCodeM` | 11 / 11 | User32Hook 5、Kernel32Hook 3 等 |
| `GetOpCodeSize` / `CopyOneOpCode` | 6 / 3 | HookPort、Utility |
| `Mp::PatchMemory` / `Mp::PATCH_MEMORY_DATA` | 5 / 5 | 各 hook 檔各一次 |
| `LeFunctionJump` / `LeFunctionCall` | 5 / 1 | User32Hook、Gdi32Hook |
| `Mp::FunctionCallVa` | 3 | HookPort、NtdllHook（`LdrInitNtContinue`） |
| `WriteProtectMemory` | 3 | NtdllHook（子程序注入） |
| `SearchPattern` 系列、`PUSHAD_REGISTER`、`ASM_DUMMY_AUTO` | 少量 | User32Hook、HookPort |

**觀察：**
- 所有 hook 都經過 `Mp::PatchMemory` 這**一個入口**（每個 hook 檔各呼叫一次，傳入一個 `PATCH_MEMORY_DATA` 陣列）。這是很好的切點：只要讓 `HookEngine` 提供相同形狀的 API，hook 檔案本身幾乎不用改。
- `WalkOpCodeT` + `GetCallDestination` 用在「掃描某個函式內部，找出它呼叫的未匯出函式」（例如 `SearchLdrInitNtContinue`）。這類**特徵碼搜尋高度依賴 x86 指令格式**，x64 需要逐一重寫。

### B. PE / Loader 工具，共 72 處

| 符號 | 次數 | 用途 |
|---|---:|---|
| `LookupExportTable` | 17 | 依雜湊值在 EAT 中找函式 |
| `FindLdrModuleByHandle` / `ByName` | 10 / 4 | 走訪 PEB 的模組串列 |
| `IATLookupRoutineByHash` | 4 | 找 IAT 項目 |
| `Nt_GetProcAddress` / `Nt_GetModuleHandle` | 4 / 2 | 不經 kernel32 的 GetProcAddress |
| `LoadPeImage` / `UnloadPeImage` / `RelocPeImage` | 3 / 4 / 1 | 手動映射（子程序注入、讀取 NLS） |
| `WalkRelocTableT` | 3 | 走訪重定位表 |
| `GetKernel32Ldr`、`GetNtdllLdrModule`、`GetNtdllHandle` 等 | 少量 | |
| `CreateProcessWithDll` + `CPWD_BEFORE_KERNEL32` | 1 | **LoaderDll 的注入核心** |
| API 雜湊巨集 | 22 個 | 見下方清單 |

使用中的雜湊巨集：`NTDLL_LdrLoadDll`、`NTDLL_NtContinue`、`NTDLL_NtCancelTimer`、`NTDLL_KiFastSystemCall`、`NTDLL_RtlQueryInformationActiveActivationContext`、`KERNEL32_GetUserDefaultLCID`、`KERNEL32_LCIDToLocaleName`、`KERNEL32_GetNLSVersionEx`、`KERNEL32_GlobalLock` / `GlobalUnlock` / `GlobalFree`、`USER32_CreateWindowExA`、`USER32_DestroyWindow`、`USER32_SendMessageA`、`USER32_SendNotifyMessageW`、`USER32_SetWindowLongA`、`USER32_NotifyWinEvent`、`GDI32_CreateFontIndirectExW`、`GDI32_EnumFontFamiliesExW`、`GDI32_SelectObject`、`NTOSKRNL_KeServiceDescriptorTable`（kernel mode 死碼）。另有 LE 自訂的 `KERNEL32_API`。

**觀察：** 這組工具是「不依賴 kernel32 找函式」的基礎設施，鐵則 4、5 依賴它成立。邏輯與架構無關（x64 只是 PE 結構換成 64 位元版），**可以整組搬到自有的 `LePe` 模組**。雜湊巨集只需要保留 22 個。

### C. 程序 / 記憶體 / 檔案，共 50 處

| 符號 | 次數 |
|---|---:|
| `CurrentPeb` | 11 |
| `ML_PROCESS_INFORMATION` | 4 |
| `Mm::FreeVirtualMemory` / `Mm::AllocVirtualMemory` / `AllocVirtualMemory(Ex)` | 4 / 1 / 3 |
| `Ps::IsWow64Process` | 3 |
| `ReadMemory` / `WriteMemory` | 3 / 2 |
| `CurrentTeb`、`CurrentPid`、`CurrentTid` | 各 2 |
| `NtFileDisk`、`NtFileMemory` | 各 2 |
| `Ps::CreateProcess`、`Ps::Sleep`、`Ps::ExitProcess`、`PidToHandleEx`、`ProcessIdToHandle`、`QuerySystemProcesses`、`GetSessionId`、`Io::QueryDosPathFromNtDeviceName`、`Nt_GetFileAttributes` | 各 1 |

**觀察：** 幾乎都是 `Nt*` 的薄包裝，取代成本低。`CurrentPeb` / `CurrentTeb` 的 x64 實作經複查是正確的（`ReadFsPtr` 在 x64 實際讀 GS），取代時只需保留行為。

### D. Registry，共 8 處

`Reg::OpenKey` 2、`Reg::CloseKeyHandle` 4、`GetKeyValue` 2。全部在 `LocaleEmulator.cpp`。

### E. 字串 / 編碼，共 92 處

| 符號 | 次數 | 備註 |
|---|---:|---|
| `PLARGE_UNICODE_STRING` / `LARGE_UNICODE_STRING` | 32 / 3 | win32k 的 `LARGE_STRING` 結構，**只是型別宣告** |
| `USTR64` / `USTR` / `PUSTR` | 12 / 4 / 1 | 建立 `UNICODE_STRING` 常數的巨集 |
| `ml::String` | 9 | ml 的字串類別 |
| `StrLengthW` / `StrLengthA` | 9 / 4 | 等同 `wcslen` / `strlen` |
| `UnicodeToAnsi` / `AnsiToUnicode` | 5 / 4 | User32Hook |
| `StringUpperW`、`MatchExpression`、`CONST_STRLEN`、`CONST_STRSIZE` | 少量 | |
| `findnamew`、`getargsW`、`ReleaseArgv` | 各 1 | 只在 `Loader.cpp`（測試用小工具） |

### F. 容器 / 記憶體配置，共 44 處

| 符號 | 次數 |
|---|---:|
| `AllocStack` | 10 |
| `AllocateMemoryP` / `FreeMemoryP` / `ReAllocateMemoryP` | 8 / 7 / 1 |
| `THREAD_LOCAL_BUFFER` | 7 |
| `SafeDeleteT` | 3 |
| `ml::MlInitialize` / `ml::MlUnInitialize` | 2 / 1 |
| `ml::GrowableArray`、`ml::HashTableT`、`FixedMemoryBlock`、`MemoryAllocator`、`ML_OVERLOAD_NEW` | 各 1 |

**觀察：** 數量不多，但 `GrowableArray`、`HashTableT`、`THREAD_LOCAL_BUFFER` 是 template 類別，**牽連到 ml 內部的配置器與初始化**（`MlInitialize`）。取代時要先寫好一個以 `RtlAllocateHeap` 為基礎的小配置器。這一組是除了 A 以外最需要小心的。

### G. 字型（TrueType name table），共 17 處

`Gdi::TT_*` 常數、`PTT_NAME_TABLE_HEADER`、`PTT_NAME_RECORD`、`Bswap`。全部在 `Gdi32Hook.cpp`，用於讀取字型檔的名稱表。搬成一個小 header 即可。

### H. 通用巨集，共 385 處（數量最多，但最簡單）

| 巨集 | 次數 | | 巨集 | 次數 |
|---|---:|---|---|---:|
| `PtrAdd` | 87 | | `ForceInline` | 12 |
| `API_POINTER` | 37 | | `ML_MAX` | 12 |
| `NT_FAILED` | 31 | | `FOR_EACH` | 11 |
| `FAIL_RETURN` | 26 | | `TAG4` | 9 |
| `LOOP_ONCE` | 25 | | `ROUND_UP` / `ML_MIN` | 8 / 8 |
| `countof` | 20 | | `CopyStruct` / `CLEAR_FLAG` | 7 / 7 |
| `PtrOffset` | 19 | | `EXTC` | 7 |
| `FLAG_ON` | 12 | | `SEH_TRY` / `SEH_EXCEPT` | 5 / 5 |

其餘：`PtrSub`、`PtrAnd`、`LOOP_FOREVER`、`FAIL_BREAK`、`FAIL_CONTINUE`、`FOR_EACH_VEC`、`FOR_EACH_ARRAY`、`FLAG_OFF`、`SET_FLAG`、`ROUND_DOWN`、`TAG2`、`SCOPE_EXIT`、`NoInline`、`IF_EXIST`、`IF_NOT_EXIST`、`IN_RANGE`、`IN_RANGEEX`、`FIELD_BASE`、`NO_BREAK`。

**觀察：** 共約 40 個巨集，全部是一兩行的定義。**不需要改寫使用的地方**，把定義複製到自有的 `LeMacros.h` 即可。`SCOPE_EXIT` 與 `API_POINTER` 稍微複雜（用到 template / 型別推導），複製時要一起帶走相依的定義。

### I. NT 原生 API 宣告：63 個函式、121 處呼叫

Windows SDK 沒有宣告大部分的 `Nt*` / `Rtl*` / `Ldr*` 函式與相關結構，目前全靠 ml 提供。呼叫次數最多的：

`NtClose` 15、`RtlFreeUnicodeString` 12、`RtlCreateUnicodeString` 5、`RtlInitEmptyString` 5、`RtlUnicodeToMultiByteN` 5、`RtlInitCodePageTable` 3、`RtlEqualUnicodeString` 3，其餘 56 個各 1～2 次。

連帶用到的結構：`PEB_BASE`、`TEB_ACTIVE_FRAME`、`LDR_MODULE`、`PS_ATTRIBUTE` / `PS_ATTRIBUTE_LIST` / `PS_CREATE_INFO`、`KEY_VALUE_*_INFORMATION`、`CPTABLEINFO`、`NLSTABLEINFO`、`SYSTEM_BASIC_INFORMATION`、`MEMORY_MAPPED_FILENAME_INFORMATION2`、`RTL_USER_PROCESS_PARAMETERS` 等。

**取代選項：**
1. 自己寫一份 `LeNative.h`，只宣告這 63 個函式與相關結構（建議；範圍明確、可控）
2. 改用 phnt（System Informer 維護的 NT 標頭集合）；完整但龐大，需先確認授權與 LGPL 相容

### J. 除錯工具，共 6 處

`ExceptionBox` 2、`PrintConsoleW` 2、`CreateMiniDump` 1、`DebugBreakPoint` 1。大多在已註解或除錯用的路徑上，可以直接移除或換成 `WriteLog`。

---

## 3. 各檔案對 ml 的依賴程度

| 檔案 | 行數 | 主要依賴 |
|---|---:|---|
| `HookPort.cpp` | 2,479（其中 791 行為 kernel mode 死碼） | A（大量 inline asm、opcode 解析）、C |
| `User32Hook.cpp` | 1,610 | A、B、E（`LARGE_UNICODE_STRING`、ANSI/Unicode 轉換） |
| `NtdllHook.cpp` | 1,166 | A、B（子程序注入）、C |
| `Gdi32Hook.cpp` | 895 | A、G |
| `LocaleEmulator.h` | 893 | F（容器）、H（`API_POINTER` 28 處） |
| `LocaleEmulator.cpp` | 674 | C、D、F、H |
| `LoaderDll.cpp` | 245 | B（`CreateProcessWithDll`）、H |
| `Kernel32Hook.cpp` | 251 | A（`WalkOpCodeT`）、B |
| `Utility.cpp` / `.h` | 377 | E、F |
| `HandleTable.*`、`SectionProtector.h` | 485 | F、H |

---

## 4. 建議的取代順序

| 順序 | 分類 | 做法 | 難度 | 與 x64 的關係 |
|---|---|---|---|---|
| 0 | HookPort 的 kernel mode 死碼 | 直接刪除 832 行 | 極低 | 減少要閱讀與移植的量 |
| 1 | H 巨集 | 複製到 `LeMacros.h`，使用處不變 | 低 | 無 |
| 2 | I NT 宣告 | `LeNative.h`，加上 x86/x64 的 `static_assert` | 低 | **x64 必要**（結構佈局） |
| 3 | C 程序 / 記憶體 | 改成直接呼叫 `Nt*`；`CurrentTeb` / `CurrentPeb` 改用 `LeArch.h` | 低 | **x64 必要** |
| 4 | B PE / Loader | `LePe` 模組，支援 PE32 / PE32+ | 中 | **x64 必要** |
| 5 | A Hook | `IHookEngine`，以 `PatchMemory` 為切點 | **高** | **x64 核心工作** |
| 6 | F 容器 | 以 `RtlAllocateHeap` 為基礎的小配置器 + 簡化版陣列 / 雜湊表 | 中 | 無 |
| 7 | D、E、G、J | 小範圍各自處理 | 低 | 無 |

做到第 5 步時，LocaleEmulator.dll 對 ml 剩下的依賴就只有容器和少量字串工具，屆時再決定是否完全移除 ml。

每一步都必須讓現有 x86 smoke test 維持全綠，修改 `src/Core/` 的 commit 一律加 `[Core]` 前綴。

---

## 5. 盤點方法與限制

- 以 regex 解析 `ml.h` / `ml.cpp` 的定義（`#define`、`typedef`、`struct`/`class`、函式宣告、列舉），再比對 LE 程式碼中出現的識別字。**不是編譯器層級的分析。**
- 已排除：LE 自己定義的符號、Windows SDK 的基本型別（`PVOID`、`HANDLE` 等）、被誤判成型別的變數名稱。
- 類別的成員函式（例如 `ml::String` 的 `Format`）沒有分開列出，歸在所屬類別下。
- 數字是出現次數，不是精確的呼叫次數；巨集內部展開的間接使用沒有計入。
- **最準確的驗證方法**：在 Windows 上把某一類 ml 定義移出，看編譯器回報哪些錯誤。這應該在實際取代每個分類時順便進行。
