# Core x64 化與工具鏈選型：技術研究報告

> 狀態：研究稿（尚未實作、尚未在 Windows 上驗證）
> 日期：2026-11-11
> 範圍：`src/Core/`（LoaderDll、LocaleEmulator.dll、Runtime、ImportLibs、Core.props）
>
> **本文提議的方向與 AGENTS.md 鐵則 1（「LEProc 與 Core 必須維持 x86」）衝突。** 若決定推進 x64，需先修改鐵則並同步更新 README。在那之前，本文只是研究紀錄。

## 0. 閱讀前須知：可信度標記

本文所有結論都依據本 repo 的實際程式碼。涉及 Windows 未公開行為的部分，以下列標記區分可信度：

| 標記 | 意義 |
|---|---|
| **[確定]** | 公開文件（MS Docs / ABI 規格）或本 repo 程式碼可直接證實 |
| **[實測]** | 本專案曾在實機上量測過（例如 build 26300 的 no-op stub） |
| **[推論]** | 依逆向社群的共識或結構推導，可信度高，但需要在 x64 實機上驗證 |
| **[待驗證]** | 假說；必須先用 LocaleTestApp 的 `CHECK_*` 證實，才能作為設計依據 |

### 現況盤點（直接影響 x64 的程式碼事實）

| 位置 | 現況 | x64 影響 |
|---|---|---|
| `HookPort.cpp:372-410` | syscall hook 靠比對 `KiFastSystemCall` 的 opcode，一個點攔截全部 syscall | **x64 沒有 `KiFastSystemCall`**，每個 `Nt*` stub 自己執行 `syscall`，整個 HookPort 架構無法移植 |
| `HookPort.cpp` | 多處 `INLINE_ASM`（`__asm`） | MSVC x64 不支援，需改寫 |
| `Runtime/CoreRuntime.cpp` | `_except_handler3/4`、`_alloca_probe_16` 都用 naked `__asm` | 只適用 x86；x64 需要另一套做法（見 §4.2） |
| `ImportLibs/*.def` | `Name@N EXPORTAS Name`、`_alldiv`、`_ftol*`、`_except_handler4_common` | x64 沒有修飾名，這些 helper 也不存在 |
| `Core.props:96` | `lib.exe /machine:x86` 寫死 | 需依平台切換 |
| `ml.h:22186` `ReadFsPtr()` | 名稱寫 Fs，但 `ML_AMD64` 分支實際使用 `__readgsqword` | 正確，只是名稱有誤導性（初版報告誤判為錯誤，已更正） |
| `ml.cpp:9220` `Mp` 的 x64 `OpPush` 編碼 | `push imm32` 後用 `C7 04 24`（`mov [rsp], imm32`），且 `Buffer += POINTER_SIZE` 只寫入 4 bytes | **編碼錯誤**：高 32 位元會遺失、位移也錯；另外 push/ret 跳轉和 CET 不相容（§3.2） |
| `ml.cpp:8940` | `SIZE_OF_JUMP_CODE` 只為 x86 定義（5） | x64 尚未實作 |
| `NtdllHook.cpp:313-316` | 注入子程序時，用 5-byte `E9` 把 `LdrLoadDll` 跳到 shadow image | 跨程序 ±2GB 不保證成立 |
| `LoaderDll.cpp:2-3` | 合併成單一 ERW section `.Asuna` | x64 上仍可行，但 ACG / CET 程序會受影響（§5 Pitfalls） |

**結論：** ml.h / ml.cpp 雖然到處有 `ML_AMD64` 分支，但這些分支**從未在這個專案中建置或測試過**，而且已經發現明確的錯誤（`Mp` 的 x64 push/ret 編碼）。x64 化不能假設「ml 已經支援 x64」，必須把每條 x64 路徑當成新程式碼來寫、來測。

---

## 1. 語言與工具鏈選型

### 1.1 `LocaleEmulator.dll` 改用 Rust `no_std` 的實現阻力

`LocaleEmulator.dll` 的執行環境有三個限制：被注入時 kernel32 尚未載入、只能依賴 ntdll、程序只有一條執行緒且 loader lock 被持有。以下逐項評估 Rust 在這個環境下的阻力。

| 面向 | Rust `no_std` 的狀況 | 阻力 |
|---|---|---|
| **Panic runtime** | 必須提供 `#[panic_handler]`；`panic = "abort"` 可以省掉 `eh_personality`。但 `core::fmt` 的格式化程式碼會被 panic 路徑拉進來（`index out of bounds` 這類訊息），體積大約 +20～60 KB。要真正去除需要 nightly 的 `-Zbuild-std=core -Zbuild-std-features=panic_immediate_abort`。handler 只能呼叫 `NtTerminateProcess` 或 `RtlRaiseStatus` | 中 |
| **Stack probing** | x86_64-pc-windows-msvc 的大型 stack frame（超過 4 KB）會產生 `__chkstk` 呼叫。這個符號一般由 MSVC CRT 或 `compiler_builtins` 提供，必須確認 link 時解析到 **ntdll 的 `__chkstk` 匯出**，而不是 CRT [推論] | 中 |
| **memcpy / memset 等** | `core` 會發出 `memcpy`/`memset`/`memcmp` 呼叫。可以用 `compiler_builtins` 的 `mem` feature 自帶實作，或 link 到 ntdll 的匯出（和現在的 .def 做法一致） | 低 |
| **Symbol decorator** | x86 上 `extern "stdcall"` 會產生 `_Name@N`。Rust 1.71 起穩定的 `#[link(name = "ntdll", kind = "raw-dylib")]` 搭配 `import_name_type = "undecorated"` **可以完全取代 .def + lib.exe 的流程**，這反而是 Rust 的優勢 [確定] | 低 |
| **PE section 配置** | 自訂 entry 需要 `#![no_main]` + `/ENTRY:` + `/NODEFAULTLIB`（在 build.rs 用 `cargo:rustc-link-arg`）。`#[link_section]` 可以放程式碼或資料，但 `/MERGE`、`/SECTION:ERW` 這類設定只能透過 linker 參數控制，彈性不如 `#pragma comment(linker)` | 中 |
| **SEH** | Rust 沒有 `__try/__except`。目前 hook 裡用 SEH 保護「讀取可能無效的使用者指標」，在 Rust 裡只能先用 `NtQueryVirtualMemory` 檢查，或寫 C shim 包裝 | **高** |
| **Inline hook / 裸函式** | x86 的 `naked` 函式在 Rust 1.88 才穩定（`#[unsafe(naked)]`），x64 可以用 `core::arch::naked_asm!` / `global_asm!` 撰寫 thunk，能力上不輸 MASM | 低 |
| **既有程式碼** | `ml.h` + `ml.cpp` 約 5 萬行，hook 本體（NtdllHook、User32Hook、Gdi32Hook、HookPort）約 7,000 行，大量依賴 ml 的巨集與型別。改寫等於**重寫整個 Core**，而且 smoke test 是唯一能驗證 hook 行為的手段 | **極高** |

**判斷：** 技術上可行，但沒有任何一項是 Rust 獨有的好處足以抵銷重寫成本。Hook DLL 的風險集中在「和未公開 Windows 行為互動」，換語言無法降低這類風險。**`LocaleEmulator.dll` 建議沿用 MSVC C++。**

### 1.2 分層：「Loader 用 Rust + Hook DLL 用 C++」

先釐清 LoaderDll 實際做的事（`LoaderDll.cpp`）：

1. 以 `CREATE_SUSPENDED` 建立程序（ml 的 `CreateProcessWithDll(CPWD_BEFORE_KERNEL32, ...)`，背後是 `CreateProcessInternalW`）
2. 找出目標程序第一次呼叫 `LdrLoadDll` 的位置，備份並 patch，讓目標程序把 `LocaleEmulator.dll` 載入成第一個 DLL
3. 建立具名共享記憶體 `LePeb`，寫入 `LEB` 與 registry 轉向項目
4. resume

