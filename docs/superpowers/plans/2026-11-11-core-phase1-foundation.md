# Core Phase 1（地基）Implementation Plan

> **For agentic workers:** 依 task 順序執行；每個 task 是一個 commit。步驟用 checkbox（`- [ ]`）追蹤。動手前確認 spec 的待確認決策（Q1～Q6）已有答案。

**Goal:** 在**不改變任何 x86 行為**的前提下，讓 Core 具備 x64 建置能力：清掉死碼、建置設定依平台參數化、x64 的 import lib 與執行期支援、佈局檢查、修正已知的 ml x64 錯誤，最後讓 `LoaderDll` 與 `LocaleEmulator` 能以 `Release|x64` 編譯成功（hook 暫時停用），並納入 CI。

**Architecture:** 10 個 task，線性執行。Task 1～5 是 x86 行為不變的整理；Task 6～8 建立 x64 建置；Task 9 是文件；Task 10 是最終驗證。

**Tech Stack:** MSVC v145 / MSBuild / lib.exe / MASM（本 phase 只準備，不寫 asm）/ GitHub Actions（windows-2025-vs2026）

**Spec:** `docs/superpowers/specs/2026-11-11-core-modularization-and-x64-design.md`

**Branch:** `claude/busy-wozniak-et7oxc`（本 session 指定的分支）

---

## 驗證流程（所有 task 共用）

開發環境是 Linux 雲端容器，沒有 MSVC，**所有建置與測試都靠 CI**：

1. 完成 task 的修改，commit（訊息格式見各 task）
2. push 到 `claude/busy-wozniak-et7oxc`
3. 以 `workflow_dispatch` 觸發 CI（GitHub Actions 的 `CI` workflow，ref 指定本分支）
4. 讀 job log，確認以下全部通過：
   - .NET build + test（不應受影響）
   - `Build Core (Release|Win32)`
   - `Run C++ tests`（Core.Tests、ShellExtension.Tests）
   - `Smoke Tests`：所有 `CHECK_*` 都是 `OK`
   - （Task 6 之後）`Build Core (Release|x64)`
5. 失敗時：在同一個 task 範圍內修正，追加 commit，再跑一次 CI

> **與 AGENTS.md 的落差（需要使用者同意）：** AGENTS.md 要求「驗證通過才 commit」以及「commit / push 前先問」。在沒有 Windows 的環境中，唯一的驗證方式是 push 後跑 CI，所以本計畫的流程是「先 commit + push 到工作分支 → CI 驗證 → 失敗就追加修正 commit」。工作分支上的 commit 在合併前都可以再整理。**請在開始執行前確認是否接受這個流程，以及每個 task 是否需要個別確認後才 push。**

**x86 行為不變的判斷標準：** Smoke test 全綠，而且 `dumpbin /dependents LocaleEmulator.dll` 仍然只有 ntdll（`LeCheckNoStaticKernel32` 會自動檢查）。

---

## File Structure

### 修改