**這些步驟都在 LEProc 程序裡執行，不在目標程序裡。** 所以 Loader 端沒有「kernel32 不存在」的限制，Rust 在這裡的阻力小很多：

- 可以使用 `std`（但會連結 vcruntime，違反鐵則 3「Core 不使用 CRT」；需要決定 Loader 是否豁免）
- 可以用 `windows-sys` crate 取得 `NtWriteVirtualMemory`、`NtQueryInformationProcess` 等宣告
- 跨程序寫入、PE 解析、x86/x64 雙目標判斷，用 Rust 寫比 ml 巨集更安全

**但有兩個陷阱：**

1. **`LEPEB` / `LEB` 的佈局契約**：這兩個結構由 C# 的 `LoaderWrapper` 產生、在 C++ 端讀取，`Core.Tests` 驗證的正是它們的佈局。若 Loader 改用 Rust，就變成**三種語言共享同一份結構**（C#、Rust、C++）。必須以 C++ header 為單一真相源，用 `bindgen` 產生 Rust 定義，並把 `Core.Tests` 擴充成同時驗證 Rust 端（`static_assert` ↔ `const _: () = assert!(size_of::<LEB>() == N);`）。
2. **子程序注入的路徑仍在 C++**：`NtdllHook.cpp:258` 的 `InjectSelfToChildProcess` 在**目標程序內**執行，負責把 LE 傳給子程序。也就是說，「把 DLL 注入新程序」這套邏輯本來就必須在 C++ 中存在一份；Loader 改用 Rust 會變成**兩套注入實作**並存，維護成本上升。

**建議的界面（若採用）：**

```rust
// LoaderDll（Rust cdylib），匯出名稱與 C# P/Invoke 現況一致
#[no_mangle]
pub unsafe extern "system" fn LeCreateProcess(
    leb: *const Leb,                 // bindgen 由 LoaderDll.h 產生
    application_name: *const u16,
    command_line: *mut u16,
    current_directory: *const u16,
    creation_flags: u32,
    startup_info: *const STARTUPINFOW,
    process_information: *mut PROCESS_INFORMATION,
    process_attributes: *const SECURITY_ATTRIBUTES,
    thread_attributes: *const SECURITY_ATTRIBUTES,
    environment: *const c_void,
    token: HANDLE,
) -> NTSTATUS { /* ... */ }
```

**判斷：** 可行，但收益有限。建議**先不拆語言**，等 x64 C++ 版完成、smoke test 綠燈後，再評估 Loader 是否值得獨立改寫。

### 1.3 x64 化帶來的架構問題（與語言無關）

一個 x86 程序無法直接對 x64 程序做 inline patch。WoW64 下雖然有未公開的 `NtWow64WriteVirtualMemory64`，但無法在目標程序執行 x64 程式碼。因此：

```
LEProc（判斷目標 exe 的 PE Machine）
  ├─ IMAGE_FILE_MACHINE_I386  → LoaderDll.dll（x86）  → LocaleEmulator.dll（x86）   ← 現況
  └─ IMAGE_FILE_MACHINE_AMD64 → LEProc64.exe / LoaderDll64.dll（x64） → LocaleEmulator64.dll（x64）
```

LEProc 是 x86（鐵則 1），不能直接載入 x64 的 LoaderDll。可選方案：

- **A（建議）**：新增一個極小的 x64 啟動器 `LEProc64.exe`（.NET 10 x64 或 C++）。LEProc 判斷目標是 x64 時，把參數轉交給它。
- B：LEProc 改成 AnyCPU、以 64 位元執行，再依目標架構載入對應的 LoaderDll。改動面大，而且會牽動既有的 `LEProc.Tests --arch x86`。

**子程序跨架構**：x64 程序啟動 x86 子程序（或反過來）時，`InjectSelfToChildProcess` 目前在 `Wow64 && !IsWow64Process(Process)` 時直接放棄（`NtdllHook.cpp:270`）。x64 版需要決定：同樣放棄（簡單，先這樣做），還是交由對應架構的 Loader 處理（需要 IPC）。

### 1.4 技術架構決策表

| 模組 | 語言 / 工具鏈 | 平台 | 理由 |
|---|---|---|---|
| LocaleEmulator.dll（x86） | MSVC C++ v145, No-CRT | x86 | 現況，不動 |
| LocaleEmulator64.dll | MSVC C++ v145, No-CRT + **MASM（ml64）** | x64 | 沿用 hook 本體；x64 asm 一律放在 .asm |
| x64 Hook 引擎 | C++ 新寫（取代 HookPort 的 x64 路徑），長度解碼用 **Zydis（`ZYAN_NO_LIBC`，只用 decoder）** | x64 | HookPort 無法移植；ml 的 x64 Mp 有錯誤 |
| LoaderDll（x86） | MSVC C++ | x86 | 現況 |
| LoaderDll64 | MSVC C++（第一階段）；Rust 為後續選項 | x64 | 與 x86 共用原始碼，降低雙實作風險 |
| LEProc | .NET 10 | x86 | 鐵則 1；新增「依目標 Machine 分派」邏輯 |
| LEProc64（新） | .NET 10 或 C++ | x64 | 極薄的轉交層 |
| Import libs | `.def` + `lib.exe /machine:x64`，**x86/x64 分開兩份 .def** | 兩者 | 修飾規則不同，不要硬塞進同一份 |
| 結構契約 | C++ header 為真相源；Core.Tests 新增 x64 組態 | 兩者 | `LEB` 內的指標欄位在 x64 會改變大小 |

---

## 2. x64 記憶體佈局與 NLS

### 2.1 TEB / PEB 定址 [確定]

| | x86（含 WoW64 的 32 位元側） | x64 |
|---|---|---|
| Segment | `FS` | `GS` |
| `TEB->NtTib.Self` | `fs:[0x18]` | `gs:[0x30]` |
| `TEB->ProcessEnvironmentBlock` | `fs:[0x30]` | `gs:[0x60]` |
| Intrinsic | `__readfsdword(off)` | `__readgsqword(off)` |
| SDK 內建 | `NtCurrentTeb()` | `NtCurrentTeb()`（`winnt.h` 中就是 `__readgsqword(0x30)`） |

**WoW64 注意事項：** 32 位元程序同時擁有 32 位元與 64 位元的 TEB/PEB。32 位元 ntdll 讀的是 32 位元 PEB，x86 的 `LocaleEmulator.dll` 只需要處理這一份。從外部取得遠端程序的 PEB：

- 64 位元目標：`NtQueryInformationProcess(ProcessBasicInformation)` 的 `PebBaseAddress`
- 從 64 位元 Loader 看 WoW64 目標的 32 位元 PEB：`NtQueryInformationProcess(ProcessWow64Information)` 回傳的位址

### 2.2 PEB 的 NLS 欄位位移

| 欄位 | x86 位移 | x64 位移 | x64 型別大小 |
|---|---|---|---|
| `ProcessHeap` | 0x018 | 0x030 | 8 |
| `ReadOnlyStaticServerData` | 0x054 | 0x098 | 8 |
| **`AnsiCodePageData`** | **0x058** | **0x0A0** | 8 |
| **`OemCodePageData`** | **0x05C** | **0x0A8** | 8 |
| **`UnicodeCaseTableData`** | **0x060** | **0x0B0** | 8 |
| `NumberOfProcessors` | 0x064 | 0x0B8 | 4 |

[推論：與 WinDbg `dt ntdll!_PEB` 的公開輸出一致，自 Windows 7 起未變。] x64 的對齊規則是指標 8-byte 對齊。`ml.h` 的 `PEB_BASE` 註解只寫 x86 位移，但欄位型別（`PVOID`、`ULONG_PTR`）在 x64 自然對齊後的結果與上表一致。**一定要加 `static_assert` 把它鎖住**（見 §2.5）。

`LocaleEmulator.cpp:313-315` 直接寫入 `Peb->AnsiCodePageData` 等欄位，這段程式碼本身與架構無關，x64 可以沿用。

### 2.3 現代 Windows x64 的 NLS 初始化流程

1. **核心建立程序時** [推論]：kernel 把系統的 ANSI / OEM code page 與 `l_intl.nls` 對應的 NLS section 映射到新程序，並填好 `PEB->AnsiCodePageData`、`OemCodePageData`、`UnicodeCaseTableData`。這些頁面是唯讀的共享映射。
2. **ntdll 的 `LdrpInitializeProcess`** [推論 + 實測佐證]：讀取上述三個 PEB 欄位，初始化 ntdll **內部、未匯出**的 NLS 全域變數（例如 `NlsAnsiCodePage`、`NlsMbCodePageTag`、轉換表指標）。此後 `RtlMultiByteToUnicodeN` 等函式都使用這些內部變數。
   - **[實測]** 匯出的 `RtlInitNlsTables` / `RtlResetRtlTranslations` 在 build 26300 是空函式（鐵則 6）。所以**初始化完成後**，改寫 PEB 或呼叫這兩個 API 都無法改變 ntdll 的行為。這正是目前要靠 `LeRtl*` hook 的原因。
3. **kernelbase 初始化** [待驗證]：kernelbase 的 NLS 狀態（`GetACP()` 的回傳值等）有自己的初始化流程，資料來源可能是 PEB 欄位或 `NtGetNlsSectionPtr` / `NtInitializeNlsFiles`。LE 目前 hook 了 `NtInitializeNlsFiles`，表示這條路徑確實存在。
4. **地區（LCID）** [確定]：`NtQueryDefaultLocale`、`NtQueryDefaultUILanguage`、`NtQueryInstallUILanguage` 這幾個 syscall。LE 目前在 HookPort 攔截它們。

### 2.4 在 x64 下安全替換目標程序的 NLS 表

依可靠度排序：

**策略 A：Loader 在 resume 之前改寫遠端 PEB** [待驗證，價值最高]

LoaderDll 以 `CREATE_SUSPENDED` 建立程序時，初始執行緒還沒跑 `LdrInitializeThunk`，ntdll 的 NLS 內部變數**尚未初始化**。此時：

1. 在目標程序中配置記憶體，寫入 ANSI / OEM / case 三份 `.nls` 檔案內容（與 `LocaleEmulator.cpp:283-289` 的排列方式相同），之後改成 `PAGE_READONLY`
2. 以 `NtWriteVirtualMemory` 改寫遠端 PEB 的三個欄位
3. resume

若步驟 2 的假說成立，ntdll 初始化時會**直接採用 LE 的表**，從而繞開「`RtlInitNlsTables` 是空函式」的問題。這也可能讓 kernelbase 一併生效。
**驗證方法：** x64 版 LocaleTestApp 的 `CHECK_*` 全部為 `OK`，**而且在停用 `LeRtl*` hook 時仍然為 `OK`**。

**策略 B：沿用 `LeRtl*` hook** [實測有效（x86）]

鐵則 6 的做法。移植到 x64 時，hook 函式本體與架構無關（只依賴 ntdll 的 `RtlCustomCPToUnicodeN` 等），只需要換掉底層的 patch 引擎。**在策略 A 被證實之前，B 必須保留；即使 A 成立，B 仍建議保留作為防線。**

**策略 C：特徵碼掃描並直接改寫 ntdll 內部全域變數** —— **不建議**。每次 Windows 更新都可能失效，而且失敗時沒有明顯徵兆（只會靜靜地用錯 code page）。

**共通的安全要求：**
- 表格內容必須是完整、格式正確的 `.nls` 檔；ntdll 會依表頭的 `CodePage`、`MaximumCharacterSize`（DBCS）決定解析方式
- 記憶體設成 `PAGE_READONLY`，與系統映射一致，避免被誤寫
- 不要釋放原本的系統 NLS 映射（其他元件可能已經保留指標）

### 2.5 程式碼：跨架構 TEB / PEB / NLS 定位

```cpp
// src/Core/LocaleEmulator/LeArch.h（提議）
#pragma once
#include <windows.h>
#include <winternl.h>
#include <intrin.h>

#if defined(_M_X64)
    #define LE_TEB_SELF_OFFSET   0x30
    #define LE_TEB_PEB_OFFSET    0x60
    #define LE_PEB_ANSI_CP       0x0A0
    #define LE_PEB_OEM_CP        0x0A8
    #define LE_PEB_CASE_TABLE    0x0B0
    #define LeReadTebPointer(o)  ((PVOID)__readgsqword(o))
#elif defined(_M_IX86)
    #define LE_TEB_SELF_OFFSET   0x18
    #define LE_TEB_PEB_OFFSET    0x30
    #define LE_PEB_ANSI_CP       0x058
    #define LE_PEB_OEM_CP        0x05C
    #define LE_PEB_CASE_TABLE    0x060
    #define LeReadTebPointer(o)  ((PVOID)(ULONG_PTR)__readfsdword(o))
#else
    #error "Locale Emulator Core supports x86 and x64 only"
#endif

// 給遠端（跨架構）改寫用：與「本程序」的架構無關
#define LE_PEB32_ANSI_CP     0x058
#define LE_PEB32_OEM_CP      0x05C
#define LE_PEB32_CASE_TABLE  0x060
#define LE_PEB64_ANSI_CP     0x0A0
#define LE_PEB64_OEM_CP      0x0A8
#define LE_PEB64_CASE_TABLE  0x0B0

FORCEINLINE PVOID LeCurrentTeb() { return LeReadTebPointer(LE_TEB_SELF_OFFSET); }
FORCEINLINE PVOID LeCurrentPeb() { return LeReadTebPointer(LE_TEB_PEB_OFFSET); }

typedef struct _LE_PEB_NLS_VIEW
{
    PUSHORT* AnsiCodePageData;
    PUSHORT* OemCodePageData;
    PVOID*   UnicodeCaseTableData;
} LE_PEB_NLS_VIEW;

FORCEINLINE LE_PEB_NLS_VIEW LeGetPebNls(PVOID Peb)
{
    LE_PEB_NLS_VIEW View;
    View.AnsiCodePageData     = (PUSHORT*)((PBYTE)Peb + LE_PEB_ANSI_CP);
    View.OemCodePageData      = (PUSHORT*)((PBYTE)Peb + LE_PEB_OEM_CP);
    View.UnicodeCaseTableData = (PVOID*)  ((PBYTE)Peb + LE_PEB_CASE_TABLE);
    return View;
}

// 和 ml.h 的 PEB_BASE 互相鎖定：只要任何一邊錯了，建置就會失敗。
// （放在已 include ml.h 的 .cpp 裡）
// static_assert(FIELD_OFFSET(PEB_BASE, AnsiCodePageData)     == LE_PEB_ANSI_CP,    "PEB layout");
// static_assert(FIELD_OFFSET(PEB_BASE, OemCodePageData)      == LE_PEB_OEM_CP,     "PEB layout");
// static_assert(FIELD_OFFSET(PEB_BASE, UnicodeCaseTableData) == LE_PEB_CASE_TABLE, "PEB layout");
// static_assert(FIELD_OFFSET(TEB_BASE, ProcessEnvironmentBlock) == LE_TEB_PEB_OFFSET, "TEB layout");
```

策略 A 的遠端改寫（在 Loader 端、目標程序仍處於 suspended 狀態時呼叫）：