| 檔案 | Task | 內容 |
|---|---|---|
| `AGENTS.md` | 0、9 | 鐵則 1、2；C++ 建置組態表；目錄說明 |
| `src/Core/LocaleEmulator/HookPort.cpp` | 1 | 刪除 `ML_KERNEL_MODE` 區段 |
| `src/Core/LocaleEmulator/HookPort.h` | 1 | 同上 |
| `src/Core/Core.props` | 2、3 | 依平台參數化；.def 目錄改為 `ImportLibs\$(LeLibMachine)\` |
| `src/Core/Runtime/CoreRuntime.cpp` | 3 | 依 `_M_IX86` / `_M_X64` 分段 |
| `src/Core/LocaleEmulator/LocaleEmulator.vcxproj` | 4、6、7 | 加入新檔案；新增 `Release\|x64` 組態 |
| `src/Core/LocaleEmulator/ml.cpp` | 5 | `Mp` 的 x64 `OpPush` 改為不支援 |
| `src/Core/LoaderDll/LoaderDll.vcxproj` | 6 | 新增 `Release\|x64` 組態 |
| `src/Core/LoaderDll/LoaderDll.cpp` | 6 | x64 下注入的 DLL 檔名（依 Q3） |
| `LocaleEmulator.sln` | 6、8 | x64 方案組態對應 |
| `.github/workflows/ci.yml` | 6、8 | 新增 `Build Core (Release\|x64)`、Core.Tests x64 |
| `tests/Core.Tests/Core.Tests.vcxproj` | 8 | 新增 `Release\|x64` 組態 |
| `src/Core/README.md` | 9 | x64 建置、.def 目錄、CoreRuntime 分段說明 |

### 新增

| 檔案 | Task | 內容 |
|---|---|---|
| `src/Core/ImportLibs/x86/ntdll_ex.def`、`kernel32_ex.def` | 2 | 由 `ImportLibs/*.def` 搬移（`git mv`），內容不變 |
| `src/Core/ImportLibs/x64/ntdll_ex.def`、`kernel32_ex.def` | 3 | x64 版本 |
| `src/Core/LocaleEmulator/Arch/LeArch.h` | 4 | TEB / PEB / NLS 位移常數 |
| `src/Core/LocaleEmulator/Arch/LayoutAsserts.cpp` | 4 | 對 ml 的 `TEB_BASE` / `PEB_BASE` 做 `static_assert` |
| `src/Core/LocaleEmulator/HookPortX64Stub.cpp` | 7 | x64 暫時替代 HookPort（全部回傳 `STATUS_NOT_SUPPORTED`） |

---

## Task 0：Phase 0 決策落地（commit 1，docs）

**前提：** 使用者已回覆 spec §3.2 的 Q1～Q6。以下以建議預設值撰寫，若使用者選擇不同，依回覆調整。

- [ ] **Step 1：修改 AGENTS.md 鐵則 1**

```markdown
1. **LEProc 與 x86 版 Core 必須維持 x86。** 32 位元目標程式走 `LoaderDll.dll` + `LocaleEmulator.dll`（x86）；x86 的 syscall hook 引擎（`HookSysCall_x86`）靠比對 `KiFastSystemCall` 的 opcode 運作。64 位元目標程式由 x64 版 Core（`LoaderDll64.dll` + `LocaleEmulator64.dll`）處理，x64 沒有 `KiFastSystemCall`，hook 引擎是另一套實作（見 `docs/superpowers/specs/2026-11-11-core-modularization-and-x64-design.md`）。x64 版尚在開發中，未納入發佈產物前不影響現有行為。
```

- [ ] **Step 2：鐵則 2 不變**（ShellExtension 仍是 x86 + x64），但在架構表的 Core 兩列「平台」欄註記「x86（x64 開發中）」。

- [ ] **Step 3：在「文件整理原則」提到的 specs 清單中，不需要逐一列檔名**；確認 AGENTS.md 沒有需要同步的其他段落（例如「建置產出」暫不改，等 Phase 5）。

- [ ] **Step 4：commit**

```
docs: 修訂鐵則 1，允許新增 x64 版 Core

x86 路徑維持不變；x64 版 Core 用於 64 位元目標程式，設計見
docs/superpowers/specs/2026-11-11-core-modularization-and-x64-design.md。
```

驗證：純文件，不需要跑 CI。

---

## Task 1：刪除 HookPort 的 kernel mode 死碼（commit 2，`[Core]`）

**目標：** 刪除 `HookPort.cpp` / `HookPort.h` 中只在 `ML_KERNEL_MODE` 為真時才會編譯的區段（約 832 行）。LE 永遠是 user mode（`ml.h:18` 定義 `ML_USER_MODE 1`，任何專案都沒有定義 `ML_KERNEL_MODE`），所以編譯結果應該完全不變。

**Files:**
- Modify: `src/Core/LocaleEmulator/HookPort.cpp`
- Modify: `src/Core/LocaleEmulator/HookPort.h`

- [ ] **Step 1：安裝 unifdef**

```bash
sudo apt-get install -y unifdef
```

裝不起來時改用手動刪除（Step 3 的清單），並逐一核對前置處理器的配對。

- [ ] **Step 2：用 unifdef 移除區段**

```bash
cd src/Core/LocaleEmulator
unifdef -UML_KERNEL_MODE -DML_USER_MODE=1 -o HookPort.cpp HookPort.cpp
unifdef -UML_KERNEL_MODE -DML_USER_MODE=1 -o HookPort.h HookPort.h
```

- `-UML_KERNEL_MODE`：把 `#if ML_KERNEL_MODE` 視為假，刪除該分支、保留 `#else` 分支
- `-DML_USER_MODE=1`：`#if ML_USER_MODE` 視為真
- **不要**動 `ML_X86` / `ML_AMD64`（unifdef 不認識的巨集會原封不動保留）
- unifdef 有修改時 exit code 是 1，屬於正常

- [ ] **Step 3：人工檢查 diff**

```bash
git diff --stat
git diff src/Core/LocaleEmulator/HookPort.cpp | less
```

確認：
- 刪除的區段都是 `#if ML_KERNEL_MODE` 的真分支，或 `#if ML_USER_MODE` 的 `#else` 分支
- `#if 0` 區段（第 706、1574 行附近）**原封不動**
- `#if ML_X86` / `#if ML_AMD64` **原封不動**
- `// r3`、`// r0` 這類只剩下單一分支後失去意義的註解可以順手刪除，其他程式碼不動

- [ ] **Step 4：確認沒有殘留**

```bash
grep -n "ML_KERNEL_MODE" src/Core/LocaleEmulator/HookPort.cpp src/Core/LocaleEmulator/HookPort.h
```

預期：沒有輸出。

- [ ] **Step 5：commit + CI**

```
[Core] refactor: 移除 HookPort 中只在 kernel mode 編譯的死碼

LE 一律以 user mode 建置（ML_USER_MODE=1，從未定義 ML_KERNEL_MODE），
這些區段從未被編譯。以 unifdef 移除，x86 編譯結果不變。
```

驗證：CI 全綠（重點：smoke test）。

---

## Task 2：Core.props 依平台參數化、.def 搬到 x86 子目錄（commit 3，`[Core]`）

**目標：** 為 x64 做準備，但這個 task 結束時 x86 的行為與產出完全相同。

**Files:**
- Move: `src/Core/ImportLibs/*.def` → `src/Core/ImportLibs/x86/`
- Modify: `src/Core/Core.props`

- [ ] **Step 1：搬移 .def**

```bash
mkdir -p src/Core/ImportLibs/x86
git mv src/Core/ImportLibs/ntdll_ex.def    src/Core/ImportLibs/x86/ntdll_ex.def
git mv src/Core/ImportLibs/kernel32_ex.def src/Core/ImportLibs/x86/kernel32_ex.def
```

並把兩個檔案開頭註解裡的「(x86 only)」保留，`kernel32_ex.def` 中「See ntdll_ex.def」的相對說明不需要改（同目錄）。

- [ ] **Step 2：Core.props 新增平台屬性**

在第一個 `<PropertyGroup>` 中加入：

```xml
    <LeLibMachine Condition="'$(Platform)' == 'Win32'">x86</LeLibMachine>
    <LeLibMachine Condition="'$(Platform)' == 'x64'">x64</LeLibMachine>
```

- [ ] **Step 3：.def 來源改為平台子目錄**

```xml
  <ItemGroup>
    <LeImportDef Include="$(LeCoreDir)ImportLibs\$(LeLibMachine)\*.def" />
  </ItemGroup>
```

- [ ] **Step 4：lib.exe 的 machine 參數**

```xml
    <Exec Command="lib.exe /nologo /machine:$(LeLibMachine) /def:&quot;%(LeImportDef.FullPath)&quot; /out:&quot;$(LeImportLibDir)%(LeImportDef.Filename).lib&quot;" />
```

- [ ] **Step 5：x86 專屬設定移到條件區塊**

把共用 `ItemDefinitionGroup` 裡的：
- `<EnableEnhancedInstructionSet>StreamingSIMDExtensions</EnableEnhancedInstructionSet>`
- `<ImageHasSafeExceptionHandlers>false</ImageHasSafeExceptionHandlers>`

移到新的區塊：

```xml
  <ItemDefinitionGroup Condition="'$(Platform)' == 'Win32'">
    <ClCompile>
      <EnableEnhancedInstructionSet>StreamingSIMDExtensions</EnableEnhancedInstructionSet>
    </ClCompile>
    <Link>
      <ImageHasSafeExceptionHandlers>false</ImageHasSafeExceptionHandlers>
    </Link>
  </ItemDefinitionGroup>
```

x64 區塊在 Task 6 才加入。

- [ ] **Step 6：更新 Core.props 檔頭註解**，說明 `ImportLibs\<machine>\` 的結構。

- [ ] **Step 7：檢查**

```bash
grep -n "ImportLibs\|LeLibMachine\|machine:" src/Core/Core.props
```

確認沒有殘留的 `ImportLibs\*.def` 或 `/machine:x86`。

- [ ] **Step 8：commit + CI**

```
[Core] build: Core.props 依平台參數化，.def 移到 ImportLibs\x86\

為 x64 建置做準備；x86 的建置參數與產出不變。
```

驗證：CI 全綠；`Build Core (Release|Win32)` 的 log 中可以看到 `lib.exe /machine:x86 ... ImportLibs\x86\...`。

---

## Task 3：x64 的 .def 與 CoreRuntime 依架構分段（commit 4，`[Core]`）

**Files:**
- Create: `src/Core/ImportLibs/x64/ntdll_ex.def`
- Create: `src/Core/ImportLibs/x64/kernel32_ex.def`
- Modify: `src/Core/Runtime/CoreRuntime.cpp`

- [ ] **Step 1：建立 `ImportLibs/x64/kernel32_ex.def`**

```def
; kernel32.dll exports that the Windows SDK's kernel32.lib does not provide (x64).
; Built into kernel32_ex.lib by Core.props.
; x64 has a single calling convention: names carry no "@N" decoration.
LIBRARY kernel32.dll
EXPORTS
    CreateProcessInternalW
```

- [ ] **Step 2：建立 `ImportLibs/x64/ntdll_ex.def`**

```def
; ntdll.dll exports that the Windows SDK's ntdll.lib does not provide (x64).
; Built into ntdll_ex.lib by Core.props.
;
; Differences from x86\ntdll_ex.def:
;   - no "@N" decoration and no leading underscore on x64
;   - no 64-bit arithmetic / float conversion helpers (_alldiv, _ftol, ...):
;     x64 has native instructions for them
;   - stack probe is __chkstk; SEH handler is __C_specific_handler
;     (x64 SEH is table based, see Runtime\CoreRuntime.cpp)
LIBRARY ntdll.dll
EXPORTS
    ; Undocumented loader APIs
    LdrInitializeThunk
    LdrRegisterDllNotification
    LdrUnregisterDllNotification

    ; NLS conversion
    RtlUpcaseUnicodeStringToAnsiString

    ; Compiler helpers
    __chkstk
    __C_specific_handler

    ; C runtime functions
    memcmp
    memcpy
    memmove
    memset
    strlen
    wcslen
    wcsstr
    _stricmp
    _wcsicmp
    wcsicmp                                 EXPORTAS _wcsicmp
    _snwprintf
    _vsnprintf
    _vsnwprintf
    _vscwprintf
    swprintf
    vswprintf                               EXPORTAS _vsnwprintf
```

> 這份清單要在 Task 7 的 x64 建置中實際驗證：若 SDK 的 x64 `ntdll.lib` 已經提供某個符號，linker 會出 LNK4006 警告，屆時從這裡移除該項。

- [ ] **Step 3：CoreRuntime.cpp 依架構分段**

整體結構改為：

```cpp
// （檔頭註解：補充 x64 的說明）
//   - x64: __try/__except uses table-based SEH (.pdata/.xdata) and ntdll's
//     __C_specific_handler (ImportLibs\x64\ntdll_ex.def); nothing to implement here.

#include ...

extern "C" IMAGE_DOS_HEADER __ImageBase;
// RvaToPtr、LdrLoadDll 等宣告：不變

extern "C" {

// delay-load helper：不變（與架構無關）

// _fltused：不變（兩個架構都需要）

#if defined(_M_IX86)

// _alloca_probe 宣告與 _alloca_probe_16
// __security_cookie
// EH3_* 結構、Eh3CallFunclet、Eh3JumpToHandler、Eh3GlobalUnwind、Eh3LocalUnwind
// _except_handler3、NoCookieCheck、_except_handler4_common 宣告、_except_handler4
//（以上全部原封不動，只是包進 #if）

#elif defined(_M_X64)

// x64: SEH frames are described by .pdata/.xdata and dispatched through ntdll's
// __C_specific_handler. /GS- means __security_cookie / __GSHandlerCheck are not
// referenced. _alloca is expanded inline with __chkstk.

#else
#error "Core supports x86 and x64 only"
#endif

} // extern "C"
```

注意：
- `LoadDelayDll` 中的 `InterlockedCompareExchangePointer` 兩個架構都可用
- `__delayLoadHelper2` 中的 `(RVA)Thunk->u1.AddressOfData` 在 x64 是 `ULONGLONG` 轉 `RVA`（`DWORD`），屬於有意的截斷，**加上註解**說明 import-by-name 的值本來就是 RVA

- [ ] **Step 4：確認 x86 區塊內容未變**

```bash
git diff -w src/Core/Runtime/CoreRuntime.cpp
```

`-w` 忽略空白後，差異應該只有新增的 `#if` / `#elif` / `#else` / `#endif` 與註解。

- [ ] **Step 5：commit + CI**

```
[Core] build: 新增 x64 的 .def，CoreRuntime 依架構分段

x86 的 SEH handler 與 _alloca_probe_16 原封不動移入 #if defined(_M_IX86)；
x64 使用 ntdll 的 __C_specific_handler 與 __chkstk，不需要額外實作。
```

驗證：CI 全綠（x64 的 .def 此時還不會被使用）。

---

## Task 4：LeArch.h 與佈局 static_assert（commit 5，`[Core]`）

**目標：** 用編譯期檢查鎖住 ml 的 `TEB_BASE` / `PEB_BASE` 中 LE 會用到的欄位位移，x86 與 x64 各自驗證。

**Files:**
- Create: `src/Core/LocaleEmulator/Arch/LeArch.h`
- Create: `src/Core/LocaleEmulator/Arch/LayoutAsserts.cpp`
- Modify: `src/Core/LocaleEmulator/LocaleEmulator.vcxproj`

- [ ] **Step 1：建立 `Arch/LeArch.h`**

內容以研究報告 §2.5 為準，本 task 只放「位移常數」與「讀取當前 TEB / PEB」的函式，**不放**遠端改寫（Phase 3 才需要）：

```cpp
#pragma once

// Architecture-specific TEB / PEB offsets used by Locale Emulator.
// Values are locked against ml.h's TEB_BASE / PEB_BASE by Arch\LayoutAsserts.cpp.

#include <intrin.h>

#if defined(_M_X64)
    #define LE_TEB_SELF_OFFSET      0x30
    #define LE_TEB_PEB_OFFSET       0x60
    #define LE_PEB_ANSI_CP_OFFSET   0x0A0
    #define LE_PEB_OEM_CP_OFFSET    0x0A8
    #define LE_PEB_CASE_OFFSET      0x0B0
#elif defined(_M_IX86)
    #define LE_TEB_SELF_OFFSET      0x18
    #define LE_TEB_PEB_OFFSET       0x30
    #define LE_PEB_ANSI_CP_OFFSET   0x058
    #define LE_PEB_OEM_CP_OFFSET    0x05C
    #define LE_PEB_CASE_OFFSET      0x060
#else
    #error "Locale Emulator Core supports x86 and x64 only"
#endif

// Offsets inside a 32-bit / 64-bit PEB, independent of the current build.
// Used when patching another process (Phase 3).
#define LE_PEB32_ANSI_CP_OFFSET     0x058
#define LE_PEB32_OEM_CP_OFFSET      0x05C
#define LE_PEB32_CASE_OFFSET        0x060
#define LE_PEB64_ANSI_CP_OFFSET     0x0A0
#define LE_PEB64_OEM_CP_OFFSET      0x0A8
#define LE_PEB64_CASE_OFFSET        0x0B0
```

- [ ] **Step 2：建立 `Arch/LayoutAsserts.cpp`**

```cpp
// Compile-time checks that ml.h's NT structure definitions match the offsets in
// LeArch.h for the architecture being built. This file generates no code.

#include "ml.h"
#include "Arch/LeArch.h"

static_assert(FIELD_OFFSET(TEB_BASE, NtTib.Self)               == LE_TEB_SELF_OFFSET,    "TEB_BASE.NtTib.Self");
static_assert(FIELD_OFFSET(TEB_BASE, ProcessEnvironmentBlock)  == LE_TEB_PEB_OFFSET,     "TEB_BASE.ProcessEnvironmentBlock");
static_assert(FIELD_OFFSET(PEB_BASE, AnsiCodePageData)         == LE_PEB_ANSI_CP_OFFSET, "PEB_BASE.AnsiCodePageData");
static_assert(FIELD_OFFSET(PEB_BASE, OemCodePageData)          == LE_PEB_OEM_CP_OFFSET,  "PEB_BASE.OemCodePageData");
static_assert(FIELD_OFFSET(PEB_BASE, UnicodeCaseTableData)     == LE_PEB_CASE_OFFSET,    "PEB_BASE.UnicodeCaseTableData");

#if defined(_M_IX86)
static_assert(LE_PEB32_ANSI_CP_OFFSET == LE_PEB_ANSI_CP_OFFSET, "PEB32 constants");
#elif defined(_M_X64)
static_assert(LE_PEB64_ANSI_CP_OFFSET == LE_PEB_ANSI_CP_OFFSET, "PEB64 constants");
#endif
```

> 如果 x86 建置時任何一個 assert 失敗，代表 ml 的結構定義或本表數值有誤：**先查 WinDbg `dt ntdll!_PEB` 的公開資料確認正確值**，不要直接改數字讓它通過。

- [ ] **Step 3：加入 vcxproj**

```xml
    <ClCompile Include="Arch\LayoutAsserts.cpp" />
```

```xml
    <ClInclude Include="Arch\LeArch.h" />
```

- [ ] **Step 4：commit + CI**

```
[Core] test: 新增 LeArch.h 並以 static_assert 鎖住 TEB/PEB 位移

x86 / x64 各自在編譯期驗證 ml 的 TEB_BASE / PEB_BASE 與已知位移一致，
為 x64 的 NLS 處理做準備。不產生任何程式碼。
```

驗證：CI 全綠（x86 assert 在 `Build Core (Release|Win32)` 中被檢查）。

---

## Task 5：停用 ml 錯誤的 x64 push/ret 跳轉（commit 6，`[Core]`）

**背景：** `ml.cpp` 約第 9220 行，`Mp` 在 `ML_AMD64` 下的 `OpPush` 會產生：`68 <high32>`，接著因為 `Buffer += POINTER_SIZE`（8）而**跳過 4 個未初始化的 bytes**，再寫 `C7 04 24 <low32>`（`mov dword [rsp], low32`）與 `C3`。結果是：高 32 位元遺失、中間夾著會被執行的垃圾 bytes；即使修正編碼，push/ret 也和 CET shadow stack 不相容（spec D4）。

**Files:**
- Modify: `src/Core/LocaleEmulator/ml.cpp`（`#elif ML_AMD64` 下的 `case OpPush:`）

- [ ] **Step 1：改為回傳不支援**

```cpp
            case OpPush:
                //
                // push/ret is not used on x64: the original encoding here was
                // wrong (dropped the high 32 bits) and push/ret breaks CET
                // shadow stacks. x64 hooks go through Le::Hook (HookEngineX64).
                //
                return STATUS_NOT_SUPPORTED;
```

- [ ] **Step 2：確認呼叫端在 x64 不會預設使用 OpPush**

```bash
grep -n "OpPush" src/Core/LocaleEmulator/ml.cpp src/Core/LocaleEmulator/ml.h | head -20
```

記錄預設 hook op 的選擇邏輯。若 x64 預設就是 `OpPush`，在 commit 訊息中註明「x64 的 `Mp::PatchMemory` 目前一律失敗，屬預期；x64 hook 在 Phase 3 由新引擎提供」。

- [ ] **Step 3：commit + CI**

```
[Core] fix: 停用 ml 在 x64 下錯誤的 push/ret 跳轉編碼

原本的編碼遺失目標位址的高 32 位元，且 push/ret 與 CET shadow stack
不相容。x64 的 hook 將由新的 HookEngine 提供。x86 不受影響。
```

驗證：CI 全綠（x86 不編譯這段）。

---

## Task 6：新增 `Release|x64` 組態與 CI 建置步驟（commit 7，`[Core]`）

**目標：** 專案與 CI 能「嘗試」建置 x64。這個 task 允許 x64 建置失敗（`continue-on-error`），用來取得完整的錯誤清單。

**Files:**
- Modify: `src/Core/LoaderDll/LoaderDll.vcxproj`
- Modify: `src/Core/LocaleEmulator/LocaleEmulator.vcxproj`
- Modify: `src/Core/Core.props`
- Modify: `src/Core/LoaderDll/LoaderDll.cpp`
- Modify: `LocaleEmulator.sln`
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1：兩個 vcxproj 新增 `Release|x64`**

以 `Release|Win32` 為範本，各區塊複製一份 `Condition="'$(Configuration)|$(Platform)'=='Release|x64'"`，差異如下：

| 設定 | Win32（不變） | x64 |
|---|---|---|
| `OutDir` / `IntDir` | `.\Release\` | `.\x64\Release\` |
| `TargetName` | （預設） | `LoaderDll64` / `LocaleEmulator64`（依 Q3） |
| `PreprocessorDefinitions` | `WIN32;...` | 拿掉 `WIN32`，其餘相同 |
| `CallingConvention` | `FastCall` | 刪除（x64 忽略） |
| `Midl/TargetEnvironment` | `Win32` | `X64` |
| `Link/OutputFile`、`ImportLibrary` | `.\Release\xxx.dll` | `$(OutDir)$(TargetName)$(TargetExt)`、`$(OutDir)$(TargetName).lib` |
| `RandomizedBaseAddress` | `false` | 保持 `false`（與 x86 一致，之後再評估） |
| `ReferencePath`（LoaderDll） | `$(VC_ReferencesPath_x86)` | `$(VC_ReferencesPath_x64)` |
| `LeForbidStaticKernel32`（LocaleEmulator） | `true` | `true` |

- [ ] **Step 2：Core.props 加入 x64 區塊**

```xml
  <ItemDefinitionGroup Condition="'$(Platform)' == 'x64'">
    <ClCompile>
      <!-- x64 always has SSE2; /arch:SSE is not valid on x64. -->
      <EnableEnhancedInstructionSet>NotSet</EnableEnhancedInstructionSet>
    </ClCompile>
    <Link>
      <TargetMachine>MachineX64</TargetMachine>
      <AdditionalOptions>/CETCOMPAT:NO %(AdditionalOptions)</AdditionalOptions>
    </Link>
  </ItemDefinitionGroup>
```

- [ ] **Step 3：LoaderDll.cpp 的注入 DLL 檔名**

`LeCreateProcess2` 中：

```cpp
#if defined(_M_X64)
	static WCHAR Dll[] = L"LocaleEmulator64.dll";
#else
	static WCHAR Dll[] = L"LocaleEmulator.dll";
#endif
```

（若 Q3 選擇子資料夾方案，此步驟略過。）

- [ ] **Step 4：sln 的方案組態對應**

`LoaderDll`（`{7015572F-...}`）與 `LocaleEmulator`（`{B8B77FAC-...}`）：

```
{GUID}.Release|x64.ActiveCfg = Release|x64
{GUID}.Release|x64.Build.0 = Release|x64
```

取代目前的 `Release|x64.ActiveCfg = Release|Win32`。`Loader`（測試小工具）維持只有 Win32。

- [ ] **Step 5：CI 新增 step**（放在 `Build Core (Release|Win32)` 之後）

```yaml
      - name: Build Core (Release|x64)
        # Phase 1: x64 Core is not shipped yet. Allowed to fail until Task 7 is done.
        continue-on-error: true
        run: |
          msbuild src/Core/LoaderDll/LoaderDll.vcxproj /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=$env:SOLUTION_DIR" /v:minimal
          msbuild src/Core/LocaleEmulator/LocaleEmulator.vcxproj /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=$env:SOLUTION_DIR" /v:minimal
        shell: pwsh
```

x64 產物**不**複製到 `Build/Release/`（Phase 5 才組裝）。

- [ ] **Step 6：commit + CI**

```
[Core] build: LoaderDll / LocaleEmulator 新增 Release|x64 組態

x64 版輸出 LoaderDll64.dll / LocaleEmulator64.dll，尚未納入發佈產物。
CI 新增 x64 建置步驟，暫時允許失敗以收集錯誤清單。
```

- [ ] **Step 7：整理錯誤清單**

從 CI log 擷取 `Build Core (Release|x64)` 的所有 `error C....` / `error LNK....`，依檔案與類型分組，寫進 Task 7 的工作清單（可以直接放在 PR 說明或回報給使用者）。

驗證：x86 全綠；x64 step 可以失敗，但必須確實執行到編譯階段（不是 MSBuild 設定錯誤）。

---

## Task 7：修正 x64 編譯錯誤，hook 暫時停用（commit 8～N，`[Core]`）

**目標：** `Build Core (Release|x64)` 成功，並移除 `continue-on-error`。

**原則：**
1. **x86 路徑一行都不能變。** 所有修改都要包在 `#if defined(_M_X64)` / `#if ML_AMD64`，或是 x86 / x64 語意相同的型別修正（例如 `ULONG` → `ULONG_PTR`，要在 commit 訊息說明為什麼 x86 不受影響）
2. **不修 hook 邏輯。** x64 的 hook 一律停用，正確實作是 Phase 3 的工作
3. **一類錯誤一個 commit**，方便回溯

**預期會遇到的錯誤類型與處理方式：**

| 類型 | 預期來源 | 處理 |
|---|---|---|
| `__asm` 不支援（C4235 / C2400） | `HookPort.cpp`、`ml.cpp` 部分區段 | x64 建置排除 `HookPort.cpp`（見下）；ml 中的 asm 若未被 `ML_X86` 包住，補上條件 |
| `naked` 不支援 | 同上 | 同上 |
| 指標截斷（C4311 / C4302 / C4312） | 各處 `(ULONG)指標` | 只在 x64 實際錯誤的地方修正；警告層級的先記錄，不在本 task 修 |
| 未解析的外部符號（LNK2019） | `.def` 清單不完整、x86 專屬 helper | 依 log 補齊 `ImportLibs/x64/*.def` |
| 重複定義（LNK4006） | SDK 的 x64 `ntdll.lib` 已提供 | 從 x64 `.def` 移除該項 |
| ml 的 x64 路徑其他錯誤 | 未知 | 逐一處理；若某區段 LE 根本沒用到，可用 `#if` 排除 |

- [ ] **Step 1：x64 建置排除 HookPort.cpp，改用 stub**

`LocaleEmulator.vcxproj`：

```xml
    <ClCompile Include="HookPort.cpp">
      <ExcludedFromBuild Condition="'$(Platform)'=='x64'">true</ExcludedFromBuild>
    </ClCompile>
    <ClCompile Include="HookPortX64Stub.cpp">
      <ExcludedFromBuild Condition="'$(Platform)'=='Win32'">true</ExcludedFromBuild>
    </ClCompile>
```

`HookPortX64Stub.cpp`：對 `HookPort.h` 中 LE 實際呼叫的每個函式（`InstallHookPort`、`UnInstallHookPort`、`HpInitializeWin32kPort`、`HpAddSystemCallByRoutine` 等，以編譯錯誤為準）提供回傳 `STATUS_NOT_SUPPORTED` 的實作，檔頭註明「Phase 3 由 HookEngineX64 取代」。

- [ ] **Step 2：x64 下 LeGlobalData::Initialize 不安裝 hook**

`LocaleEmulator.cpp` 的 `LeGlobalData::Initialize()`，在 `InstallHookPort()` 之前：

```cpp
#if defined(_M_X64)
    // Phase 1: the x64 build only has to compile. Hooks are installed by
    // HookEngineX64 starting in Phase 3.
    return STATUS_NOT_SUPPORTED;
#endif
```

確切位置以不影響 x86 流程為準（放在 `#if` 內，x86 完全看不到）。

- [ ] **Step 3：依 Task 6 的錯誤清單逐類修正**，每類一個 commit：

```
[Core] build(x64): <錯誤類型的摘要>

<哪些檔案、怎麼修、為什麼 x86 不受影響>
```

每個 commit 都跑 CI，確認 x86 全綠。

- [ ] **Step 4：x64 靜態相依檢查**

`LeCheckNoStaticKernel32` 在 x64 也會執行；確認 `LocaleEmulator64.dll` 沒有靜態 import kernel32。若失敗，**依鐵則 4 修正**，不要停用檢查。

- [ ] **Step 5：移除 `continue-on-error`**（最後一個 commit）

```
ci: Core x64 建置改為必須通過
```

驗證：CI 全綠，包含 `Build Core (Release|x64)`。

---

## Task 8：Core.Tests 新增 x64 組態（commit，`test`）

**目標：** C# ↔ C++ 結構契約在 x64 也被驗證。依 spec F7，`LEB` 沒有指標欄位，預期 x86 / x64 的大小與位移完全相同。

**Files:**
- Modify: `tests/Core.Tests/Core.Tests.vcxproj`
- Modify: `LocaleEmulator.sln`
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1：vcxproj 新增 `Release|x64`**

```xml
    <OutDir>$(SolutionDir)Build\$(Configuration)\x64\</OutDir>
    <IntDir>$(SolutionDir)Build\Intermediate\Core.Tests\$(Configuration)\x64\</IntDir>
```

其餘設定比照 `Release|Win32`。

- [ ] **Step 2：sln 對應 `Release|x64.ActiveCfg / Build.0 = Release|x64`**

- [ ] **Step 3：CI 的 `Build ShellExtension + tests (Release|x64)` step 加入**

```
msbuild tests/Core.Tests/Core.Tests.vcxproj /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=$env:SOLUTION_DIR" /v:minimal
```

`Run C++ tests` 已經會執行 `Build\Release\x64\*Tests*.exe`，不需要修改。

- [ ] **Step 4：檢查 main.cpp 是否有 x86 專屬假設**

```bash
grep -n "sizeof\|offsetof\|ULONG_PTR\|PVOID\|_M_IX86\|_WIN64" tests/Core.Tests/main.cpp
```

若有任何與指標大小相關的預期值，**先確認 C# 端（`LoaderWrapper`）在 x64 的實際佈局**，再決定是共用預期值還是依架構分開。

- [ ] **Step 5：commit + CI**

```
test(core): Core.Tests 新增 x64 組態，驗證結構佈局在 x64 一致
```

驗證：CI 的 C++ 測試同時執行 x86 與 x64 版 Core.Tests，皆通過。

---

## Task 9：文件更新（commit，`docs`）

**Files:**
- Modify: `AGENTS.md`
- Modify: `src/Core/README.md`

- [ ] **Step 1：AGENTS.md 的 C++ 建置組態表**

| C++ 專案 | Debug\|Win32 | Release\|Win32 | Debug\|x64 | Release\|x64 |
|---|:---:|:---:|:---:|:---:|
| Core/LoaderDll | - | ✓ | - | ✓（開發中，未發佈） |
| Core/LocaleEmulator | - | ✓ | - | ✓（開發中，hook 停用） |
| Core.Tests | ✓ | ✓ | - | ✓ |

- [ ] **Step 2：AGENTS.md 鐵則 3** 補充：「x64 的 import lib 定義在 `src/Core/ImportLibs/x64/`；x64 SEH 使用 ntdll 的 `__C_specific_handler`」。

- [ ] **Step 3：src/Core/README.md** 新增「x64 建置」小節：`ImportLibs\<machine>\` 結構、`CoreRuntime.cpp` 的分段、`HookPortX64Stub.cpp` 的暫時性質、`LeArch.h` 的用途。

- [ ] **Step 4：commit**

```
docs: 更新 Core 的 x64 建置說明
```

---

## Task 10：最終驗證

- [ ] CI 全綠：.NET、Core x86、Core x64、C++ 測試（x86 + x64）、smoke test
- [ ] `git log --oneline` 中所有 `src/Core/` 的 commit 都有 `[Core]` 前綴
- [ ] 下載 CI artifacts，確認 `Build/Release/x86/` 的內容與 Phase 1 開始前相同（檔案清單一致，沒有多出 x64 Core）
- [ ] 回報使用者：x64 錯誤清單的處理摘要、`.def` 最終內容、遇到的 ml x64 問題，作為撰寫 Phase 2 計畫的輸入

---

## 潛在問題

| 問題 | 徵兆 | 處理 |
|---|---|---|
| unifdef 誤刪 | x86 編譯錯誤或 smoke test 失敗 | 改用手動刪除，逐段比對 |
| `LeLibMachine` 在某些 MSBuild 評估階段為空 | `lib.exe /machine:` 參數錯誤 | 加上 `Condition` 檢查並 `<Error>` |
| ml 的 x64 錯誤數量非常多 | Task 7 拖很久 | 先回報使用者錯誤清單與估計，再決定是否把 x64 不需要的 ml 區段整段排除 |
| `/CETCOMPAT:NO` 在此版 linker 不被接受 | LNK 警告或錯誤 | 移除該參數（DLL 預設本來就不是 CET 相容） |
| `WholeProgramOptimization` 與 x64 stub 互動 | 連結錯誤 | 比照 `CoreRuntime.cpp` 對特定檔案關閉 `/GL` |