```cpp
// 只依賴 ntdll；x64 Loader 可以處理 x64 目標與 WoW64 目標兩種情況。
extern "C" NTSYSAPI NTSTATUS NTAPI NtWriteVirtualMemory(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);

typedef struct _LE_REMOTE_NLS
{
    ULONG64 AnsiTable;      // 目標程序內、已寫好且設為唯讀的表格位址
    ULONG64 OemTable;
    ULONG64 CaseTable;
} LE_REMOTE_NLS;

static NTSTATUS LeWriteRemotePointer(HANDLE Process, ULONG_PTR Address, ULONG64 Value, BOOLEAN Target32)
{
    ULONG   Value32 = (ULONG)Value;
    PVOID   Source  = Target32 ? (PVOID)&Value32 : (PVOID)&Value;
    SIZE_T  Size    = Target32 ? sizeof(ULONG)   : sizeof(ULONG64);

    if (Target32 && (Value >> 32) != 0)
        return STATUS_INVALID_PARAMETER;   // 32 位元目標的表格必須配置在 4GB 以下

    return NtWriteVirtualMemory(Process, (PVOID)Address, Source, Size, nullptr);
}

NTSTATUS LePatchRemotePebNls(HANDLE Process, BOOLEAN Target32, const LE_REMOTE_NLS* Nls)
{
    NTSTATUS  Status;
    ULONG_PTR Peb;

    if (Target32)
    {
        // WoW64 目標：取 32 位元 PEB（僅限 x64 Loader 呼叫）
        Status = NtQueryInformationProcess(Process, ProcessWow64Information, &Peb, sizeof(Peb), nullptr);
    }
    else
    {
        PROCESS_BASIC_INFORMATION Basic;
        Status = NtQueryInformationProcess(Process, ProcessBasicInformation, &Basic, sizeof(Basic), nullptr);
        Peb = (ULONG_PTR)Basic.PebBaseAddress;
    }
    if (!NT_SUCCESS(Status) || Peb == 0)
        return NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status;

    const ULONG_PTR Ansi = Target32 ? LE_PEB32_ANSI_CP    : LE_PEB64_ANSI_CP;
    const ULONG_PTR Oem  = Target32 ? LE_PEB32_OEM_CP     : LE_PEB64_OEM_CP;
    const ULONG_PTR Case = Target32 ? LE_PEB32_CASE_TABLE : LE_PEB64_CASE_TABLE;

    if (!NT_SUCCESS(Status = LeWriteRemotePointer(Process, Peb + Ansi, Nls->AnsiTable, Target32))) return Status;
    if (!NT_SUCCESS(Status = LeWriteRemotePointer(Process, Peb + Oem,  Nls->OemTable,  Target32))) return Status;
    return LeWriteRemotePointer(Process, Peb + Case, Nls->CaseTable, Target32);
}
```

> 更正：初版報告說 `ml.h` 的 `Nt_CurrentTeb()` 在 x64 用 FS 讀 TEB，這是誤判。它呼叫的 `ReadFsPtr()` 在 `ML_AMD64` 分支使用 `__readgsqword`，結果正確，只是函式名稱有誤導性。`LeArch.h` 的 `static_assert` 仍然值得加，用來鎖住 `TEB_BASE` / `PEB_BASE` 的佈局。

---

## 3. x64 Hook 機制與組語

### 3.1 MSVC x64 沒有 `__asm`：落地方案

| 需求 | 方案 |
|---|---|
| 一般 hook 函式（`LeRtlMultiByteToUnicodeN` 等） | **純 C++，簽章與原函式完全相同**。編譯器自動處理 shadow space、對齊與 non-volatile 暫存器，**不需要任何 asm**。現有 hook 大多屬於這一類 |
| 讀寫 TEB / PEB | `__readgsqword` / `__writegsqword` |
| 原子寫入 patch | `_InterlockedCompareExchange128`（16 bytes，需要 16-byte 對齊）或 `_InterlockedExchange64` |
| 取得回傳位址 | `_ReturnAddress()`、`_AddressOfReturnAddress()`（`LoadFirstDll` 已在使用） |
| 參數不定、必須保留暫存器的 thunk | **MASM（ml64）**，並寫好 unwind 資訊（`PROC FRAME`） |
| 自行發出 syscall | MASM（或從原 stub 抄出 syscall 編號，見 §3.2） |

vcxproj 啟用 MASM 的方式：

```xml
<ImportGroup Label="ExtensionSettings">
  <Import Project="$(VCTargetsPath)\BuildCustomizations\masm.props" />
</ImportGroup>
<ItemGroup>
  <MASM Include="Hooks\LeX64Thunks.asm">
    <ExcludedFromBuild Condition="'$(Platform)'=='Win32'">true</ExcludedFromBuild>
  </MASM>
</ItemGroup>
<ImportGroup Label="ExtensionTargets">
  <Import Project="$(VCTargetsPath)\BuildCustomizations\masm.targets" />
</ImportGroup>
```

`CoreRuntime.cpp` 和 `HookPort.cpp` 裡的所有 `__asm`，都要用 `#if defined(_M_IX86)` 包起來。

### 3.2 64 位元跳轉與 Trampoline 設計

**為什麼 `E9 rel32` 不夠用：** rel32 只能涵蓋 ±2GB。x64 下 ntdll 位於高位址（`0x7FFx'xxxxxxxx`），自行配置的 hook DLL 或 trampoline 很容易超出這個範圍。

**跳轉編碼比較：**

| 編碼 | 長度 | 破壞暫存器 | CET Shadow Stack | 備註 |
|---|---|---|---|---|
| `E9 rel32` | 5 | 無 | ✅ | 目標必須在 ±2GB 內 |
| `FF 25 00000000` + `imm64` | **14** | 無 | ✅ | 透過 RIP-relative 間接跳轉，最通用 |
| `48 B8 imm64` + `FF E0`（mov rax / jmp rax） | 12 | **RAX** | ✅ | 函式入口處 RAX 是 volatile，通常可以用，但對 syscall stub 會出問題（其中 `mov eax, idx` 在後） |
| `68 imm32` + `C7 44 24 04 imm32` + `C3`（push/ret） | 14 | 無 | ❌ | **`ret` 和 shadow stack 不一致，啟用 CET 的程序會 #CP 崩潰**。ml 的 `OpPush` 就是這個做法（而且編碼還有錯） |

**建議策略（與 MinHook 相同的思路）：**

1. 在目標函式 ±2GB 範圍內配置一個 **relay page**（以 `NtAllocateVirtualMemory` 加上位址提示，由目標位址往下或往上，以 64KB allocation granularity 逐段嘗試）
2. 在目標函式入口只寫 **5-byte `E9`**，跳到 relay
3. relay 裡放 **14-byte `FF 25`**，跳到真正的 hook 函式（可以在任何位址）
4. 找不到 near page 時，才退回在目標入口直接寫 14-byte `FF 25`

好處：入口只覆蓋 5 bytes，遇到「被覆蓋的範圍內有分支目標」或「指令需要重定位」的機率大幅降低。

**Prologue 覆蓋長度：** 用 decoder 逐條指令累加，直到 ≥ 5（near）或 ≥ 14（far），覆蓋範圍必須是**完整指令邊界**。被搬到 trampoline 的指令（stolen bytes）要做以下檢查：

| 指令類型 | 處理 |
|---|---|
| 一般指令 | 直接複製 |
| RIP-relative 記憶體運算元（`[rip+disp32]`） | 重新計算 disp32；trampoline 必須位於原位址 ±2GB 內（near relay 正好滿足），否則放棄 |
| `jcc rel8/rel32`、`jmp rel`、`call rel32` | 轉換成絕對形式（`jcc` 反轉條件 + `FF 25`；`call` 改成 `FF 15` + 8-byte slot） |
| `ret`、`int3`、`jmp` 落在覆蓋範圍內 | 函式太短，**放棄 hook**（例如 `C3` 開頭的 stub） |
| 覆蓋範圍內有其他分支的目標 | decoder 無法得知 → 必須維持最小覆蓋長度；這也是優先採用 5-byte 方案的原因 |

**Decoder 選型：**

| | ml 的 `LdeGetOpCodeSize64` | MinHook（HDE64） | Zydis |
|---|---|---|---|
| 只依賴 ntdll | ✅ | ❌（kernel32 的 `VirtualAlloc`、Toolhelp32 列舉執行緒；需要 CRT） | ✅（`ZYAN_NO_LIBC`） |
| 回報 RIP-relative / 分支類型 | 只回報長度 | 有 | **完整運算元資訊** |
| 在本專案經過測試 | ❌（x64 從未建置） | — | — |
| 授權 | 隨 ml | BSD-2 | MIT |

**建議：Zydis（只用 decoder、no-libc、靜態連結）。** MinHook 違反鐵則 4、5，不能直接引入，但它的 relay page 與重定位邏輯值得參考。

**x64 syscall hook 的特殊處理：** x64 的 `Nt*` stub 在 Windows 10/11 上是固定格式 [推論]：

```
4C 8B D1                mov  r10, rcx
B8 xx xx xx xx          mov  eax, <syscall index>
F6 04 25 08 03 FE 7F 01 test byte ptr [7FFE0308h], 1
75 03                   jne  +3
0F 05                   syscall
C3                      ret
CD 2E                   int  2Eh
C3                      ret
```

HookPort 的「單一入口攔截全部 syscall」在 x64 上做不到。改為**針對 LE 實際用到的 stub 逐一 hook**。依目前原始碼，需要的有：`NtQueryDefaultLocale`、`NtQueryDefaultUILanguage`、`NtQueryInstallUILanguage`、`NtQuerySystemInformation`、`NtQueryInformationThread`、`NtQueryValueKey`、`NtInitializeNlsFiles`、`NtCreateUserProcess`、`NtTerminateThread`，以及位於 **win32u.dll** 的 `NtUserCreateWindowEx*`、`NtUserDefSetText`、`NtUserMessageCall`、`NtGdiHfontCreate`。

對 syscall stub **不要搬移指令**，改成：先驗證 stub 符合上面的格式，讀出 syscall index，然後自己產生一份「呼叫原始 syscall」的 stub：

```
4C 8B D1        mov r10, rcx
B8 idx          mov eax, idx
0F 05           syscall
C3              ret
```

LE 的 hook 想呼叫原函式時，就呼叫這份自建 stub。完全沒有重定位問題。

### 3.3 程式碼：14-byte 絕對跳轉與 hook 安裝

```cpp
// src/Core/LocaleEmulator/Hook/LeX64Hook.cpp（提議，僅 _M_X64）
#if defined(_M_X64)
#include <windows.h>
#include <winternl.h>
#include <Zydis/Zydis.h>

extern "C" {
NTSYSAPI NTSTATUS NTAPI NtProtectVirtualMemory(HANDLE, PVOID*, PSIZE_T, ULONG, PULONG);
NTSYSAPI NTSTATUS NTAPI NtAllocateVirtualMemory(HANDLE, PVOID*, ULONG_PTR, PSIZE_T, ULONG, ULONG);
NTSYSAPI NTSTATUS NTAPI NtFlushInstructionCache(HANDLE, PVOID, SIZE_T);
}

constexpr SIZE_T kAbsJmpSize  = 14;   // FF 25 00000000 + imm64
constexpr SIZE_T kRelJmpSize  = 5;    // E9 rel32
constexpr SIZE_T kMaxStolen   = 32;

// FF 25 00 00 00 00   jmp qword ptr [rip+0]
// xx xx xx xx xx xx xx xx   ; 目標位址
static void LeEmitAbsJmp(PBYTE At, ULONG64 Target)
{
    At[0] = 0xFF;
    At[1] = 0x25;
    *(ULONG*)  (At + 2) = 0;
    *(ULONG64*)(At + 6) = Target;
}

static bool LeEmitRelJmp(PBYTE At, ULONG_PTR Target)
{
    LONG64 Delta = (LONG64)Target - (LONG64)(At + kRelJmpSize);
    if (Delta < INT_MIN || Delta > INT_MAX)
        return false;
    At[0] = 0xE9;
    *(LONG*)(At + 1) = (LONG)Delta;
    return true;
}

// 在 Target 前後 2GB 內找一塊可配置的頁面（64KB granularity）
static PVOID LeAllocNear(PVOID Target, SIZE_T Size)
{
    const ULONG_PTR Granularity = 0x10000;
    const ULONG_PTR Base  = (ULONG_PTR)Target & ~(Granularity - 1);
    const ULONG_PTR Range = 0x7FFF0000;

    for (ULONG_PTR Delta = Granularity; Delta < Range; Delta += Granularity)
    {
        for (int Dir = 0; Dir < 2; ++Dir)
        {
            ULONG_PTR Probe = Dir == 0 ? Base - Delta : Base + Delta;
            if (Dir == 0 && Probe > Base) continue;            // 下溢
            PVOID  Address = (PVOID)Probe;
            SIZE_T Bytes   = Size;
            if (NT_SUCCESS(NtAllocateVirtualMemory(NtCurrentProcess(), &Address, 0, &Bytes,
                                                   MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE)))
                return Address;
        }
    }
    return nullptr;
}

// 回傳應該覆蓋的位元組數；遇到無法安全搬移的指令時回傳 0
static SIZE_T LeMeasurePrologue(const BYTE* Code, SIZE_T Needed, bool* HasRipRelative)
{
    ZydisDecoder Decoder;
    ZydisDecoderInit(&Decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

    SIZE_T Length = 0;
    *HasRipRelative = false;

    while (Length < Needed)
    {
        ZydisDecodedInstruction Insn;
        ZydisDecodedOperand     Ops[ZYDIS_MAX_OPERAND_COUNT];
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&Decoder, Code + Length, kMaxStolen - Length, &Insn, Ops)))
            return 0;

        // 第一版只接受「不改變控制流」的指令；分支／ret 一律放棄，不做轉換
        if (Insn.meta.category == ZYDIS_CATEGORY_COND_BR ||
            Insn.meta.category == ZYDIS_CATEGORY_UNCOND_BR ||
            Insn.meta.category == ZYDIS_CATEGORY_CALL ||
            Insn.meta.category == ZYDIS_CATEGORY_RET ||
            Insn.meta.category == ZYDIS_CATEGORY_INTERRUPT)
            return 0;

        if (Insn.attributes & ZYDIS_ATTRIB_IS_RELATIVE)
            *HasRipRelative = true;   // 呼叫端要負責修正 disp32

        Length += Insn.length;
    }
    return Length;
}

struct LE_X64_HOOK
{
    PBYTE  Target;
    PBYTE  Trampoline;       // stolen bytes + FF25 跳回 Target+Stolen
    PBYTE  Relay;            // FF25 跳到 Detour（使用 near 模式時）
    SIZE_T Stolen;
    BYTE   Backup[kMaxStolen];
};

// 前提：在單一執行緒的時機呼叫（LE 注入時程序只有一條執行緒），
// 所以不處理「其他執行緒正執行到被覆蓋區段」的情況。
NTSTATUS LeInstallHookX64(PVOID Target, PVOID Detour, LE_X64_HOOK* Hook)
{
    PBYTE Code  = (PBYTE)Target;
    PBYTE Block = (PBYTE)LeAllocNear(Target, 0x1000);
    bool  Near  = Block != nullptr;
    bool  RipRel;

    SIZE_T Needed = Near ? kRelJmpSize : kAbsJmpSize;
    SIZE_T Stolen = LeMeasurePrologue(Code, Needed, &RipRel);
    if (Stolen == 0 || Stolen > kMaxStolen)
        return STATUS_NOT_SUPPORTED;
    if (RipRel)
        return STATUS_NOT_SUPPORTED;   // 第一版：遇到 RIP-relative 就放棄；之後再實作 disp32 修正

    if (!Near)
    {
        // 沒有 near page：trampoline 可以放在任何位址（只用絕對跳轉）
        // 實作時改用一般 NtAllocateVirtualMemory；此處省略
        return STATUS_NO_MEMORY;
    }

    Hook->Target     = Code;
    Hook->Relay      = Block;                    // [0x00] FF25 -> Detour
    Hook->Trampoline = Block + 0x20;             // [0x20] stolen + FF25 -> Target+Stolen
    Hook->Stolen     = Stolen;
    memcpy(Hook->Backup, Code, Stolen);

    LeEmitAbsJmp(Hook->Relay, (ULONG64)Detour);
    memcpy(Hook->Trampoline, Code, Stolen);
    LeEmitAbsJmp(Hook->Trampoline + Stolen, (ULONG64)(Code + Stolen));

    // 修改入口
    PVOID  ProtectBase = Code;
    SIZE_T ProtectSize = Stolen;
    ULONG  OldProtect;
    NTSTATUS Status = NtProtectVirtualMemory(NtCurrentProcess(), &ProtectBase, &ProtectSize,
                                             PAGE_EXECUTE_READWRITE, &OldProtect);
    if (!NT_SUCCESS(Status))
        return Status;

    LeEmitRelJmp(Code, (ULONG_PTR)Hook->Relay);
    for (SIZE_T i = kRelJmpSize; i < Stolen; ++i)
        Code[i] = 0xCC;                          // 剩餘的位元組填 int3，避免跳進半截指令時靜靜地執行錯誤指令

    NtProtectVirtualMemory(NtCurrentProcess(), &ProtectBase, &ProtectSize, OldProtect, &OldProtect);
    NtFlushInstructionCache(NtCurrentProcess(), Code, Stolen);
    NtFlushInstructionCache(NtCurrentProcess(), Block, 0x1000);

    // 完成後把 Block 改成 PAGE_EXECUTE_READ（W^X），此處省略
    return STATUS_SUCCESS;
}
#endif // _M_X64
```

> `memcpy` 在 Core 中解析到 ntdll 的匯出（x64 的 ntdll 也有匯出），不違反 No-CRT。

### 3.4 ABI：Shadow Space 與 16-byte 對齊 [確定]

Microsoft x64 呼叫約定的重點：

- 前四個整數參數放在 `RCX, RDX, R8, R9`；浮點參數放在 `XMM0–3`
- **caller** 必須在 call 前保留 32 bytes 的 shadow space（home space），供 callee 存放這四個暫存器
- **call 指令執行前，RSP 必須 16-byte 對齊**；所以進入 callee 時 `RSP ≡ 8 (mod 16)`
- Volatile：`RAX RCX RDX R8–R11 XMM0–5`；Non-volatile：`RBX RBP RDI RSI RSP R12–R15 XMM6–15`

**對 hook 設計的影響：**

| 情境 | 風險 | 防護 |
|---|---|---|
| 以 `jmp` 進入 detour（`E9`、`FF 25`） | 堆疊狀態與正常呼叫完全相同，**安全** | 首選做法 |
| 以 `call` 進入 detour（ml 的 `FunctionCallVa`，例如 `LdrInitNtContinue` hook） | 多 push 一個回傳位址，**進入時 RSP ≡ 0 (mod 16)**，偏了 8 bytes。detour 內只要有 `movaps` 存 XMM（編譯器很常這樣做）就會 #GP | 改用 `jmp`；或在 thunk 裡 `sub rsp, 8` 修正對齊，且 unwind 資訊要對應 |
| 手寫 thunk 呼叫 C++ 回呼 | 忘記保留 32 bytes shadow space → callee 寫入 home space 時蓋掉 thunk 自己的資料 | 固定 `sub rsp, 28h + N`（見下方範例） |
| 手寫 thunk 沒有 unwind 資訊 | 在 thunk 呼叫的程式碼內發生例外時無法 unwind → 程序直接被終止（`__try` 也接不住） | MASM 一律用 `PROC FRAME` + `.allocstack` / `.endprolog` |
| detour 的參數宣告與原函式不一致 | x64 上不會像 x86 stdcall 那樣把堆疊弄壞，但參數錯位會讀到垃圾值 | detour 一律用 `decltype(&原函式)` 宣告指標型別 |

**MASM 範例：保留全部參數暫存器，呼叫 C++ 前置回呼，再跳回 trampoline**

```asm
; src/Core/LocaleEmulator/Hooks/LeX64Thunks.asm（ml64，僅 x64）
; 進入時的狀態與正常呼叫相同：RSP ≡ 8 (mod 16)，[RSP] = 回傳位址，
; [RSP+8 .. RSP+28h) 是 caller 保留的 shadow space。

EXTERN LeOnHookedCall:PROC          ; void LeOnHookedCall(ULONG_PTR* IntArgs)
EXTERN g_LeOriginalTrampoline:QWORD

.code

LeGenericThunk PROC FRAME
        ; 1) 四個整數參數暫存器存回 caller 的 home space（不需要 unwind 資訊）
        mov     [rsp+08h], rcx
        mov     [rsp+10h], rdx
        mov     [rsp+18h], r8
        mov     [rsp+20h], r9

        ; 2) 配置 20h shadow + 40h XMM0–3 存放區 + 8 對齊 = 68h
        ;    進入時 RSP ≡ 8，減去 68h 後 RSP ≡ 0 (mod 16) → movaps 安全
        sub     rsp, 68h
        .allocstack 68h
        .endprolog

        movaps  [rsp+20h], xmm0
        movaps  [rsp+30h], xmm1
        movaps  [rsp+40h], xmm2
        movaps  [rsp+50h], xmm3

        lea     rcx, [rsp+70h]          ; = 原本 RSP+8，即 IntArgs[0..3]
        call    LeOnHookedCall          ; callee 可以使用 [rsp .. rsp+20h) 作為 home space

        movaps  xmm0, [rsp+20h]
        movaps  xmm1, [rsp+30h]
        movaps  xmm2, [rsp+40h]
        movaps  xmm3, [rsp+50h]

        mov     rcx, [rsp+70h]
        mov     rdx, [rsp+78h]
        mov     r8,  [rsp+80h]
        mov     r9,  [rsp+88h]

        add     rsp, 68h
        jmp     qword ptr [g_LeOriginalTrampoline]   ; tail jump；回傳位址仍是原 caller 的
LeGenericThunk ENDP

; 直接呼叫原始 syscall 的 stub 範本：由 hook 引擎依 syscall index 複製並修補
; mov eax 的立即值，這裡只是格式示範。
LeSyscallTemplate PROC
        mov     r10, rcx
        mov     eax, 0FFFFFFFFh         ; 由引擎改寫成真正的 index
        syscall
        ret
LeSyscallTemplate ENDP

END
```

> x64 的 unwinder 只認得「`add rsp, imm` 後面緊接 `ret` 或 `jmp`」這種 epilog 形式。上面的寫法符合這個要求：還原暫存器的 `movaps` / `mov` 都放在 `add rsp` 之前（仍屬於函式本體，unwind 資訊有效），而 `add rsp, 68h` 與 `jmp` 之間沒有其他指令。之後修改這個 thunk 時要維持這個順序。

**實務建議：** LE 現有的 hook 都是「完全取代原函式，必要時呼叫原函式」的形式，**只要用 C++ 並保持相同簽章即可，不需要 `LeGenericThunk`**。MASM 只用在 syscall stub 範本與少數必須保留原始暫存器的特殊 hook（例如 `LdrInitNtContinue` 這類以 `call` 方式 patch 的點）。

---

## 4. x64 Import Libraries 與 Linker

### 4.1 `.def` 在 x64 的差異 [確定]

| 項目 | x86 | x64 |
|---|---|---|
| `__stdcall` 修飾 | `_Name@N` → 需要 `Name@N EXPORTAS Name` | **x64 沒有 `__stdcall`／`@N` 修飾**（呼叫約定只有一種），直接寫 `Name` |
| `__cdecl` 前綴底線 | lib.exe 自動加 `_` | 沒有前綴 |
| 64 位元算術 helper（`_alldiv` 等） | 需要 | **不存在**（原生 64 位元指令） |
| 浮點轉整數（`_ftol*`） | 需要 | 不存在（使用 SSE2 `cvttsd2si`） |
| Stack probe | `_chkstk`、`_alloca_probe` | `__chkstk`（兩個底線） |
| SEH handler | `_except_handler4_common` | **`__C_specific_handler`**（ntdll x64 有匯出） |
| `lib.exe` 參數 | `/machine:x86` | `/machine:x64` |

**建議做法：分開兩份 .def**，不要用條件式混在同一個檔案裡：

```
src/Core/ImportLibs/
  x86/ntdll_ex.def        ← 現有檔案搬過來
  x86/kernel32_ex.def
  x64/ntdll_ex.def        ← 新增
  x64/kernel32_ex.def     ← 新增
```

`x64/kernel32_ex.def`：

```def
; kernel32.dll exports that the Windows SDK's kernel32.lib does not provide (x64).
; x64 has a single calling convention: no "@N" decoration and no EXPORTAS needed.
LIBRARY kernel32.dll
EXPORTS
    CreateProcessInternalW
```

`x64/ntdll_ex.def`：

```def
; ntdll.dll exports that the Windows SDK's ntdll.lib does not provide (x64).
LIBRARY ntdll.dll
EXPORTS
    ; Undocumented loader APIs
    LdrInitializeThunk
    LdrRegisterDllNotification
    LdrUnregisterDllNotification

    ; NLS conversion
    RtlUpcaseUnicodeStringToAnsiString

    ; Compiler helpers (x64)
    __chkstk
    __C_specific_handler

    ; C runtime functions (exported by x64 ntdll as well)
    memcmp
    memcpy
    memmove
    memset
    strlen
    wcslen
    wcsstr
    _stricmp
    _wcsicmp
    wcsicmp                 EXPORTAS _wcsicmp
    _snwprintf
    _vsnprintf
    _vsnwprintf
    _vscwprintf
    swprintf
    vswprintf               EXPORTAS _vsnwprintf
```

> 實作前，先在 Windows x64 上執行 `dumpbin /exports C:\Windows\System32\ntdll.dll`，逐一確認上面的名稱都存在；並用 `dumpbin /linkermember` 檢查 SDK 的 x64 `ntdll.lib` 是否已經提供 `__C_specific_handler` / `__chkstk`。已經提供的就從 .def 移除，避免 LNK4006（重複定義）。

### 4.2 SEH：x86 frame-based → x64 table-based

| | x86 | x64 |
|---|---|---|
| 機制 | 執行時把 `EXCEPTION_REGISTRATION` 串在 `fs:[0]` 的鏈結串列上 | 編譯期產生 `.pdata`（RUNTIME_FUNCTION）+ `.xdata`（UNWIND_INFO），由 `RtlLookupFunctionEntry` 查表 |
| `__try` 的 handler | `_except_handler3`（/GS-）或 `_except_handler4` | `__C_specific_handler`（scope table 存在 `.xdata`） |
| 本專案的實作 | `CoreRuntime.cpp` 自行實作 `_except_handler3`，並包裝 ntdll 的 `_except_handler4_common` | **不需要自己寫任何東西**：import ntdll 的 `__C_specific_handler` 即可 |
| Security cookie | `__try` 會用 `__security_cookie` XOR scope table 指標 | `__try` 不使用 cookie；只有 `/GS` 會使用（`__GSHandlerCheck`） |
| `/SAFESEH` | 有意義（`ImageHasSafeExceptionHandlers`） | 不適用 |

`CoreRuntime.cpp` 的 x64 化：

```cpp
// Runtime/CoreRuntime.cpp（結構示意）

// ---- 共用：delay-load helper ----
// __delayLoadHelper2 與架構無關：IMAGE_THUNK_DATA 在 x64 是 64 位元，
// IMAGE_SNAP_BY_ORDINAL / IMAGE_ORDINAL 巨集會自動選用 64 位元版本。

// ---- 共用：浮點 ----
extern "C" int _fltused = 0x9875;

#if defined(_M_IX86)

// _alloca_probe_16、__security_cookie、_except_handler3、_except_handler4
// 全部維持現狀（naked __asm 只在 x86 可以編譯）。

#elif defined(_M_X64)

// SEH：__try/__except 由編譯器產生 .pdata/.xdata，handler 是 ntdll 匯出的
// __C_specific_handler（由 ImportLibs/x64/ntdll_ex.def 提供），這裡不需要任何程式碼。
//
// /GS- 時不會參考 __security_cookie；若日後要啟用 /GS，需要提供：
//   UINT_PTR __security_cookie = <每次載入時用 RDTSC 等方式產生的亂數>;
//   void __security_check_cookie(UINT_PTR) { if (...) __fastfail(FAST_FAIL_STACK_COOKIE_CHECK_FAILURE); }
//   以及 __GSHandlerCheck（ntdll 沒有匯出，必須自行實作）→ 不建議，維持 /GS-。
//
// _alloca 在 x64 由編譯器內聯成 __chkstk 呼叫，不需要 _alloca_probe_16。

#else
#error "Unsupported architecture"
#endif
```

**關鍵陷阱：手動映射的 image 沒有登記 `.pdata`。** `InjectSelfToChildProcess`（`NtdllHook.cpp:258-316`）把整個 DLL 以 `LoadPeImage` + `RelocPeImage` 的方式**手動映射**到子程序，再從 shadow 執行 `LoadFirstDll`。在 x86 上，SEH 鏈存在堆疊上，所以沒有問題；**在 x64 上，系統查不到這塊記憶體的 function table**，只要 shadow 中的程式碼發生例外（即使有 `__try` 包住），程序就會直接被終止。對策：

1. 在 shadow 裡最先執行的程式碼中，呼叫 `RtlAddFunctionTable(pdata, count, (DWORD64)ShadowBase)` 登記 `.pdata`（ntdll 匯出）
2. 或者讓 shadow 中執行的程式碼保持極簡：不使用 `__try`、不呼叫任何可能丟出例外的函式，只做「呼叫 `LdrLoadDll` 載入真正的 DLL」這件事。真正的 DLL 經由 loader 正常載入，`.pdata` 會自動登記 → **建議採用這個做法**

### 4.3 `Core.props` 的 x64 設定清單

```xml
<!-- 平台相關的設定：放在現有 ItemDefinitionGroup 之後 -->
<PropertyGroup>
  <LeLibMachine Condition="'$(Platform)' == 'Win32'">x86</LeLibMachine>
  <LeLibMachine Condition="'$(Platform)' == 'x64'">x64</LeLibMachine>
  <LeImportDefDir>$(LeCoreDir)ImportLibs\$(LeLibMachine)\</LeImportDefDir>
</PropertyGroup>

<ItemGroup>
  <!-- 取代原本的 ImportLibs\*.def -->
  <LeImportDef Include="$(LeImportDefDir)*.def" />
</ItemGroup>

<ItemDefinitionGroup Condition="'$(Platform)' == 'Win32'">
  <ClCompile>
    <EnableEnhancedInstructionSet>StreamingSIMDExtensions</EnableEnhancedInstructionSet>
  </ClCompile>
  <Link>
    <ImageHasSafeExceptionHandlers>false</ImageHasSafeExceptionHandlers>
  </Link>
</ItemDefinitionGroup>

<ItemDefinitionGroup Condition="'$(Platform)' == 'x64'">
  <ClCompile>
    <!-- x64 預設就有 SSE2；/arch:SSE 在 x64 無效，會產生 D9002 警告 -->
    <EnableEnhancedInstructionSet>NotSet</EnableEnhancedInstructionSet>
    <!-- 與 x86 相同：無 CRT、無 C++ EH、無 RTTI、/GS-、無 CFG -->
    <BufferSecurityCheck>false</BufferSecurityCheck>
    <ControlFlowGuard>false</ControlFlowGuard>
    <!-- 若程式碼大量使用 SSE intrinsics，可考慮 /arch:AVX2；但會失去舊 CPU 支援，不建議 -->
  </ClCompile>
  <Link>
    <!-- x64 沒有 /SAFESEH；不要設定 ImageHasSafeExceptionHandlers -->
    <TargetMachine>MachineX64</TargetMachine>
    <!-- 注入用 DLL 明確關閉 CET 相容標記，避免日後 shadow stack 嚴格模式下的意外；
         若要支援 CET，所有 hook 都不能使用 push/ret，並需另外審查 -->
    <AdditionalOptions>/CETCOMPAT:NO %(AdditionalOptions)</AdditionalOptions>
  </Link>
</ItemDefinitionGroup>
```

`LeBuildImportLibs` target 改為：

```xml
<Exec Command="lib.exe /nologo /machine:$(LeLibMachine) /def:&quot;%(LeImportDef.FullPath)&quot; /out:&quot;$(LeImportLibDir)%(LeImportDef.Filename).lib&quot;" />
```

**必須保留的設定（兩個平台都一樣）：**

| 設定 | 理由 |
|---|---|
| `IgnoreSpecificDefaultLibraries`（整串 CRT lib） | 鐵則 3 |
| `ExceptionHandling=false`、`RuntimeTypeInfo=false` | 不連結 C++ EH runtime |
| `BufferSecurityCheck=false`（/GS-） | 不需要 cookie 與 `__GSHandlerCheck` |
| `ControlFlowGuard=false` | 不需要 `__guard_*` 符號；手寫 thunk 也不必登記 CFG 目標 |
| `_NO_CRT_STDIO_INLINE`、`_USE_STD_VECTOR_ALGORITHMS=0` | 與 x86 理由相同 |
| `LeCheckNoStaticKernel32` | 鐵則 4，x64 同樣適用 |
| `/Zc:threadSafeInit-` | 函式內 static 變數不能依賴 CRT 的 TLS 初始化 |

**`CoreRuntime.cpp` 註解中的 `/Gr`**：x64 會忽略 `__fastcall` / `__stdcall` 關鍵字，所以 `/Gr` 在 x64 沒有作用，也不會造成問題；x86 / x64 共用的宣告不需要修改。

---

## 5. 遷移路線圖

依「先解除阻塞、再降低風險、最後擴大範圍」排序。每個階段都要建置與測試綠燈後才 commit；`src/Core/` 的修改一律加 `[Core]` 前綴。

| 階段 | 內容 | 重要性 | 阻力 | 驗證方式 |
|---|---|---|---|---|
| **P0** | 修改 AGENTS.md 鐵則 1、2，定義 x64 的範圍（是否支援 x64 目標、子程序跨架構是否支援） | 必要 | 低 | 使用者確認 |
| **P1** | CI 新增 `Release\|x64` 的 Core 建置（先讓它能編譯，hook 暫時全部停用）：`#if _M_IX86` 包住所有 `__asm`、拆分 .def、`Core.props` 依平台切換 | 高 | 中 | CI 綠燈、`dumpbin /dependents` 只有 ntdll |
| **P2** | `Core.Tests` 新增 x64 組態：`LEB`、`LEPEB`、`REGISTRY_REDIRECTION_ENTRY64` 的大小與位移，以及 `PEB_BASE` / `TEB_BASE` 的 `static_assert` | 高 | 低 | 測試綠燈 |
| **P3** | 修正 ml 的 x64 錯誤：`Mp` 的 x64 `OpPush` 跳轉（改為不支援，x64 一律走新的 HookEngine） | 高 | 低 | 單元測試（對產生的 bytes 做比對） |
| **P4** | x64 hook 引擎：near relay + Zydis + syscall stub 複製；在測試 exe 裡 hook 自己的函式來驗證 | **最高** | **高** | 新增 C++ 單元測試 |
| **P5** | LocaleTestApp 加 x64 組態；smoke test 支援 x64 | 高 | 中 | 現有 `CHECK_*` 在 x86 仍然全綠 |
| **P6** | 驗證策略 A（Loader 在 resume 前改寫遠端 PEB）：只做這一件事，**不裝任何 hook**，看 `CHECK_*` 有哪些變成 OK | 高（決定後續方向） | 中 | x64 smoke test |
| **P7** | 移植 `LeRtl*` hook、地區相關 syscall hook（`NtQueryDefault*`）、時區 | 高 | 中 | x64 smoke test 全綠 |
| **P8** | LEProc 依目標 PE Machine 分派；新增 LEProc64；ShellExtension 不需要修改 | 中 | 中 | `LEProc.Tests` 新增分派測試 |
| **P9** | user32 / gdi32 / win32u 的 hook（視窗標題、字型） | 中 | 高 | LocaleTestApp 新增 user32 x64 檢查 |
| **P10** | 子程序注入（x64 → x64）；手動映射的 shadow 只呼叫 `LdrLoadDll` | 中 | 高 | 新增「啟動子程序」的 smoke test |
| **P11**（選擇性） | 評估 Loader 改用 Rust | 低 | 中 | — |

### 潛在故障清單（Pitfalls）

| # | 問題 | 症狀 | 防護 |
|---|---|---|---|
| 1 | 以為 ml 的 `ML_AMD64` 分支可用 | 隨機崩潰、patch 出錯誤的跳轉 | 把每條 x64 路徑當新程式碼審查；P3 |
| 2 | push/ret 跳轉遇到 CET shadow stack | 啟用 CET 的程序（Chrome 系、部分遊戲）立即 `STATUS_STACK_BUFFER_OVERRUN`（#CP） | 只用 `jmp` 形式；不修改回傳位址 |
| 3 | 以 `call` 方式 patch（`FunctionCallVa`）造成 RSP 偏移 8 | detour 中的 `movaps` 觸發 #GP，而且只在特定最佳化層級出現 | 改用 jmp；或用 thunk 修正對齊 |
| 4 | 手寫 thunk 沒有 unwind 資訊 | 例外發生時程序被直接終止，`__try` 接不住 | `PROC FRAME` + `.allocstack` |
| 5 | 手動映射的 shadow image 沒有 `.pdata` | 子程序注入時，偶發性地無聲消失 | shadow 只做 `LdrLoadDll`；或 `RtlAddFunctionTable` |
| 6 | prologue 搬移時遇到 RIP-relative 指令 | trampoline 讀到錯誤位址的資料 | Zydis 檢查；第一版直接放棄 hook 並記錄 log |
| 7 | 覆蓋範圍內有其他分支的目標 | 極難重現的崩潰 | 優先使用 5-byte near jump，縮小覆蓋範圍 |
| 8 | x64 的 Nt stub 格式在未來 Windows 版本改變 | hook 安裝失敗 | 驗證 stub 格式，不符時拒絕並記錄 log，不要猜 |
| 9 | 32 位元目標的 NLS 表配置在 4GB 以上（策略 A，由 x64 Loader 操作 WoW64 目標時） | 32 位元 PEB 欄位被截斷 | 配置時加 `ZeroBits` 或檢查位址（範例已加入檢查） |
| 10 | 只改了 ntdll 的 NLS，kernelbase 仍使用系統 code page | `CHECK_KERNELBASE_*` FAIL | P6 分別觀察各個 `CHECK_*`；kernelbase 必要時另外 hook |
| 11 | ACG（Arbitrary Code Guard）程序拒絕配置可執行記憶體 | relay / trampoline 配置失敗 | 偵測到 ACG 時提早失敗並給出清楚的錯誤；這類程序本來就不在支援範圍內 |
| 12 | `LoaderDll` 的 ERW 合併 section | 部分防毒軟體誤判 | x64 版評估是否需要保留合併；Loader 只在 LEProc 程序內執行，應該可以拿掉 |
| 13 | x86 / x64 共用原始碼時，`ULONG` 和 `ULONG_PTR` 混用 | 指標被截斷成 32 位元（例如 `NtdllHook.cpp:316` 的 `*(PULONG)&ooxxBuffer[1] = Offset`） | 開啟 C4311 / C4302 / C4312 警告並當成錯誤處理 |
| 14 | `LEB` 在 x64 的大小改變 | C# 與 C++ 結構不一致 → 讀到錯誤的 code page | P2 的佈局測試涵蓋兩種架構 |

---

## 6. 本文未涵蓋、需要另外研究的項目

- ARM64 Windows（ARM64EC / x64 模擬）：完全不同的 hook 問題，不在本文範圍
- 簽章與 Defender：x64 的注入行為較容易被 EDR 標記，發佈前需要評估
- 策略 A 的實測結果：會決定 P7 的工作量，請先執行 P6 再細部規劃 P7
